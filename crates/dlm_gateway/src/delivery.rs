use crate::{
    crypto, ensure, fec,
    journal::{Journal, State},
    now,
    protocol::Decoder,
    Error, Result,
};
use std::{
    collections::BTreeMap,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
pub const BLOCK: usize = 8192;
pub const SHARD: usize = 512;
pub const MAX_TRANSFER: usize = 16 * 1024 * 1024;
pub const MAX_GROUPS: usize = 64;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Descriptor {
    pub id: [u8; 16],
    pub stream: u64,
    pub length: u64,
    pub expires: u64,
    pub digest: [u8; 32],
}
impl Descriptor {
    pub fn new(id: [u8; 16], stream: u64, expires: u64, bytes: &[u8]) -> Result<Self> {
        let value = Self {
            id,
            stream,
            length: bytes.len() as u64,
            expires,
            digest: crypto::hash(bytes)?,
        };
        value.validate()?;
        Ok(value)
    }
    pub fn validate(&self) -> Result<()> {
        let current = now();
        ensure(
            self.id != [0; 16]
                && self.stream > 0
                && self.length <= MAX_TRANSFER as u64
                && self.expires > current
                && self.expires - current <= 86400,
            4,
            "invalid delivery descriptor/TTL",
        )
    }
    pub fn encode(&self) -> Vec<u8> {
        let mut out = b"DLD1".to_vec();
        out.extend_from_slice(&self.id);
        out.extend_from_slice(&self.stream.to_be_bytes());
        out.extend_from_slice(&self.length.to_be_bytes());
        out.extend_from_slice(&self.expires.to_be_bytes());
        out.extend_from_slice(&(BLOCK as u32).to_be_bytes());
        out.extend_from_slice(&self.digest);
        out
    }
    pub fn decode(bytes: &[u8]) -> Result<Self> {
        let mut d = Decoder::new(bytes);
        ensure(
            d.take(4)? == b"DLD1",
            11,
            "invalid delivery descriptor version",
        )?;
        let id = d.take(16)?.try_into().unwrap();
        let stream = d.u64()?;
        let length = d.u64()?;
        let expires = d.u64()?;
        ensure(
            d.u32()? == BLOCK as u32,
            6,
            "unsupported delivery block size",
        )?;
        let digest = d.take(32)?.try_into().unwrap();
        d.end()?;
        let value = Self {
            id,
            stream,
            length,
            expires,
            digest,
        };
        value.validate()?;
        Ok(value)
    }
    pub fn blocks(&self) -> usize {
        (self.length as usize).div_ceil(BLOCK)
    }
    pub fn block_len(&self, index: usize) -> Result<usize> {
        ensure(
            index < self.blocks(),
            4,
            "delivery block offset outside transfer",
        )?;
        Ok((self.length as usize - index * BLOCK).min(BLOCK))
    }
    fn key(&self, owner: &[u8; 32], index: Option<usize>) -> Result<[u8; 16]> {
        let mut data = if index.is_some() {
            b"DLN-BLOCK1".to_vec()
        } else {
            b"DLN-TRANSFER1".to_vec()
        };
        data.extend_from_slice(owner);
        data.extend_from_slice(&self.id);
        data.extend_from_slice(&self.stream.to_be_bytes());
        if let Some(i) = index {
            data.extend_from_slice(&(i as u64 * BLOCK as u64).to_be_bytes());
        }
        Ok(crypto::hash(&data)?[..16].try_into().unwrap())
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum DeliveryState {
    AcceptedLocal = 1,
    AcceptedRemote = 2,
    Committed = 3,
    Completed = 4,
    Consumed = 5,
}
#[derive(Clone, Debug)]
pub struct Receipt {
    pub state: DeliveryState,
    pub descriptor: Descriptor,
    pub bitmap: Vec<u8>,
    pub credit: u32,
}
impl Receipt {
    pub fn has(&self, index: usize) -> bool {
        index < self.descriptor.blocks()
            && self
                .bitmap
                .get(index / 8)
                .is_some_and(|v| v & (1 << (index % 8)) != 0)
    }
    pub fn encode(&self) -> Vec<u8> {
        let mut v = b"DLA2".to_vec();
        v.extend_from_slice(&(self.state as u32).to_be_bytes());
        v.extend_from_slice(&self.credit.to_be_bytes());
        v.extend_from_slice(&self.descriptor.encode());
        v.extend_from_slice(&(self.bitmap.len() as u32).to_be_bytes());
        v.extend_from_slice(&self.bitmap);
        v
    }
    pub fn decode(bytes: &[u8], expected: &Descriptor) -> Result<Self> {
        let mut d = Decoder::new(bytes);
        ensure(
            d.take(4)? == b"DLA2",
            11,
            "invalid delivery receipt version",
        )?;
        let state = match d.u32()? {
            1 => DeliveryState::AcceptedLocal,
            2 => DeliveryState::AcceptedRemote,
            3 => DeliveryState::Committed,
            4 => DeliveryState::Completed,
            5 => DeliveryState::Consumed,
            _ => return Err(Error::new(11, "invalid delivery receipt state")),
        };
        let credit = d.u32()?;
        let descriptor = Descriptor::decode(d.take(80)?)?;
        ensure(
            &descriptor == expected,
            12,
            "delivery receipt identity/digest mismatch",
        )?;
        let n = d.u32()? as usize;
        ensure(
            n == descriptor.blocks().div_ceil(8) && credit <= MAX_GROUPS as u32,
            11,
            "invalid delivery receipt bitmap/credit",
        )?;
        let bitmap = d.take(n)?.to_vec();
        d.end()?;
        if descriptor.blocks() % 8 != 0 {
            let used = (1u16 << (descriptor.blocks() % 8)) - 1;
            ensure(
                bitmap.last().copied().unwrap_or(0) as u16 & !used == 0,
                11,
                "noncanonical delivery bitmap",
            )?;
        }
        Ok(Self {
            state,
            descriptor,
            bitmap,
            credit,
        })
    }
}
#[derive(Clone, Debug)]
pub struct Packet {
    pub id: [u8; 16],
    pub stream: u64,
    pub offset: u64,
    pub length: usize,
    pub data: u8,
    pub total: u8,
    pub shard: fec::Shard,
    pub digest: [u8; 32],
}
impl Packet {
    pub fn encode(&self) -> Vec<u8> {
        let mut v = b"DLP1".to_vec();
        v.extend_from_slice(&self.id);
        v.extend_from_slice(&self.stream.to_be_bytes());
        v.extend_from_slice(&self.offset.to_be_bytes());
        v.extend_from_slice(&(self.length as u32).to_be_bytes());
        v.extend_from_slice(&[self.data, self.total, self.shard.index, 0]);
        v.extend_from_slice(&self.digest);
        v.extend_from_slice(&self.shard.bytes);
        v
    }
    pub fn decode(bytes: &[u8]) -> Result<Self> {
        let mut d = Decoder::new(bytes);
        ensure(d.take(4)? == b"DLP1", 11, "invalid delivery shard version")?;
        let id = d.take(16)?.try_into().unwrap();
        let stream = d.u64()?;
        let offset = d.u64()?;
        let length = d.u32()? as usize;
        let codes = d.take(4)?;
        let digest = d.take(32)?.try_into().unwrap();
        ensure(
            length > 0
                && length <= BLOCK
                && offset % BLOCK as u64 == 0
                && codes[0] as usize == length.div_ceil(SHARD)
                && codes[1] >= codes[0]
                && codes[1] <= 32
                && codes[2] < codes[1]
                && codes[3] == 0,
            11,
            "invalid delivery shard bounds",
        )?;
        let width = length.div_ceil(codes[0] as usize).max(1);
        let shard = fec::Shard {
            index: codes[2],
            bytes: d.take(width)?.to_vec(),
        };
        d.end()?;
        Ok(Self {
            id,
            stream,
            offset,
            length,
            data: codes[0],
            total: codes[1],
            shard,
            digest,
        })
    }
}
#[derive(Clone, Debug)]
pub struct BlockAck {
    pub id: [u8; 16],
    pub stream: u64,
    pub offset: u64,
    pub state: DeliveryState,
    pub received: u32,
    pub credit: u32,
    pub status: i32,
    pub digest: [u8; 32],
}
impl BlockAck {
    pub fn encode(&self) -> Vec<u8> {
        let mut v = b"DBA1".to_vec();
        v.extend_from_slice(&self.id);
        v.extend_from_slice(&self.stream.to_be_bytes());
        v.extend_from_slice(&self.offset.to_be_bytes());
        v.extend_from_slice(&(self.state as u32).to_be_bytes());
        v.extend_from_slice(&self.received.to_be_bytes());
        v.extend_from_slice(&self.credit.to_be_bytes());
        v.extend_from_slice(&self.status.to_be_bytes());
        v.extend_from_slice(&self.digest);
        v
    }
    pub fn decode(bytes: &[u8]) -> Result<Self> {
        let mut d = Decoder::new(bytes);
        ensure(d.take(4)? == b"DBA1", 11, "invalid block ACK version")?;
        let id = d.take(16)?.try_into().unwrap();
        let stream = d.u64()?;
        let offset = d.u64()?;
        let state = match d.u32()? {
            2 => DeliveryState::AcceptedRemote,
            3 => DeliveryState::Committed,
            _ => return Err(Error::new(11, "invalid block ACK state")),
        };
        let received = d.u32()?;
        let credit = d.u32()?;
        let status = d.u32()? as i32;
        let digest = d.take(32)?.try_into().unwrap();
        d.end()?;
        ensure(credit <= MAX_GROUPS as u32, 11, "invalid block ACK credit")?;
        Ok(Self {
            id,
            stream,
            offset,
            state,
            received,
            credit,
            status,
            digest,
        })
    }
}
struct Assembly {
    packet: Packet,
    shards: Vec<fec::Shard>,
    mask: u32,
    updated: Instant,
}
pub struct Store {
    journal: Arc<Mutex<Journal>>,
    pending: BTreeMap<([u8; 32], [u8; 16], u64, u64), Assembly>,
    per_client: usize,
}
impl Store {
    pub fn new(journal: Arc<Mutex<Journal>>, per_client: usize) -> Self {
        Self {
            journal,
            pending: BTreeMap::new(),
            per_client: per_client.clamp(1, 16),
        }
    }
    fn credit(&self, owner: &[u8; 32]) -> usize {
        (MAX_GROUPS - self.pending.len()).min(
            self.per_client.saturating_sub(
                self.pending
                    .keys()
                    .filter(|(root, _, _, _)| root == owner)
                    .count(),
            ),
        )
    }
    pub fn begin(&mut self, owner: &[u8; 32], desc: &Descriptor, create: bool) -> Result<Receipt> {
        desc.validate()?;
        let mut j = self
            .journal
            .lock()
            .map_err(|_| Error::new(5, "delivery journal lock failed"))?;
        j.prune()?;
        self.pending
            .retain(|_, e| e.updated.elapsed() < Duration::from_secs(5));
        let key = desc.key(owner, None)?;
        if create {
            if j.get(&key).is_none() {
                let (count, bytes) = j.owner_usage(owner);
                ensure(
                    count < 2049 && bytes < 20 * 1024 * 1024,
                    7,
                    "client durable storage quota exhausted",
                )?;
            }
            j.put(key, *owner, desc.expires, &desc.encode(), State::Pending)?;
        }
        let mut value = receipt(&j, owner, desc)?;
        value.credit = self.credit(owner) as u32;
        Ok(value)
    }
    pub fn accept(&mut self, owner: &[u8; 32], bytes: &[u8]) -> Result<BlockAck> {
        let p = Packet::decode(bytes)?;
        let mut ack = BlockAck {
            id: p.id,
            stream: p.stream,
            offset: p.offset,
            state: DeliveryState::AcceptedRemote,
            received: 0,
            credit: 0,
            status: 0,
            digest: p.digest,
        };
        match self.accept_packet(owner, p) {
            Ok((committed, mask, credit)) => {
                ack.state = if committed {
                    DeliveryState::Committed
                } else {
                    DeliveryState::AcceptedRemote
                };
                ack.received = mask;
                ack.credit = credit as u32
            }
            Err(e) => ack.status = e.code,
        }
        Ok(ack)
    }
    fn accept_packet(&mut self, owner: &[u8; 32], p: Packet) -> Result<(bool, u32, usize)> {
        self.pending
            .retain(|_, e| e.updated.elapsed() < Duration::from_secs(5));
        let probe = Descriptor {
            id: p.id,
            stream: p.stream,
            length: 0,
            expires: 0,
            digest: [0; 32],
        };
        let mut j = self
            .journal
            .lock()
            .map_err(|_| Error::new(5, "delivery journal lock failed"))?;
        let key = probe.key(owner, None)?;
        let meta = j
            .get(&key)
            .ok_or(Error::new(2, "delivery manifest is absent"))?;
        ensure(
            meta.owner == *owner && meta.state != State::Acknowledged,
            16,
            "delivery manifest owner/consumed state denied",
        )?;
        let desc = Descriptor::decode(&meta.payload)?;
        let index = (p.offset / BLOCK as u64) as usize;
        ensure(
            desc.block_len(index)? == p.length,
            11,
            "delivery shard offset/length mismatch",
        )?;
        let blockkey = desc.key(owner, Some(index))?;
        if let Some(block) = j.get(&blockkey) {
            ensure(
                block.owner == *owner
                    && block.expires == desc.expires
                    && block.state == State::Committed,
                18,
                "delivery block state conflict",
            )?;
            ensure(
                crypto::hash(decode_block(&desc, index, &block.payload)?)? == p.digest,
                18,
                "delivery block content conflict",
            )?;
            return Ok((true, 0, self.credit(owner)));
        }
        let mapkey = (*owner, p.id, p.stream, p.offset);
        if !self.pending.contains_key(&mapkey) {
            ensure(
                self.credit(owner) > 0,
                7,
                "delivery receive credit exhausted",
            )?;
            self.pending.insert(
                mapkey,
                Assembly {
                    packet: p.clone(),
                    shards: Vec::new(),
                    mask: 0,
                    updated: Instant::now(),
                },
            );
        }
        let a = self.pending.get_mut(&mapkey).unwrap();
        ensure(
            a.packet.digest == p.digest
                && a.packet.data == p.data
                && a.packet.total == p.total
                && a.packet.length == p.length,
            18,
            "conflicting FEC group metadata",
        )?;
        if a.mask & (1u32 << p.shard.index) == 0 {
            a.mask |= 1u32 << p.shard.index;
            a.shards.push(p.shard);
        }
        a.updated = Instant::now();
        let mask = a.mask;
        if a.shards.len() < p.data as usize {
            return Ok((false, mask, self.credit(owner)));
        }
        let payload = fec::decode(&a.shards, p.data, p.total, p.length, &p.digest)?;
        let mut block = b"DBK1".to_vec();
        block.extend_from_slice(&desc.id);
        block.extend_from_slice(&desc.stream.to_be_bytes());
        block.extend_from_slice(&p.offset.to_be_bytes());
        block.extend_from_slice(&p.digest);
        block.extend_from_slice(&payload);
        let (count, bytes) = j.owner_usage(owner);
        ensure(
            count < 2049 && bytes.saturating_add(157 + block.len()) <= 20 * 1024 * 1024,
            7,
            "client durable block storage quota exhausted",
        )?;
        j.put(blockkey, *owner, desc.expires, &block, State::Committed)?;
        self.pending.remove(&mapkey);
        Ok((true, mask, self.credit(owner)))
    }
    pub fn complete(&mut self, owner: &[u8; 32], desc: &Descriptor) -> Result<Receipt> {
        let mut j = self
            .journal
            .lock()
            .map_err(|_| Error::new(5, "delivery journal lock failed"))?;
        let r = receipt(&j, owner, desc)?;
        if matches!(r.state, DeliveryState::Completed | DeliveryState::Consumed) {
            return Ok(r);
        }
        ensure(
            (0..desc.blocks()).all(|i| r.has(i)),
            1,
            "delivery still has uncommitted blocks",
        )?;
        ensure(
            crypto::hash(&assemble(&j, owner, desc)?)? == desc.digest,
            12,
            "completed delivery digest mismatch",
        )?;
        j.mark(desc.key(owner, None)?, State::Committed)?;
        receipt(&j, owner, desc)
    }
    pub fn read(
        &self,
        owner: &[u8; 32],
        desc: &Descriptor,
        index: usize,
        offset: usize,
        maximum: usize,
    ) -> Result<Vec<u8>> {
        let j = self
            .journal
            .lock()
            .map_err(|_| Error::new(5, "delivery journal lock failed"))?;
        ensure(
            receipt(&j, owner, desc)?.state == DeliveryState::Completed,
            18,
            "delivery is not complete or was consumed",
        )?;
        let block = j
            .get(&desc.key(owner, Some(index))?)
            .ok_or(Error::new(2, "delivery block absent"))?;
        let bytes = decode_block(desc, index, &block.payload)?;
        ensure(
            maximum > 0 && maximum <= 32768 && offset <= bytes.len(),
            4,
            "delivery read range invalid",
        )?;
        Ok(bytes[offset..bytes.len().min(offset + maximum)].to_vec())
    }
    pub fn consume(&mut self, owner: &[u8; 32], desc: &Descriptor) -> Result<Receipt> {
        let mut j = self
            .journal
            .lock()
            .map_err(|_| Error::new(5, "delivery journal lock failed"))?;
        ensure(
            matches!(
                receipt(&j, owner, desc)?.state,
                DeliveryState::Completed | DeliveryState::Consumed
            ),
            18,
            "cannot consume an incomplete delivery",
        )?;
        j.mark(desc.key(owner, None)?, State::Acknowledged)?;
        for i in 0..desc.blocks() {
            if j.get(&desc.key(owner, Some(i))?).is_some() {
                j.mark(desc.key(owner, Some(i))?, State::Acknowledged)?;
            }
        }
        receipt(&j, owner, desc)
    }
    pub fn pending_groups(&self) -> usize {
        self.pending.len()
    }
}
fn receipt(j: &Journal, owner: &[u8; 32], desc: &Descriptor) -> Result<Receipt> {
    desc.validate()?;
    let meta = j
        .get(&desc.key(owner, None)?)
        .ok_or(Error::new(2, "delivery manifest is absent"))?;
    ensure(
        meta.owner == *owner && meta.expires == desc.expires,
        16,
        "delivery manifest owner denied",
    )?;
    ensure(
        meta.digest
            == crate::journal::content_digest(
                &desc.key(owner, None)?,
                owner,
                desc.expires,
                &desc.encode(),
            )?,
        18,
        "delivery descriptor conflicts with committed manifest",
    )?;
    let state = match meta.state {
        State::Pending => DeliveryState::AcceptedRemote,
        State::Committed => DeliveryState::Completed,
        State::Acknowledged => DeliveryState::Consumed,
    };
    let mut bitmap = vec![0; desc.blocks().div_ceil(8)];
    for i in 0..desc.blocks() {
        if state == DeliveryState::Consumed
            || j.get(&desc.key(owner, Some(i))?).is_some_and(|e| {
                e.owner == *owner && e.state == State::Committed && e.expires == desc.expires
            })
        {
            bitmap[i / 8] |= 1 << (i % 8);
        }
    }
    Ok(Receipt {
        state,
        descriptor: desc.clone(),
        bitmap,
        credit: MAX_GROUPS as u32,
    })
}
fn decode_block<'a>(desc: &Descriptor, index: usize, bytes: &'a [u8]) -> Result<&'a [u8]> {
    ensure(
        bytes.len() >= 68
            && &bytes[..4] == b"DBK1"
            && bytes[4..20] == desc.id
            && bytes[20..28] == desc.stream.to_be_bytes()
            && bytes[28..36] == (index as u64 * BLOCK as u64).to_be_bytes()
            && bytes.len() == 68 + desc.block_len(index)?,
        11,
        "corrupt delivery block metadata",
    )?;
    ensure(
        crypto::hash(&bytes[68..])? == bytes[36..68],
        12,
        "corrupt delivery block digest",
    )?;
    Ok(&bytes[68..])
}
fn assemble(j: &Journal, owner: &[u8; 32], desc: &Descriptor) -> Result<Vec<u8>> {
    let mut out = Vec::with_capacity(desc.length as usize);
    for i in 0..desc.blocks() {
        let block = j
            .get(&desc.key(owner, Some(i))?)
            .ok_or(Error::new(2, "delivery block absent"))?;
        ensure(
            block.owner == *owner && block.state == State::Committed,
            18,
            "delivery block is not committed",
        )?;
        out.extend_from_slice(decode_block(desc, i, &block.payload)?);
    }
    Ok(out)
}
pub fn enqueue(
    j: &mut Journal,
    peer: [u8; 32],
    desc: &Descriptor,
    bytes: &[u8],
) -> Result<Receipt> {
    desc.validate()?;
    ensure(
        bytes.len() == desc.length as usize && crypto::hash(bytes)? == desc.digest,
        12,
        "local delivery digest mismatch",
    )?;
    for (i, part) in bytes.chunks(BLOCK).enumerate() {
        let mut block = b"DBK1".to_vec();
        block.extend_from_slice(&desc.id);
        block.extend_from_slice(&desc.stream.to_be_bytes());
        block.extend_from_slice(&(i as u64 * BLOCK as u64).to_be_bytes());
        block.extend_from_slice(&crypto::hash(part)?);
        block.extend_from_slice(part);
        j.put(
            desc.key(&peer, Some(i))?,
            peer,
            desc.expires,
            &block,
            State::Committed,
        )?;
    }
    j.put(
        desc.key(&peer, None)?,
        peer,
        desc.expires,
        &desc.encode(),
        State::Pending,
    )?;
    let mut value = receipt(j, &peer, desc)?;
    value.state = DeliveryState::AcceptedLocal;
    Ok(value)
}
pub fn queued(j: &Journal) -> Result<Vec<([u8; 32], Descriptor)>> {
    j.pending()
        .iter()
        .filter(|e| e.payload.starts_with(b"DLD1"))
        .map(|e| Ok((e.owner, Descriptor::decode(&e.payload)?)))
        .collect()
}
pub fn queued_bytes(j: &Journal, peer: &[u8; 32], desc: &Descriptor) -> Result<Vec<u8>> {
    assemble(j, peer, desc)
}
pub fn acknowledged(j: &mut Journal, peer: &[u8; 32], desc: &Descriptor) -> Result<()> {
    j.mark(desc.key(peer, None)?, State::Acknowledged)?;
    for i in 0..desc.blocks() {
        j.mark(desc.key(peer, Some(i))?, State::Acknowledged)?;
    }
    Ok(())
}
