use crate::{crypto, ensure, ffi, now, Error, Result};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    net::SocketAddr,
    path::{Path, PathBuf},
    ptr::NonNull,
};

pub struct Identity {
    material: [u8; 124],
}
impl Drop for Identity {
    fn drop(&mut self) {
        crypto::wipe(&mut self.material)
    }
}
impl Identity {
    pub fn generate(path: &Path) -> Result<Self> {
        let mut m = [0; 124];
        m[..4].copy_from_slice(b"DGI1");
        m[4..12].copy_from_slice(&1u64.to_be_bytes());
        m[12..20].copy_from_slice(&now().saturating_sub(60).to_be_bytes());
        m[20..28].copy_from_slice(&(now() + 30 * 86400).to_be_bytes());
        m[28..92].copy_from_slice(&crypto::random::<64>()?);
        let digest = crypto::hash(&m[..92])?;
        m[92..].copy_from_slice(&digest);
        write_new(path, &m)?;
        Ok(Self { material: m })
    }
    pub fn load(path: &Path) -> Result<Self> {
        let secret = crypto::Bytes(read_bounded(path, 124)?);
        let m = &secret.0;
        ensure(
            m.len() == 124 && &m[..4] == b"DGI1",
            4,
            "invalid identity file",
        )?;
        ensure(
            crypto::hash(&m[..92])? == m[92..],
            12,
            "identity file integrity mismatch",
        )?;
        let mut material = [0; 124];
        material.copy_from_slice(&m);
        let result = Self { material };
        ensure(
            result.epoch() > 0 && result.first() <= now() && now() < result.expires(),
            14,
            "identity key expired or not yet valid",
        )?;
        Ok(result)
    }
    pub fn epoch(&self) -> u64 {
        u64::from_be_bytes(self.material[4..12].try_into().unwrap())
    }
    pub fn first(&self) -> u64 {
        u64::from_be_bytes(self.material[12..20].try_into().unwrap())
    }
    pub fn expires(&self) -> u64 {
        u64::from_be_bytes(self.material[20..28].try_into().unwrap())
    }
    pub fn signer(&self) -> Result<Signer> {
        let p = unsafe {
            ffi::dgn_signer(
                self.material[28..60].as_ptr(),
                self.material[60..92].as_ptr(),
                self.epoch(),
                self.first(),
                self.expires(),
            )
        }
        .checked()?;
        Ok(Signer(NonNull::new(p).unwrap()))
    }
    pub fn public(&self) -> Result<Record> {
        self.signer()?
            .record("127.0.0.1:1".parse().unwrap(), 2, 60)
            .and_then(|v| Record::parse(&v))
    }
    pub fn rotate(path: &Path) -> Result<Self> {
        let mut identity = Self::load(path)?;
        let epoch = identity
            .epoch()
            .checked_add(1)
            .ok_or(Error::new(7, "key epoch exhausted"))?;
        identity.material[4..12].copy_from_slice(&epoch.to_be_bytes());
        identity.material[12..20].copy_from_slice(&now().saturating_sub(60).to_be_bytes());
        identity.material[20..28].copy_from_slice(&(now() + 30 * 86400).to_be_bytes());
        identity.material[60..92].copy_from_slice(&crypto::random::<32>()?);
        let h = crypto::hash(&identity.material[..92])?;
        identity.material[92..].copy_from_slice(&h);
        replace(path, &identity.material)?;
        Ok(identity)
    }
}
pub struct Signer(NonNull<std::ffi::c_void>);
unsafe impl Send for Signer {}
impl Drop for Signer {
    fn drop(&mut self) {
        unsafe { ffi::dgn_signer_close(self.0.as_ptr()) }
    }
}
impl Signer {
    pub fn records(&mut self, entries: &[(SocketAddr, u32)], ttl: u32) -> Result<Vec<u8>> {
        ensure(
            !entries.is_empty() && entries.len() <= 8,
            4,
            "invalid locator count",
        )?;
        let entries: Vec<_> = entries
            .iter()
            .map(|(a, p)| ffi::Locator {
                address: (*a).into(),
                profiles: *p,
                priority: 0,
            })
            .collect();
        let mut bytes = vec![0; 572];
        let n = unsafe {
            ffi::dgn_records(
                self.0.as_ptr(),
                entries.as_ptr(),
                entries.len(),
                ttl,
                bytes.as_mut_ptr(),
                bytes.len(),
            )
        }
        .checked()?;
        bytes.truncate(n);
        Ok(bytes)
    }
    pub fn resume(&mut self, previous: &[u8]) -> Result<()> {
        unsafe { ffi::dgn_record_resume(self.0.as_ptr(), previous.as_ptr(), previous.len()) }
            .checked()?;
        Ok(())
    }

    pub fn sign(&self, data: &[u8]) -> Result<[u8; 64]> {
        ensure(data.len() <= 65536, 7, "signing input limit")?;
        let mut signature = [0; 64];
        unsafe {
            ffi::dgn_sign(
                self.0.as_ptr(),
                data.as_ptr(),
                data.len(),
                signature.as_mut_ptr(),
            )
        }
        .checked()?;
        Ok(signature)
    }
    pub fn record(&mut self, address: SocketAddr, profiles: u32, ttl: u32) -> Result<Vec<u8>> {
        let mut bytes = vec![0; 572];
        let n = unsafe {
            ffi::dgn_record(
                self.0.as_ptr(),
                address.into(),
                profiles,
                ttl,
                bytes.as_mut_ptr(),
                bytes.len(),
            )
        }
        .checked()?;
        bytes.truncate(n);
        Ok(bytes)
    }
}
#[derive(Clone, Debug)]
pub struct Record {
    pub bytes: Vec<u8>,
    pub id: [u8; 32],
    pub root: [u8; 32],
    pub key: [u8; 32],
    pub epoch: u64,
    pub expires: u64,
    pub key_expires: u64,
    pub locators: Vec<(SocketAddr, u32)>,
}
impl Record {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        Self::parse_checked(bytes, false)
    }
    pub fn historical(bytes: &[u8]) -> Result<Self> {
        Self::parse_checked(bytes, true)
    }
    fn parse_checked(bytes: &[u8], historical: bool) -> Result<Self> {
        ensure(
            (284..=572).contains(&bytes.len()),
            11,
            "locator length limit",
        )?;
        let status = unsafe {
            if historical {
                ffi::dgn_record_verify_historical(bytes.as_ptr(), bytes.len())
            } else {
                ffi::dgn_record_verify(bytes.as_ptr(), bytes.len())
            }
        };
        ensure(status == 0, status, "signed locator validation failed")?;
        let n = u32::from_be_bytes(bytes[152..156].try_into().unwrap()) as usize;
        let mut locators = Vec::with_capacity(n);
        for i in 0..n {
            let p = 220 + 36 * i;
            let mut address = ffi::Address {
                family: u32::from_be_bytes(bytes[p..p + 4].try_into().unwrap()),
                port: u32::from_be_bytes(bytes[p + 4..p + 8].try_into().unwrap()),
                scope: u32::from_be_bytes(bytes[p + 8..p + 12].try_into().unwrap()),
                bytes: [0; 16],
            };
            address.bytes.copy_from_slice(&bytes[p + 12..p + 28]);
            locators.push((
                address.socket()?,
                u32::from_be_bytes(bytes[p + 28..p + 32].try_into().unwrap()),
            ));
        }
        Ok(Self {
            bytes: bytes.to_vec(),
            id: bytes[8..40].try_into().unwrap(),
            root: bytes[40..72].try_into().unwrap(),
            key: bytes[72..104].try_into().unwrap(),
            epoch: u64::from_be_bytes(bytes[104..112].try_into().unwrap()),
            key_expires: u64::from_be_bytes(bytes[128..136].try_into().unwrap()),
            expires: u64::from_be_bytes(bytes[144..152].try_into().unwrap()),
            locators,
        })
    }
    pub fn pinned(&self, root: &[u8; 32], minimum_epoch: u64) -> Result<()> {
        ensure(
            &self.root == root && self.epoch >= minimum_epoch,
            12,
            "peer identity or key epoch is not authorised",
        )
    }
}
pub fn hex(bytes: &[u8]) -> String {
    const H: &[u8] = b"0123456789abcdef";
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push(H[(b >> 4) as usize] as char);
        s.push(H[(b & 15) as usize] as char);
    }
    s
}
pub fn unhex<const N: usize>(text: &str) -> Result<[u8; N]> {
    ensure(
        text.len() == N * 2 && text.is_ascii(),
        4,
        "invalid hexadecimal size",
    )?;
    let mut out = [0; N];
    for (i, item) in out.iter_mut().enumerate() {
        *item = u8::from_str_radix(&text[i * 2..i * 2 + 2], 16)
            .map_err(|_| Error::new(4, "invalid hexadecimal value"))?;
    }
    Ok(out)
}
pub fn read_bounded(path: &Path, maximum: usize) -> Result<Vec<u8>> {
    let mut data = Vec::new();
    File::open(path)?
        .take(maximum as u64 + 1)
        .read_to_end(&mut data)?;
    ensure(data.len() <= maximum, 7, "file size limit")?;
    Ok(data)
}
pub fn write_new(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)?;
    if let Err(e) = file.write_all(bytes).and_then(|_| file.sync_all()) {
        drop(file);
        let _ = fs::remove_file(path);
        return Err(e.into());
    }
    sync_parent(path)
}
pub fn sync_parent(path: &Path) -> Result<()> {
    #[cfg(unix)]
    {
        File::open(
            path.parent()
                .filter(|p| !p.as_os_str().is_empty())
                .unwrap_or(Path::new(".")),
        )?
        .sync_all()?;
    }
    #[cfg(not(unix))]
    let _ = path;
    Ok(())
}
pub fn replace(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut temp = PathBuf::from(path);
    temp.set_extension(format!("tmp-{}", hex(&crypto::random::<8>()?)));
    write_new(&temp, bytes)?;
    if let Err(e) = fs::rename(&temp, path) {
        let _ = fs::remove_file(temp);
        return Err(e.into());
    }
    sync_parent(path)
}

pub(crate) fn trust_cache(path: &Path, root: &[u8; 32], candidate: Option<&Record>) -> Result<u64> {
    let mut lock_name = path.as_os_str().to_os_string();
    lock_name.push(".lock");
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true).truncate(false);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let lock = options.open(PathBuf::from(lock_name))?;
    lock.try_lock()
        .map_err(|_| Error::new(7, "trust cache is in use"))?;
    let previous = if path.exists() {
        let record = Record::historical(&read_bounded(path, 572)?)?;
        record.pinned(root, 1)?;
        record.epoch
    } else {
        0
    };
    if let Some(candidate) = candidate {
        candidate.pinned(root, previous.max(1))?;
        if candidate.epoch > previous {
            replace(path, &candidate.bytes)?;
        }
        Ok(candidate.epoch.max(previous))
    } else {
        Ok(previous)
    }
}
