use crate::{
    crypto,
    delivery::{self, Descriptor, Store},
    ensure,
    gateway::{self, Counters, Destination, Outcome},
    identity::{read_bounded, replace, Identity},
    journal::{self, Journal},
    now,
    policy::{Limits, Policy},
    protocol::Decoder,
    Error, Result,
};
use std::{
    collections::BTreeMap,
    net::SocketAddr,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
struct Logical {
    ticket: [u8; 32],
    next: u64,
    last: Option<(u64, [u8; 32], Vec<u8>)>,
    targets: BTreeMap<u32, Destination>,
    stream: u32,
    written: BTreeMap<u32, u64>,
    read: BTreeMap<u32, u64>,
    updated: Instant,
}
type LogicalMap = BTreeMap<([u8; 32], [u8; 16]), Arc<Mutex<Logical>>>;
pub(crate) struct Context {
    pub journal: Option<Arc<Mutex<Journal>>>,
    pub delivery: Option<Mutex<Store>>,
    pub instance: [u8; 32],
    pub storage: [u8; 32],
    pub root: [u8; 32],
    identity: PathBuf,
    addresses: Mutex<Vec<(SocketAddr, u32)>>,
    logical: Mutex<LogicalMap>,
    limits: Limits,
}
impl Context {
    pub fn new(
        identity: &Path,
        spool: Option<&(PathBuf, PathBuf)>,
        limits: &Limits,
    ) -> Result<Self> {
        let root = Identity::load(identity)?.public()?.root;
        let instance = crypto::random()?;
        let (journal, delivery_journal, storage) = if let Some((path, key)) = spool {
            let key = journal::load_key(key)?;
            let journal = Arc::new(Mutex::new(Journal::open(path, key, 67108864, 4096)?));
            let mut delivery = Journal::open(&path.join("delivery-v2"), key, 67108864, 4096)?;
            let mut binding = crypto::Bytes(b"DLN-STORAGE1".to_vec());
            binding.0.extend_from_slice(&root);
            binding.0.extend_from_slice(&key);
            binding.0.extend_from_slice(&delivery.storage_id()?);
            let storage = crypto::hash(&binding.0)?;
            (
                Some(journal),
                Some(Arc::new(Mutex::new(delivery))),
                storage,
            )
        } else {
            (None, None, instance)
        };
        let delivery = delivery_journal
            .as_ref()
            .map(|j| Mutex::new(Store::new(j.clone(), limits.pending)));
        Ok(Self {
            journal,
            delivery,
            instance,
            storage,
            root,
            identity: identity.to_path_buf(),
            addresses: Mutex::new(Vec::new()),
            logical: Mutex::new(BTreeMap::new()),
            limits: limits.clone(),
        })
    }
    pub fn prune(&self) {
        if let Ok(mut map) = self.logical.try_lock() {
            let ttl = Duration::from_millis(self.limits.idle_ms.max(30000) as u64);
            map.retain(|_, value| match value.try_lock() {
                Ok(state) => state.updated.elapsed() < ttl,
                Err(_) => true,
            });
        }
    }
    pub fn address(&self, address: SocketAddr, profile: u32) -> Result<()> {
        let mut entries = self
            .addresses
            .lock()
            .map_err(|_| Error::new(5, "gateway locator lock failed"))?;
        if !entries.contains(&(address, 1 << (profile - 1))) {
            ensure(entries.len() < 8, 7, "gateway locator capacity")?;
            entries.push((address, 1 << (profile - 1)));
        }
        Ok(())
    }
    fn record(&self) -> Result<Vec<u8>> {
        let mut signer = Identity::load(&self.identity)?.signer()?;
        let entries = self
            .addresses
            .lock()
            .map_err(|_| Error::new(5, "gateway locator lock failed"))?;
        signer.records(&entries, 300)
    }
    pub fn publish(&self, path: &Path) -> Result<Vec<u8>> {
        let mut signer = Identity::load(&self.identity)?.signer()?;
        if path.exists() {
            let previous = read_bounded(path, 572)?;
            if previous.len() >= 320 && previous[40..72] == self.root {
                let epoch = u64::from_be_bytes(previous[104..112].try_into().unwrap());
                if epoch == Identity::load(&self.identity)?.epoch() {
                    signer.resume(&previous)?;
                }
            }
        }
        let entries = self
            .addresses
            .lock()
            .map_err(|_| Error::new(5, "gateway locator lock failed"))?;
        let bytes = signer.records(&entries, 300)?;
        replace(path, &bytes)?;
        Ok(bytes)
    }
    pub fn shard(&self, owner: &[u8; 32], bytes: &[u8]) -> Result<Vec<u8>> {
        let delivery = self
            .delivery
            .as_ref()
            .ok_or(Error::new(6, "durable gateway storage is not configured"))?;
        Ok(delivery
            .lock()
            .map_err(|_| Error::new(5, "delivery receive lock failed"))?
            .accept(owner, bytes)?
            .encode())
    }
    pub fn rpc(
        &self,
        owner: &[u8; 32],
        session: &[u8; 32],
        operation: u32,
        bytes: &[u8],
        maximum: usize,
        policy: &Policy,
        statistics: &Arc<Counters>,
    ) -> Result<(Outcome, u64, Vec<u8>)> {
        match operation {
            11 => {
                ensure(bytes.is_empty(), 11, "capabilities request carries data")?;
                let mut out = b"DGC2".to_vec();
                out.extend_from_slice(&1u32.to_be_bytes());
                out.extend_from_slice(&self.storage);
                out.extend_from_slice(&self.instance);
                out.extend_from_slice(
                    &(if self.delivery.is_some() { 15u32 } else { 9u32 }).to_be_bytes(),
                );
                out.extend_from_slice(&(delivery::MAX_TRANSFER as u32).to_be_bytes());
                out.extend_from_slice(&(delivery::BLOCK as u32).to_be_bytes());
                out.extend_from_slice(&(delivery::SHARD as u32).to_be_bytes());
                out.extend_from_slice(&crate::protocol::descriptor_hash()?);
                Ok((Outcome::Acknowledged, 1, out))
            }
            12 => {
                ensure(bytes.len() == 32, 11, "probe nonce size invalid")?;
                let record = self.record()?;
                let mut out = b"DGP1".to_vec();
                out.extend_from_slice(bytes);
                out.extend_from_slice(session);
                out.extend_from_slice(&now().to_be_bytes());
                out.extend_from_slice(&self.storage);
                out.extend_from_slice(&self.instance);
                out.extend_from_slice(&(record.len() as u32).to_be_bytes());
                out.extend_from_slice(&record);
                let mut signed = b"DLN-PATH-PROBE1".to_vec();
                signed.extend_from_slice(owner);
                signed.extend_from_slice(&out);
                let identity = Identity::load(&self.identity)?;
                ensure(
                    identity.public()?.root == self.root,
                    12,
                    "gateway root changed while running",
                )?;
                out.extend_from_slice(&identity.signer()?.sign(&signed)?);
                Ok((Outcome::Acknowledged, 0, out))
            }
            20 | 21 | 23 | 24 => {
                let desc = Descriptor::decode(bytes)?;
                let store = self
                    .delivery
                    .as_ref()
                    .ok_or(Error::new(6, "durable gateway storage is not configured"))?;
                let mut store = store
                    .lock()
                    .map_err(|_| Error::new(5, "delivery store lock failed"))?;
                let receipt = match operation {
                    20 => store.begin(owner, &desc, true)?,
                    21 => store.begin(owner, &desc, false)?,
                    23 => store.complete(owner, &desc)?,
                    _ => store.consume(owner, &desc)?,
                };
                Ok((
                    Outcome::Acknowledged,
                    receipt.state as u64,
                    receipt.encode(),
                ))
            }
            22 => {
                let mut d = Decoder::new(bytes);
                let desc = Descriptor::decode(d.take(80)?)?;
                let index = d.u32()? as usize;
                let offset = d.u32()? as usize;
                let count = d.u32()? as usize;
                d.end()?;
                ensure(
                    count <= maximum,
                    7,
                    "delivery read exceeds carrier capacity",
                )?;
                let store = self
                    .delivery
                    .as_ref()
                    .ok_or(Error::new(6, "durable gateway storage is not configured"))?;
                let data = store
                    .lock()
                    .map_err(|_| Error::new(5, "delivery store lock failed"))?
                    .read(owner, &desc, index, offset, count)?;
                Ok((Outcome::EndpointData, data.len() as u64, data))
            }
            30 => self.attach(owner, bytes),
            31 => self.logical_call(owner, bytes, maximum, policy, statistics),
            32 => {
                let mut d = Decoder::new(bytes);
                let id = d.take(16)?.try_into().unwrap();
                let ticket = d.take(32)?;
                d.end()?;
                let mut map = self
                    .logical
                    .lock()
                    .map_err(|_| Error::new(5, "logical session map lock failed"))?;
                if let Some(value) = map.get(&(*owner, id)) {
                    ensure(
                        value
                            .lock()
                            .map_err(|_| Error::new(5, "logical session lock failed"))?
                            .ticket
                            == ticket,
                        16,
                        "logical session close ticket denied",
                    )?;
                    map.remove(&(*owner, id));
                }
                Ok((Outcome::Acknowledged, 0, Vec::new()))
            }
            _ => Err(Error::new(6, "gateway extension unsupported")),
        }
    }
    fn attach(&self, owner: &[u8; 32], bytes: &[u8]) -> Result<(Outcome, u64, Vec<u8>)> {
        let mut d = Decoder::new(bytes);
        let id: [u8; 16] = d.take(16)?.try_into().unwrap();
        let ticket: [u8; 32] = d.take(32)?.try_into().unwrap();
        let instance = d.take(32)?;
        d.end()?;
        ensure(id != [0; 16], 4, "empty logical session identity")?;
        let mut map = self
            .logical
            .lock()
            .map_err(|_| Error::new(5, "logical session map lock failed"))?;
        let ttl = Duration::from_millis(self.limits.idle_ms.max(30000) as u64);
        map.retain(|_, s| match s.try_lock() {
            Ok(state) => state.updated.elapsed() < ttl,
            Err(_) => true,
        });
        if !map.contains_key(&(*owner, id)) {
            ensure(
                ticket == [0; 32] && instance == [0; 32],
                19,
                "logical session lost after gateway restart or expiry",
            )?;
            ensure(
                map.len() < self.limits.sessions.min(64)
                    && map.keys().filter(|(root, _)| root == owner).count()
                        < self.limits.per_client,
                7,
                "logical session quota exhausted",
            )?;
            map.insert(
                (*owner, id),
                Arc::new(Mutex::new(Logical {
                    ticket: crypto::random()?,
                    next: 1,
                    last: None,
                    targets: BTreeMap::new(),
                    stream: 1,
                    written: BTreeMap::new(),
                    read: BTreeMap::new(),
                    updated: Instant::now(),
                })),
            );
        }
        let value = map
            .get(&(*owner, id))
            .unwrap()
            .lock()
            .map_err(|_| Error::new(5, "logical session lock failed"))?;
        ensure(
            (ticket == [0; 32] && instance == [0; 32])
                || (ticket == value.ticket && instance == self.instance),
            16,
            "logical session resume ticket/instance denied",
        )?;
        let mut out = self.instance.to_vec();
        out.extend_from_slice(&value.ticket);
        out.extend_from_slice(&value.next.to_be_bytes());
        Ok((Outcome::Acknowledged, value.next, out))
    }
    fn logical_call(
        &self,
        owner: &[u8; 32],
        bytes: &[u8],
        maximum: usize,
        policy: &Policy,
        statistics: &Arc<Counters>,
    ) -> Result<(Outcome, u64, Vec<u8>)> {
        let mut d = Decoder::new(bytes);
        let id = d.take(16)?.try_into().unwrap();
        let ticket = d.take(32)?;
        let instance = d.take(32)?;
        let request = d.u64()?;
        let operation = d.u32()?;
        let body = d.take(bytes.len() - 92)?;
        let hash = crypto::hash(&bytes[80..])?;
        let value = {
            let map = self
                .logical
                .lock()
                .map_err(|_| Error::new(5, "logical session map lock failed"))?;
            map.get(&(*owner, id))
                .cloned()
                .ok_or(Error::new(19, "logical session state was lost"))?
        };
        let mut value = value
            .lock()
            .map_err(|_| Error::new(5, "logical session lock failed"))?;
        ensure(
            value.ticket == ticket && self.instance == instance,
            16,
            "logical session ticket/instance denied",
        )?;
        value.updated = Instant::now();
        if let Some((last, known, result)) = &value.last {
            if *last == request {
                ensure(
                    *known == hash,
                    18,
                    "logical request identifier content conflict",
                )?;
                return Ok((Outcome::Acknowledged, request, result.clone()));
            }
        }
        ensure(
            request == value.next && request < 1048576 && (1..=7).contains(&operation),
            18,
            "nonmonotonic logical operation",
        )?;
        let result = if operation == 3 || operation == 4 {
            let mut body = Decoder::new(body);
            let stream = body.u32()?;
            let offset = body.u64()?;
            let rest = body.take(bytes.len() - 104)?;
            let previous = if operation == 3 {
                value.written.get(&stream)
            } else {
                value.read.get(&stream)
            }
            .copied()
            .unwrap_or(0);
            if offset != previous {
                Err(Error::new(18, "logical stream offset mismatch"))
            } else {
                let mut normalized = stream.to_be_bytes().to_vec();
                normalized.extend_from_slice(rest);
                let Logical {
                    targets,
                    stream: next,
                    ..
                } = &mut *value;
                let result = gateway::execute(
                    operation,
                    &normalized,
                    owner,
                    maximum.saturating_sub(28),
                    policy,
                    targets,
                    next,
                    self.journal.as_ref(),
                    statistics,
                );
                if let Ok((_, count, _)) = &result {
                    let updated = previous
                        .checked_add(*count)
                        .ok_or(Error::new(7, "logical stream offset exhausted"))?;
                    if operation == 3 {
                        value.written.insert(stream, updated);
                    } else {
                        value.read.insert(stream, updated);
                    }
                } else if operation == 3 {
                    value.targets.remove(&stream);
                    value.written.remove(&stream);
                    value.read.remove(&stream);
                }
                result
            }
        } else {
            let Logical {
                targets,
                stream: next,
                ..
            } = &mut *value;
            let result = gateway::execute(
                operation,
                body,
                owner,
                maximum.saturating_sub(28),
                policy,
                targets,
                next,
                self.journal.as_ref(),
                statistics,
            );
            if operation == 6 && result.is_ok() {
                let stream = u32::from_be_bytes(body.try_into().unwrap());
                value.written.remove(&stream);
                value.read.remove(&stream);
            }
            result
        };
        let encoded = gateway::response(request, result)?;
        value.next += 1;
        value.last = Some((request, hash, encoded.clone()));
        Ok((Outcome::Acknowledged, request, encoded))
    }
}
