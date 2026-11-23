use crate::{
    crypto::{self, Secret},
    ensure,
    identity::{Identity, Record},
    now,
    policy::Policy,
    transport::{Channel, Kind},
    Error, Result,
};
use dlm_network_proof::{NetworkFieldIR, NetworkMessageIR, NetworkSpecIR};
use std::time::{Duration, Instant};
pub const MINOR_MAX: u16 = 3;
pub const REQUEST: u32 = 5;
pub const RESPONSE: u32 = 6;
pub const DATA_MAX: usize = 32768;

pub fn specification() -> NetworkSpecIR {
    let mut words = [0; 33];
    words[..16].copy_from_slice(&[
        0x44474e31, 1, 3, 0, 1, 1, 0, 0, 20, 0, 0, 65500, 8, 1, 0, 16777216,
    ]);
    words[18] = 1048576;
    words[20] = 100;
    words[21..33].copy_from_slice(&[5000, 30000, 3600000, 8, 1, 2, 1, 1, 2, 0, 1, 8]);
    let messages = (1..=8)
        .map(|id| NetworkMessageIR {
            words: [
                id,
                if id < 5 { 1 } else { 2 },
                if id % 2 == 1 { 1 } else { 2 },
                0,
                3,
                1,
            ],
            fields: vec![NetworkFieldIR([
                1,
                6,
                1,
                1,
                65000,
                0,
                3,
                if id >= 5 { 1 } else { 0 },
            ])],
        })
        .collect();
    NetworkSpecIR { words, messages }
}
fn legacy_descriptor_hash() -> Result<[u8; 32]> {
    let mut spec = specification();
    for message in &mut spec.messages {
        let id = message.words[0];
        message.words[1] = if id % 2 == 1 { 1 } else { 2 };
        message.words[2] = if id < 5 { 1 } else { 2 };
    }
    crypto::hash(&spec.to_bytes())
}
pub fn descriptor_hash() -> Result<[u8; 32]> {
    let spec = specification();
    spec.validate()
        .map_err(|_| Error::new(11, "invalid built-in gateway NetworkSpec"))?;
    crypto::hash(&spec.to_bytes())
}
pub(crate) fn packet(kind: u32, body: &[u8]) -> Result<Vec<u8>> {
    ensure(
        (1..=8).contains(&kind) && body.len() <= 65000,
        11,
        "invalid gateway message",
    )?;
    let mut out = Vec::with_capacity(body.len() + 8);
    out.extend_from_slice(&kind.to_be_bytes());
    out.extend_from_slice(&(body.len() as u32).to_be_bytes());
    out.extend_from_slice(body);
    Ok(out)
}
pub(crate) fn unpack(wire: &[u8]) -> Result<(u32, &[u8])> {
    ensure(
        wire.len() >= 8 && wire.len() <= 65008,
        11,
        "invalid gateway message size",
    )?;
    let kind = u32::from_be_bytes(wire[..4].try_into().unwrap());
    ensure(
        (1..=8).contains(&kind)
            && u32::from_be_bytes(wire[4..8].try_into().unwrap()) as usize == wire.len() - 8,
        11,
        "noncanonical gateway message",
    )?;
    Ok((kind, &wire[8..]))
}
pub(crate) struct Decoder<'a> {
    bytes: &'a [u8],
    at: usize,
}
impl<'a> Decoder<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, at: 0 }
    }
    pub fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        ensure(
            n <= self.bytes.len().saturating_sub(self.at),
            11,
            "truncated gateway field",
        )?;
        let p = self.at;
        self.at += n;
        Ok(&self.bytes[p..self.at])
    }
    pub fn u16(&mut self) -> Result<u16> {
        Ok(u16::from_be_bytes(self.take(2)?.try_into().unwrap()))
    }
    pub fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_be_bytes(self.take(4)?.try_into().unwrap()))
    }
    pub fn u64(&mut self) -> Result<u64> {
        Ok(u64::from_be_bytes(self.take(8)?.try_into().unwrap()))
    }
    pub fn end(&self) -> Result<()> {
        ensure(self.at == self.bytes.len(), 11, "trailing gateway bytes")
    }
}
fn bytes32(decoder: &mut Decoder<'_>) -> Result<[u8; 32]> {
    Ok(decoder.take(32)?.try_into().unwrap())
}
fn signature_data(role: &[u8], body: &[u8]) -> Vec<u8> {
    let mut data = role.to_vec();
    data.extend_from_slice(body);
    data
}
fn retry_end(end: Instant) -> Instant {
    end.min(Instant::now() + Duration::from_millis(350))
}

#[derive(Clone, Debug)]
pub struct Peer {
    pub root: [u8; 32],
    pub id: [u8; 32],
    pub epoch: u64,
    pub expires: u64,
}
pub struct Session {
    pub channel: Channel,
    cipher: Secret,
    pub peer: Peer,
    pub minor: u16,
    pub policy_digest: [u8; 32],
    binding: [u8; 32],
    last_peer: Vec<u8>,
    last_reply: Vec<u8>,
    ignore: Vec<u8>,
    pub session_id: [u8; 32],
}
impl Session {
    pub fn client(
        mut channel: Channel,
        identity: &Identity,
        expected: &[u8; 32],
        minimum_epoch: u64,
        minimum: u16,
        maximum: u16,
        ms: u32,
    ) -> Result<Self> {
        ensure(
            minimum <= maximum && maximum <= MINOR_MAX,
            4,
            "invalid minor version interval",
        )?;
        let end = crate::deadline(ms)?;
        channel.handshake(end)?;
        let mut signer = identity.signer()?;
        let record = signer.record(channel.local(), 1 << (channel.kind.profile() - 1), 300)?;
        let (ephemeral, public) = Secret::ephemeral()?;
        let session_id = crypto::random::<32>()?;
        let nonce = crypto::random::<32>()?;
        // Preserve the base v1 transcript; pool capabilities bind the corrected extension schema.
        let schema = legacy_descriptor_hash()?;
        let mut hello = b"DGN1".to_vec();
        hello.extend_from_slice(&session_id);
        hello.extend_from_slice(&nonce);
        hello.extend_from_slice(&public);
        hello.extend_from_slice(expected);
        hello.extend_from_slice(&schema);
        hello.extend_from_slice(&channel.kind.binding().to_be_bytes());
        hello.extend_from_slice(&minimum.to_be_bytes());
        hello.extend_from_slice(&maximum.to_be_bytes());
        hello.extend_from_slice(&(record.len() as u32).to_be_bytes());
        hello.extend_from_slice(&record);
        let signature = signer.sign(&signature_data(b"DLN-GATEWAY-CLIENT1", &hello))?;
        hello.extend_from_slice(&signature);
        let hello = packet(1, &hello)?;
        let response = loop {
            channel.send(&hello, end)?;
            match channel.receive(1000, retry_end(end)) {
                Ok(w) => {
                    let (kind, _) = unpack(&w)?;
                    ensure(kind == 2, 11, "server hello expected")?;
                    break w;
                }
                Err(e) if e.code == 3 && Instant::now() < end => continue,
                Err(e) => return Err(e),
            }
        };
        let (_, body) = unpack(&response)?;
        let mut d = Decoder::new(body);
        ensure(d.take(4)? == b"DGS1", 11, "invalid server hello domain")?;
        ensure(
            bytes32(&mut d)? == crypto::hash(&hello)?,
            12,
            "client transcript mismatch",
        )?;
        let _server_nonce = bytes32(&mut d)?;
        let remote_public = bytes32(&mut d)?;
        ensure(
            bytes32(&mut d)? == session_id && bytes32(&mut d)? == schema,
            12,
            "session or schema mismatch",
        )?;
        let minor = d.u16()?;
        ensure(
            (minimum..=maximum).contains(&minor),
            12,
            "unrequested protocol minor",
        )?;
        ensure(
            d.u32()? == channel.kind.binding(),
            12,
            "carrier profile mismatch",
        )?;
        let policy_digest = bytes32(&mut d)?;
        let n = d.u32()? as usize;
        let remote = Record::parse(d.take(n)?)?;
        remote.pinned(expected, minimum_epoch)?;
        let signature = d.take(64)?;
        d.end()?;
        crypto::verify(
            &remote.key,
            &signature_data(b"DLN-GATEWAY-SERVER1", &body[..body.len() - 64]),
            signature,
        )?;
        let mut transcript = hello;
        transcript.extend_from_slice(&response);
        let binding = crypto::hash(&transcript)?;
        let mut cipher = ephemeral.channel(&remote_public, &binding, &session_id, 1)?;
        let finish = packet(3, &cipher.seal(&binding, &aad(&binding, minor, 3))?)?;
        let received = loop {
            channel.send(&finish, end)?;
            match channel.receive(1000, retry_end(end)) {
                Ok(w) => {
                    if w == response {
                        continue;
                    }
                    let (kind, data) = unpack(&w)?;
                    ensure(kind == 4, 11, "server authentication finish expected")?;
                    ensure(
                        cipher.open(data, &aad(&binding, minor, 4))? == binding,
                        12,
                        "server key confirmation failed",
                    )?;
                    break w;
                }
                Err(e) if e.code == 3 && Instant::now() < end => continue,
                Err(e) => return Err(e),
            }
        };
        Ok(Self {
            channel,
            cipher,
            peer: Peer {
                root: remote.root,
                id: remote.id,
                epoch: remote.epoch,
                expires: remote.key_expires,
            },
            minor,
            policy_digest,
            binding,
            last_peer: response,
            last_reply: finish,
            ignore: received,
            session_id,
        })
    }
    pub fn server(
        mut channel: Channel,
        identity: &Identity,
        policy: &Policy,
        advertise: std::net::SocketAddr,
        authorise: impl FnOnce(&Peer) -> Result<()>,
    ) -> Result<Self> {
        let end = crate::deadline(policy.limits.handshake_ms)?;
        channel.advertise(advertise);
        channel.handshake(end)?;
        let hello = channel.receive(1000, end)?;
        let (kind, body) = unpack(&hello)?;
        ensure(kind == 1, 11, "client hello expected")?;
        let mut d = Decoder::new(body);
        ensure(d.take(4)? == b"DGN1", 11, "invalid client hello domain")?;
        let session_id = bytes32(&mut d)?;
        ensure(session_id != [0; 32], 11, "empty logical session identity")?;
        let _nonce = bytes32(&mut d)?;
        let public = bytes32(&mut d)?;
        let expected = bytes32(&mut d)?;
        let schema = bytes32(&mut d)?;
        ensure(
            schema == descriptor_hash()? || schema == legacy_descriptor_hash()?,
            12,
            "NetworkSpec descriptor mismatch",
        )?;
        ensure(
            d.u32()? == channel.kind.binding(),
            12,
            "requested carrier mismatch",
        )?;
        let minimum = d.u16()?;
        let maximum = d.u16()?;
        ensure(
            minimum <= maximum && minimum <= MINOR_MAX,
            6,
            "protocol minor interval unsupported",
        )?;
        let minor = maximum.min(MINOR_MAX);
        let n = d.u32()? as usize;
        let remote = Record::parse(d.take(n)?)?;
        let signature = d.take(64)?;
        d.end()?;
        policy.authorise_client(&remote.root, remote.epoch)?;
        crypto::verify(
            &remote.key,
            &signature_data(b"DLN-GATEWAY-CLIENT1", &body[..body.len() - 64]),
            signature,
        )?;
        let mut signer = identity.signer()?;
        let own = signer.record(advertise, 1 << (channel.kind.profile() - 1), 300)?;
        let own_record = Record::parse(&own)?;
        ensure(own_record.root == expected, 12, "gateway audience mismatch")?;
        let (ephemeral, local_public) = Secret::ephemeral()?;
        let policy_digest = policy.digest()?;
        let mut response = b"DGS1".to_vec();
        response.extend_from_slice(&crypto::hash(&hello)?);
        response.extend_from_slice(&crypto::random::<32>()?);
        response.extend_from_slice(&local_public);
        response.extend_from_slice(&session_id);
        response.extend_from_slice(&schema);
        response.extend_from_slice(&minor.to_be_bytes());
        response.extend_from_slice(&channel.kind.binding().to_be_bytes());
        response.extend_from_slice(&policy_digest);
        response.extend_from_slice(&(own.len() as u32).to_be_bytes());
        response.extend_from_slice(&own);
        let signature = signer.sign(&signature_data(b"DLN-GATEWAY-SERVER1", &response))?;
        response.extend_from_slice(&signature);
        let response = packet(2, &response)?;
        let mut transcript = hello.clone();
        transcript.extend_from_slice(&response);
        let binding = crypto::hash(&transcript)?;
        let mut cipher = ephemeral.channel(&public, &binding, &session_id, 2)?;
        let finish = loop {
            channel.send(&response, end)?;
            match channel.receive(1000, retry_end(end)) {
                Ok(w) => {
                    if w == hello {
                        continue;
                    }
                    let (kind, data) = unpack(&w)?;
                    ensure(kind == 3, 11, "client key confirmation expected")?;
                    ensure(
                        cipher.open(data, &aad(&binding, minor, 3))? == binding,
                        12,
                        "client key confirmation failed",
                    )?;
                    break w;
                }
                Err(e) if e.code == 3 && Instant::now() < end => continue,
                Err(e) => return Err(e),
            }
        };
        let peer = Peer {
            root: remote.root,
            id: remote.id,
            epoch: remote.epoch,
            expires: remote.key_expires,
        };
        authorise(&peer)?;
        let reply = packet(4, &cipher.seal(&binding, &aad(&binding, minor, 4))?)?;
        channel.send(&reply, end)?;
        Ok(Self {
            channel,
            cipher,
            peer: Peer {
                root: remote.root,
                id: remote.id,
                epoch: remote.epoch,
                expires: remote.key_expires,
            },
            minor,
            policy_digest,
            binding,
            last_peer: finish,
            last_reply: reply,
            ignore: hello,
            session_id,
        })
    }
    pub fn maximum_data(&self) -> usize {
        if self.channel.kind == Kind::QuicDatagram {
            768
        } else {
            DATA_MAX
        }
    }
    pub fn encode(&mut self, kind: u32, body: &[u8]) -> Result<Vec<u8>> {
        ensure(
            matches!(kind, REQUEST | RESPONSE | 7 | 8),
            11,
            "invalid authenticated message type",
        )?;
        ensure(now() < self.peer.expires, 14, "peer signing key expired")?;
        let bytes = packet(
            kind,
            &self
                .cipher
                .seal(body, &aad(&self.binding, self.minor, kind))?,
        )?;
        ensure(
            bytes.len() <= self.channel.kind.maximum(),
            7,
            "authenticated carrier capacity exceeded",
        )?;
        Ok(bytes)
    }
    pub fn send_wire(&mut self, wire: &[u8], end: Instant) -> Result<()> {
        self.channel.send(wire, end)
    }
    pub fn receive_wire(&mut self, end: Instant) -> Result<Vec<u8>> {
        loop {
            ensure(now() < self.peer.expires, 14, "peer signing key expired")?;
            let wire = self.channel.receive(self.channel.kind.maximum(), end)?;
            if wire == self.ignore {
                continue;
            }
            if wire == self.last_peer {
                self.channel.send(&self.last_reply, end)?;
                continue;
            }
            return Ok(wire);
        }
    }
    pub fn decode(&mut self, wire: &[u8], expected: u32) -> Result<Vec<u8>> {
        let (kind, data) = unpack(wire)?;
        ensure(
            kind == expected,
            11,
            "unexpected authenticated message type",
        )?;
        self.cipher
            .open(data, &aad(&self.binding, self.minor, kind))
    }
}
fn aad(binding: &[u8; 32], minor: u16, kind: u32) -> Vec<u8> {
    let mut out = b"DLN-GATEWAY-AAD1".to_vec();
    out.extend_from_slice(binding);
    out.extend_from_slice(&minor.to_be_bytes());
    out.extend_from_slice(&kind.to_be_bytes());
    out
}
