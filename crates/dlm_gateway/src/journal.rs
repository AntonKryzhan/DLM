use crate::{
    crypto, ensure,
    identity::{hex, read_bounded, replace, sync_parent, unhex, write_new},
    now, Error, Result,
};
use std::{
    collections::BTreeMap,
    fs::{self, File, OpenOptions},
    io::{self, Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
};
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum State {
    Pending = 1,
    Committed = 2,
    Acknowledged = 3,
}
#[derive(Clone, Debug)]
pub struct Entry {
    pub id: [u8; 16],
    pub owner: [u8; 32],
    pub expires: u64,
    pub state: State,
    pub digest: [u8; 32],
    pub payload: Vec<u8>,
}
pub struct Journal {
    directory: PathBuf,
    key: [u8; 32],
    entries: BTreeMap<[u8; 16], Entry>,
    quota: usize,
    maximum: usize,
    used: usize,
    _lock: File,
}
impl Drop for Journal {
    fn drop(&mut self) {
        crypto::wipe(&mut self.key)
    }
}
impl Journal {
    pub fn open(directory: &Path, key: [u8; 32], quota: usize, maximum: usize) -> Result<Self> {
        ensure(
            (1024..=67108864).contains(&quota) && (1..=4096).contains(&maximum),
            4,
            "invalid journal quotas",
        )?;
        if !directory.exists() {
            let mut builder = fs::DirBuilder::new();
            builder.recursive(true);
            #[cfg(unix)]
            {
                use std::os::unix::fs::DirBuilderExt;
                builder.mode(0o700);
            }
            builder.create(directory)?;
        }
        ensure(
            fs::symlink_metadata(directory)?.is_dir(),
            4,
            "journal path must be a directory",
        )?;
        let lock_path = directory.join("journal.lock");
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true).truncate(false);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let lock = options.open(lock_path)?;
        lock.try_lock()
            .map_err(|_| Error::new(7, "journal is already open in another process"))?;
        let mut journal = Self {
            directory: directory.to_path_buf(),
            key,
            entries: BTreeMap::new(),
            quota,
            maximum,
            used: 0,
            _lock: lock,
        };
        for (index, item) in fs::read_dir(directory)?.enumerate() {
            ensure(index < maximum * 2 + 64, 7, "journal directory entry quota")?;
            let item = item?;
            let name = item.file_name();
            let Some(name) = name.to_str() else {
                return Err(Error::new(11, "non-UTF8 journal filename"));
            };
            if name == "delivery-v2" && item.file_type()?.is_dir() {
                continue;
            }
            if name == "journal.lock" {
                continue;
            }
            if let Some((stem, suffix)) = name.split_once(".tmp-") {
                ensure(
                    stem.len() == 32
                        && unhex::<16>(stem).is_ok()
                        && suffix.len() == 16
                        && unhex::<8>(suffix).is_ok()
                        && item.file_type()?.is_file(),
                    11,
                    "unknown journal temporary entry",
                )?;
                fs::remove_file(item.path())?;
                continue;
            }
            ensure(
                name.ends_with(".dgw") && name.len() == 36 && item.file_type()?.is_file(),
                11,
                "unknown journal entry",
            )?;
            let id = unhex::<16>(&name[..32])?;
            let bytes = read_bounded(&item.path(), 1048576)?;
            ensure(
                bytes.len() >= 116 && &bytes[..4] == b"DGJ1" && bytes[4..20] == id,
                11,
                "invalid journal entry header",
            )?;
            let plain = crypto::Bytes(crypto::journal(
                false,
                &journal.key,
                &bytes[..20],
                &bytes[20..],
            )?);
            let entry = decode_entry(id, &plain.0)?;
            ensure(
                journal.entries.len() < maximum
                    && journal
                        .used
                        .checked_add(bytes.len())
                        .is_some_and(|v| v <= quota),
                7,
                "stored journal exceeds configured quota",
            )?;
            journal.used += bytes.len();
            journal.entries.insert(id, entry);
        }
        Ok(journal)
    }
    pub(crate) fn storage_id(&mut self) -> Result<[u8; 32]> {
        self._lock.seek(SeekFrom::Start(0))?;
        let mut bytes = Vec::new();
        (&mut self._lock).take(93).read_to_end(&mut bytes)?;
        if bytes.is_empty() {
            let id = crypto::random::<32>()?;
            let sealed = crypto::journal(true, &self.key, b"DLN-STORAGE-ID1", &id)?;
            self._lock.seek(SeekFrom::Start(0))?;
            self._lock.write_all(&sealed)?;
            self._lock.sync_all()?;
            sync_parent(&self.directory.join("journal.lock"))?;
            return Ok(id);
        }
        ensure(bytes.len() == 92, 11, "stored gateway identity is truncated or oversized")?;
        let id = crypto::Bytes(crypto::journal(false, &self.key, b"DLN-STORAGE-ID1", &bytes)?);
        ensure(id.0.len() == 32, 11, "stored gateway identity size invalid")?;
        Ok(id.0.as_slice().try_into().unwrap())
    }
    pub fn get(&self, id: &[u8; 16]) -> Option<&Entry> {
        self.entries.get(id)
    }
    pub fn pending(&self) -> Vec<Entry> {
        self.entries
            .values()
            .filter(|e| e.state == State::Pending && e.expires > now())
            .cloned()
            .collect()
    }
    pub(crate) fn owner_usage(&self, owner: &[u8; 32]) -> (usize, usize) {
        let mut count = 0;
        let mut bytes = 0;
        for entry in self.entries.values().filter(|e| &e.owner == owner) {
            count += 1;
            bytes += 157 + entry.payload.len();
        }
        (count, bytes)
    }
    pub fn count(&self) -> usize {
        self.entries.len()
    }
    pub fn used(&self) -> usize {
        self.used
    }
    pub fn prune(&mut self) -> Result<usize> {
        let ids: Vec<_> = self
            .entries
            .values()
            .filter(|e| e.expires <= now())
            .map(|e| e.id)
            .collect();
        for id in &ids {
            let p = self.path(id);
            let size = fs::metadata(&p)?.len() as usize;
            fs::remove_file(p)?;
            self.used = self.used.saturating_sub(size);
            self.entries.remove(id);
        }
        if !ids.is_empty() {
            sync_parent(&self.directory.join("entry"))?;
        }
        Ok(ids.len())
    }
    pub fn put(
        &mut self,
        id: [u8; 16],
        owner: [u8; 32],
        expires: u64,
        payload: &[u8],
        state: State,
    ) -> Result<State> {
        let current = now();
        ensure(
            !matches!(state, State::Acknowledged)
                && payload.len() <= 65500
                && expires > current
                && expires - current <= 86400,
            4,
            "invalid durable message or TTL",
        )?;
        self.prune()?;
        let digest = content_digest(&id, &owner, expires, payload)?;
        if let Some(old) = self.entries.get(&id) {
            ensure(
                old.owner == owner && old.expires == expires && old.digest == digest,
                18,
                "durable message identity conflict",
            )?;
            return Ok(old.state);
        }
        ensure(
            self.entries.len() < self.maximum,
            7,
            "journal message quota",
        )?;
        let entry = Entry {
            id,
            owner,
            expires,
            state,
            digest,
            payload: payload.to_vec(),
        };
        let bytes = self.encode(&entry)?;
        ensure(
            self.used
                .checked_add(bytes.len())
                .is_some_and(|v| v <= self.quota),
            7,
            "journal byte quota",
        )?;
        replace(&self.path(&id), &bytes)?;
        self.used += bytes.len();
        self.entries.insert(id, entry);
        Ok(state)
    }
    pub fn mark(&mut self, id: [u8; 16], state: State) -> Result<()> {
        let mut entry = self
            .entries
            .get(&id)
            .cloned()
            .ok_or(Error::new(2, "journal message absent"))?;
        ensure(
            state as u8 >= entry.state as u8,
            18,
            "durable state cannot move backwards",
        )?;
        entry.state = state;
        if state == State::Acknowledged {
            entry.payload.clear();
        }
        let bytes = self.encode(&entry)?;
        let p = self.path(&id);
        let previous = fs::metadata(&p)?.len() as usize;
        ensure(
            self.used.saturating_sub(previous) + bytes.len() <= self.quota,
            7,
            "journal update quota",
        )?;
        replace(&p, &bytes)?;
        self.used = self.used - previous + bytes.len();
        self.entries.insert(id, entry);
        Ok(())
    }
    fn path(&self, id: &[u8; 16]) -> PathBuf {
        self.directory.join(format!("{}.dgw", hex(id)))
    }
    fn encode(&self, entry: &Entry) -> Result<Vec<u8>> {
        let mut header = b"DGJ1".to_vec();
        header.extend_from_slice(&entry.id);
        let mut plain = Vec::with_capacity(77 + entry.payload.len());
        plain.push(entry.state as u8);
        plain.extend_from_slice(&entry.owner);
        plain.extend_from_slice(&entry.expires.to_be_bytes());
        plain.extend_from_slice(&entry.digest);
        plain.extend_from_slice(&(entry.payload.len() as u32).to_be_bytes());
        plain.extend_from_slice(&entry.payload);
        let encrypted = crypto::journal(true, &self.key, &header, &plain)?;
        crypto::wipe(&mut plain);
        header.extend_from_slice(&encrypted);
        Ok(header)
    }
}
pub(crate) fn content_digest(
    id: &[u8; 16],
    owner: &[u8; 32],
    expires: u64,
    payload: &[u8],
) -> Result<[u8; 32]> {
    let mut v = b"DLN-DURABLE-MESSAGE1".to_vec();
    v.extend_from_slice(id);
    v.extend_from_slice(owner);
    v.extend_from_slice(&expires.to_be_bytes());
    v.extend_from_slice(payload);
    crypto::hash(&v)
}
fn decode_entry(id: [u8; 16], plain: &[u8]) -> Result<Entry> {
    ensure(plain.len() >= 77, 11, "truncated durable entry")?;
    let state = match plain[0] {
        1 => State::Pending,
        2 => State::Committed,
        3 => State::Acknowledged,
        _ => return Err(Error::new(11, "invalid durable state")),
    };
    let owner = plain[1..33].try_into().unwrap();
    let expires = u64::from_be_bytes(plain[33..41].try_into().unwrap());
    let digest = plain[41..73].try_into().unwrap();
    let n = u32::from_be_bytes(plain[73..77].try_into().unwrap()) as usize;
    ensure(
        n <= 65500 && plain.len() == 77 + n && (state != State::Acknowledged || n == 0),
        11,
        "invalid durable payload length",
    )?;
    let payload = plain[77..].to_vec();
    if state != State::Acknowledged {
        ensure(
            content_digest(&id, &owner, expires, &payload)? == digest,
            12,
            "durable content digest mismatch",
        )?;
    }
    Ok(Entry {
        id,
        owner,
        expires,
        state,
        digest,
        payload,
    })
}
pub fn load_key(path: &Path) -> Result<[u8; 32]> {
    let secret = crypto::Bytes(read_bounded(path, 32)?);
    let bytes = &secret.0;
    ensure(
        bytes.len() == 32,
        4,
        "journal key must contain exactly 32 bytes",
    )?;
    Ok(bytes.as_slice().try_into().unwrap())
}
pub fn create_key(path: &Path) -> Result<()> {
    let mut bytes = crypto::random::<32>()?;
    let result = write_new(path, &bytes);
    crypto::wipe(&mut bytes);
    result
}
impl From<io::ErrorKind> for Error {
    fn from(_: io::ErrorKind) -> Self {
        Error::new(5, "journal I/O failure")
    }
}
