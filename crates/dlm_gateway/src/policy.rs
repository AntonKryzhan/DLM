use crate::{
    crypto, ensure,
    identity::{hex, unhex},
    Error, Result,
};
use std::{
    collections::BTreeMap,
    net::{IpAddr, SocketAddr, ToSocketAddrs},
    str::FromStr,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TargetProtocol {
    Tcp = 1,
    Udp = 2,
}
impl TargetProtocol {
    pub fn parse(s: &str) -> Result<Self> {
        match s {
            "tcp" => Ok(Self::Tcp),
            "udp" => Ok(Self::Udp),
            _ => Err(Error::new(4, "unknown target protocol")),
        }
    }
}
#[derive(Clone, Debug)]
pub struct TargetRule {
    pub protocol: TargetProtocol,
    pub network: IpAddr,
    pub prefix: u8,
    pub first: u16,
    pub last: u16,
}
fn normalise(ip: IpAddr) -> IpAddr {
    if let IpAddr::V6(v) = ip {
        if let Some(v4) = v.to_ipv4_mapped() {
            return IpAddr::V4(v4);
        }
    }
    ip
}
fn bits(ip: IpAddr) -> (u128, u8) {
    match normalise(ip) {
        IpAddr::V4(v) => (u32::from(v) as u128, 32),
        IpAddr::V6(v) => (u128::from(v), 128),
    }
}
fn public(ip: IpAddr) -> bool {
    match normalise(ip) {
        IpAddr::V4(v) => {
            let b = v.octets();
            !(v.is_private()
                || v.is_documentation()
                || v.is_loopback()
                || v.is_link_local()
                || v.is_multicast()
                || v.is_broadcast()
                || b[0] == 0
                || b[0] >= 240
                || (b[0] == 100 && b[1] & 192 == 64)
                || (b[0] == 192 && b[1] == 0 && b[2] == 0)
                || (b[0] == 198 && b[1] & 254 == 18))
        }
        IpAddr::V6(v) => {
            let b = v.octets();
            b[0] & 224 == 32
                && !(b[0] == 32 && b[1] == 2)
                && !(b[..4] == [32, 1, 0, 0])
                && !(b[..4] == [32, 1, 13, 184])
        }
    }
}
impl TargetRule {
    pub fn parse(text: &str) -> Result<Self> {
        let v: Vec<_> = text.split(',').collect();
        ensure(
            v.len() == 3,
            4,
            "target rule requires protocol,CIDR,port-range",
        )?;
        let protocol = TargetProtocol::parse(v[0])?;
        let (ip, prefix) = v[1]
            .split_once('/')
            .ok_or(Error::new(4, "target CIDR prefix missing"))?;
        let network =
            normalise(IpAddr::from_str(ip).map_err(|_| Error::new(4, "invalid CIDR address"))?);
        let prefix: u8 = prefix
            .parse()
            .map_err(|_| Error::new(4, "invalid CIDR prefix"))?;
        ensure(prefix <= bits(network).1, 4, "CIDR prefix out of bounds")?;
        let (first, last) = v[2].split_once('-').unwrap_or((v[2], v[2]));
        let first = first
            .parse()
            .map_err(|_| Error::new(4, "invalid target port"))?;
        let last = last
            .parse()
            .map_err(|_| Error::new(4, "invalid target port"))?;
        ensure(first > 0 && first <= last, 4, "invalid target port range")?;
        Ok(Self {
            protocol,
            network,
            prefix,
            first,
            last,
        })
    }
    pub fn accepts(&self, protocol: TargetProtocol, address: SocketAddr) -> bool {
        let (a, width) = bits(address.ip());
        let (b, other) = bits(self.network);
        if width != other || self.prefix > width {
            return false;
        }
        let mask = if self.prefix == 0 {
            0
        } else {
            u128::MAX << (width - self.prefix)
        };
        protocol == self.protocol
            && (a & mask) == (b & mask)
            && (self.first..=self.last).contains(&address.port())
    }
}
#[derive(Clone, Debug)]
pub struct Limits {
    pub sessions: usize,
    pub per_client: usize,
    pub handshakes: usize,
    pub streams: usize,
    pub pending: usize,
    pub handshake_ms: u32,
    pub idle_ms: u32,
    pub request_ms: u32,
    pub session_bytes: u64,
    pub bytes_per_second: u64,
}
impl Default for Limits {
    fn default() -> Self {
        Self {
            sessions: 32,
            per_client: 4,
            handshakes: 8,
            streams: 8,
            pending: 8,
            handshake_ms: 5000,
            idle_ms: 30000,
            request_ms: 5000,
            session_bytes: 67108864,
            bytes_per_second: 8388608,
        }
    }
}
impl Limits {
    pub fn validate(&self) -> Result<()> {
        ensure(
            (1..=256).contains(&self.sessions)
                && (1..=self.sessions).contains(&self.per_client)
                && (1..=self.sessions).contains(&self.handshakes)
                && (1..=64).contains(&self.streams)
                && (1..=256).contains(&self.pending),
            4,
            "invalid session/stream quotas",
        )?;
        ensure(
            (1..=60000).contains(&self.handshake_ms)
                && (1..=60000).contains(&self.idle_ms)
                && (1..=60000).contains(&self.request_ms)
                && self.session_bytes >= 1024
                && self.bytes_per_second >= 1024,
            4,
            "invalid time/byte quotas",
        )
    }
}
#[derive(Clone, Debug)]
pub struct Policy {
    pub clients: BTreeMap<[u8; 32], u64>,
    pub targets: Vec<TargetRule>,
    pub allow_private: bool,
    pub limits: Limits,
}
impl Policy {
    pub fn new() -> Self {
        Self {
            clients: BTreeMap::new(),
            targets: Vec::new(),
            allow_private: false,
            limits: Limits::default(),
        }
    }
    pub fn client(&mut self, key: &str) -> Result<()> {
        ensure(self.clients.len() < 1024, 7, "client policy capacity")?;
        let (root, epoch) = key.split_once('@').unwrap_or((key, "1"));
        let epoch: u64 = epoch
            .parse()
            .map_err(|_| Error::new(4, "invalid minimum key epoch"))?;
        ensure(epoch > 0, 4, "minimum key epoch must be positive")?;
        let root = unhex(root)?;
        ensure(
            !self.clients.contains_key(&root),
            4,
            "duplicate client identity policy",
        )?;
        self.clients.insert(root, epoch);
        Ok(())
    }
    pub fn validate(&self) -> Result<()> {
        self.limits.validate()?;
        ensure(
            !self.clients.is_empty() && !self.targets.is_empty() && self.targets.len() <= 256,
            4,
            "gateway requires bounded client and target allowlists",
        )
    }
    pub fn authorise_client(&self, root: &[u8; 32], epoch: u64) -> Result<()> {
        ensure(
            self.clients
                .get(root)
                .is_some_and(|minimum| epoch >= *minimum),
            16,
            "client denied by gateway policy",
        )
    }
    pub fn permits(&self, protocol: TargetProtocol, address: SocketAddr) -> bool {
        (self.allow_private || public(address.ip()))
            && self.targets.iter().any(|r| r.accepts(protocol, address))
    }
    pub fn resolve(&self, protocol: TargetProtocol, target: &str) -> Result<Vec<SocketAddr>> {
        ensure(
            !target.is_empty()
                && target.len() <= 255
                && target.is_ascii()
                && !target
                    .bytes()
                    .any(|b| b.is_ascii_whitespace() || b == 0 || b == b'/' || b == b'@'),
            4,
            "invalid target name",
        )?;
        let addresses = resolve_bounded(target, self.limits.request_ms)?;
        let mut out = Vec::new();
        for address in addresses {
            ensure(
                self.permits(protocol, address),
                16,
                "resolved destination denied by gateway policy",
            )?;
            if !out.contains(&address) {
                out.push(address);
            }
        }
        ensure(!out.is_empty(), 17, "destination has no usable address")?;
        Ok(out)
    }
    pub fn digest(&self) -> Result<[u8; 32]> {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(b"DLN-GATEWAY-POLICY1\0");
        bytes.push(self.allow_private as u8);
        for (id, epoch) in &self.clients {
            bytes.extend_from_slice(id);
            bytes.extend_from_slice(&epoch.to_be_bytes());
        }
        for r in &self.targets {
            bytes.extend_from_slice(
                format!(
                    "{:?},{}/{},{}-{}\n",
                    r.protocol, r.network, r.prefix, r.first, r.last
                )
                .as_bytes(),
            );
        }
        bytes.extend_from_slice(format!("{:?}", self.limits).as_bytes());
        crypto::hash(&bytes)
    }
    pub fn client_ids(&self) -> Vec<String> {
        self.clients.keys().map(|id| hex(id)).collect()
    }
}
impl Default for Policy {
    fn default() -> Self {
        Self::new()
    }
}

// A fixed process-wide resolver pool bounds OS resolver calls even after caller timeouts.
fn resolve_bounded(target: &str, timeout: u32) -> Result<Vec<SocketAddr>> {
    if let Ok(address) = target.parse::<SocketAddr>() {
        return Ok(vec![address]);
    }
    use std::sync::{
        mpsc::{self, SyncSender},
        Arc, Mutex, OnceLock,
    };
    type Job = (String, SyncSender<Result<Vec<SocketAddr>>>);
    static RESOLVER: OnceLock<SyncSender<Job>> = OnceLock::new();
    let sender = RESOLVER.get_or_init(|| {
        let (tx, rx) = mpsc::sync_channel::<Job>(32);
        let rx = Arc::new(Mutex::new(rx));
        for _ in 0..4 {
            let rx = rx.clone();
            std::thread::spawn(move || loop {
                let job = match rx.lock() {
                    Ok(receiver) => receiver.recv(),
                    Err(_) => return,
                };
                let Ok((name, reply)) = job else { return };
                let result = name
                    .to_socket_addrs()
                    .map_err(|_| Error::new(17, "destination resolution failed"))
                    .and_then(|items| {
                        let addresses: Vec<_> = items.take(17).collect();
                        ensure(addresses.len() <= 16, 7, "DNS answer limit")?;
                        Ok(addresses)
                    });
                let _ = reply.try_send(result);
            });
        }
        tx
    });
    let (tx, rx) = mpsc::sync_channel(1);
    sender
        .try_send((target.to_owned(), tx))
        .map_err(|_| Error::new(7, "resolver queue exhausted"))?;
    rx.recv_timeout(std::time::Duration::from_millis(timeout as u64))
        .map_err(|_| Error::new(3, "destination DNS deadline expired"))?
}
