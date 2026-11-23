use crate::{capsule, ensure, ffi, Error, Result};
use std::{collections::BTreeSet, net::IpAddr, ptr::NonNull};
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Network {
    pub address: IpAddr,
    pub prefix: u8,
}
impl Network {
    pub fn parse(text: &str) -> Result<Self> {
        let (a, p) = text
            .split_once('/')
            .ok_or(Error::new(4, "IP prefix requires CIDR"))?;
        let address: IpAddr = a
            .parse()
            .map_err(|_| Error::new(4, "invalid IP prefix address"))?;
        let prefix: u8 = p
            .parse()
            .map_err(|_| Error::new(4, "invalid IP prefix length"))?;
        let value = Self { address, prefix };
        let (n, bits) = value.bits();
        ensure(
            prefix <= bits && n & Self::mask(bits, prefix) == n,
            4,
            "IP prefix has nonzero host bits or excessive length",
        )?;
        Ok(value)
    }
    fn bits(&self) -> (u128, u8) {
        match self.address {
            IpAddr::V4(a) => (u32::from(a) as u128, 32),
            IpAddr::V6(a) => (u128::from(a), 128),
        }
    }
    fn mask(bits: u8, prefix: u8) -> u128 {
        if prefix == 0 {
            0
        } else {
            u128::MAX.checked_shl((bits - prefix) as u32).unwrap_or(0)
        }
    }
    pub fn contains(&self, ip: IpAddr) -> bool {
        let other = Self {
            address: ip,
            prefix: 0,
        };
        let (a, n) = self.bits();
        let (b, m) = other.bits();
        n == m
            && self.prefix <= n
            && a & Self::mask(n, self.prefix) == b & Self::mask(n, self.prefix)
    }
    pub fn family(&self) -> u8 {
        if self.address.is_ipv4() {
            4
        } else {
            6
        }
    }
    pub fn bytes(&self) -> Vec<u8> {
        match self.address {
            IpAddr::V4(a) => a.octets().to_vec(),
            IpAddr::V6(a) => a.octets().to_vec(),
        }
    }
    fn last(&self) -> Vec<u8> {
        let (a, bits) = self.bits();
        let host = if self.prefix == 0 {
            if bits == 128 {
                u128::MAX
            } else {
                (1u128 << bits) - 1
            }
        } else {
            !Self::mask(bits, self.prefix)
        };
        let a = a | host;
        if bits == 32 {
            (a as u32).to_be_bytes().to_vec()
        } else {
            a.to_be_bytes().to_vec()
        }
    }
}
#[derive(Clone, Debug)]
pub struct IpConfig {
    pub interface: String,
    pub library: String,
    pub clients: Vec<Network>,
    pub routes: Vec<Network>,
    pub protocols: BTreeSet<u8>,
}
impl IpConfig {
    pub fn validate(&self) -> Result<()> {
        for network in self.clients.iter().chain(&self.routes) {
            Network::parse(&format!("{}/{}", network.address, network.prefix))?;
        }
        ensure(
            !self.interface.is_empty()
                && self.interface.len() <= 63
                && self.clients.len() >= 1
                && self.clients.len() <= 2
                && !self.routes.is_empty()
                && self.routes.len() <= 16
                && self.protocols.len() <= 256,
            4,
            "IP device, assignment or route quota",
        )?;
        ensure(
            self.clients
                .iter()
                .enumerate()
                .all(|(i, n)| self.clients[..i].iter().all(|p| p.family() != n.family()))
                && self
                    .routes
                    .iter()
                    .all(|r| self.clients.iter().any(|c| c.family() == r.family())),
            4,
            "IP assignments must be unique per family and cover routes",
        )
    }
    pub(crate) fn permits(&self, bytes: &[u8], client: bool, scope: &Scope) -> Result<()> {
        let (source, destination, protocol) = packet(bytes)?;
        let (assigned, target) = if client {
            (source, destination)
        } else {
            (destination, source)
        };
        ensure(
            self.clients.iter().any(|p| p.contains(assigned))
                && self.routes.iter().any(|p| p.contains(target))
                && (self.protocols.is_empty()
                    || self.protocols.contains(&0)
                    || self.protocols.contains(&protocol)
                    || protocol == 1
                    || protocol == 58)
                && scope.target.as_ref().is_none_or(|p| p.contains(target))
                && scope
                    .protocol
                    .is_none_or(|p| p == protocol || protocol == 1 || protocol == 58),
            16,
            "IP packet source, route or protocol denied",
        )
    }
    pub(crate) fn assignment(&self, requests: &[(u64, Network)]) -> Result<Vec<u8>> {
        let mut out = Vec::new();
        for n in &self.clients {
            let id = requests
                .iter()
                .find(|(_, r)| r.family() == n.family())
                .map(|p| p.0)
                .unwrap_or(0);
            capsule::put(id, &mut out)?;
            out.push(n.family());
            out.extend(n.bytes());
            out.push(n.prefix);
        }
        capsule::encode(1, &out)
    }
    pub(crate) fn advertisement(&self, scope: &Scope) -> Result<Vec<u8>> {
        let protocols: Vec<u8> = if let Some(p) = scope.protocol {
            ensure(
                self.protocols.is_empty()
                    || self.protocols.contains(&0)
                    || self.protocols.contains(&p)
                    || p == 1
                    || p == 58,
                16,
                "requested IP protocol denied",
            )?;
            vec![p]
        } else if self.protocols.is_empty() || self.protocols.contains(&0) {
            vec![0]
        } else {
            self.protocols.iter().copied().collect()
        };
        ensure(
            self.routes.len().saturating_mul(protocols.len()) <= 32,
            7,
            "IP route/protocol advertisement quota",
        )?;
        let mut out = Vec::new();
        for n in &self.routes {
            let n = if let Some(s) = &scope.target {
                if n.family() != s.family() {
                    continue;
                }
                if n.contains(s.address) && s.prefix >= n.prefix {
                    s
                } else if s.contains(n.address) && n.prefix >= s.prefix {
                    n
                } else {
                    continue;
                }
            } else {
                n
            };
            for &protocol in &protocols {
                out.push(n.family());
                out.extend(n.bytes());
                out.extend(n.last());
                out.push(protocol);
            }
        }
        ensure(
            !out.is_empty(),
            16,
            "requested IP scope has no permitted route",
        )?;
        capsule::encode(3, &out)
    }
}
#[derive(Default)]
pub(crate) struct Scope {
    pub target: Option<Network>,
    pub protocol: Option<u8>,
}
impl Scope {
    pub fn parse(target: &str, protocol: &str) -> Result<Self> {
        let target = capsule::percent_decode(target)?;
        let target = if target == "*" {
            None
        } else {
            Some(if target.contains('/') {
                Network::parse(&target)?
            } else {
                let ip: IpAddr = target.parse().map_err(|_| {
                    Error::new(6, "CONNECT-IP DNS scope unsupported; use IP prefix")
                })?;
                Network {
                    address: ip,
                    prefix: if ip.is_ipv4() { 32 } else { 128 },
                }
            })
        };
        let protocol = if protocol == "*" {
            None
        } else {
            Some(
                protocol
                    .parse()
                    .map_err(|_| Error::new(4, "invalid IP protocol scope"))?,
            )
        };
        Ok(Self { target, protocol })
    }
}
pub(crate) fn assignments(bytes: &[u8], request: bool) -> Result<Vec<(u64, Network)>> {
    let mut b = bytes;
    let mut out = Vec::new();
    let mut ids = BTreeSet::new();
    while !b.is_empty() {
        ensure(out.len() < 16, 7, "IP assignment count exceeded")?;
        let (id, n) =
            capsule::integer(b).ok_or(Error::new(11, "truncated assignment request ID"))?;
        b = &b[n..];
        ensure(!b.is_empty(), 11, "IP family absent")?;
        let family = b[0];
        let count = if family == 4 {
            4
        } else if family == 6 {
            16
        } else {
            return Err(Error::new(11, "invalid assignment IP family"));
        };
        ensure(b.len() >= count + 2, 11, "truncated assigned IP prefix")?;
        let address = if family == 4 {
            IpAddr::V4(<[u8; 4]>::try_from(&b[1..5]).unwrap().into())
        } else {
            IpAddr::V6(<[u8; 16]>::try_from(&b[1..17]).unwrap().into())
        };
        let prefix = Network::parse(&format!("{address}/{}", b[count + 1]))?;
        ensure(
            !request || (id != 0 && ids.insert(id)),
            11,
            "zero or reused IP address request ID",
        )?;
        out.push((id, prefix));
        b = &b[count + 2..];
    }
    ensure(!request || !out.is_empty(), 11, "empty ADDRESS_REQUEST")?;
    Ok(out)
}
pub(crate) fn routes(bytes: &[u8]) -> Result<()> {
    let mut b = bytes;
    let mut count = 0;
    while !b.is_empty() {
        count += 1;
        ensure(count <= 32, 7, "IP route count exceeded")?;
        let n = if b[0] == 4 {
            4
        } else if b[0] == 6 {
            16
        } else {
            return Err(Error::new(11, "invalid route IP family"));
        };
        ensure(
            b.len() >= 2 * n + 2 && b[1..1 + n] <= b[1 + n..1 + 2 * n],
            11,
            "truncated or reversed IP route range",
        )?;
        b = &b[2 * n + 2..];
    }
    Ok(())
}
pub(crate) fn route_permits(bytes: &[u8], address: IpAddr, protocol: u8) -> Result<bool> {
    routes(bytes)?;
    let mut b = bytes;
    let address = Network { address, prefix: 0 };
    let encoded = address.bytes();
    while !b.is_empty() {
        let n = if b[0] == 4 { 4 } else { 16 };
        if b[0] == address.family()
            && b[1..1 + n] <= encoded[..]
            && encoded[..] <= b[1 + n..1 + 2 * n]
            && (b[1 + 2 * n] == 0 || b[1 + 2 * n] == protocol || protocol == 1 || protocol == 58)
        {
            return Ok(true);
        }
        b = &b[2 * n + 2..];
    }
    Ok(false)
}
pub(crate) fn packet(bytes: &[u8]) -> Result<(IpAddr, IpAddr, u8)> {
    let p = unsafe { ffi::dgn_ip_packet(bytes.as_ptr(), bytes.len()) };
    ensure(
        p.status == 0 && bytes.len() <= 65535,
        11,
        "malformed or oversized IP packet",
    )?;
    if p.family == 4 {
        let n = (bytes[0] & 15) as usize * 4;
        let mut at = 20;
        while at < n {
            match bytes[at] {
                0 => break,
                1 => at += 1,
                131 | 137 => return Err(Error::new(16, "source-routed IP packets denied")),
                _ => {
                    ensure(
                        at + 1 < n && bytes[at + 1] >= 2 && at + bytes[at + 1] as usize <= n,
                        11,
                        "invalid IPv4 option length",
                    )?;
                    at += bytes[at + 1] as usize;
                }
            }
        }
        Ok((
            <[u8; 4]>::try_from(&bytes[12..16]).unwrap().into(),
            <[u8; 4]>::try_from(&bytes[16..20]).unwrap().into(),
            bytes[9],
        ))
    } else {
        ensure(bytes[6] != 43, 16, "IPv6 source routing denied")?;
        Ok((
            <[u8; 16]>::try_from(&bytes[8..24]).unwrap().into(),
            <[u8; 16]>::try_from(&bytes[24..40]).unwrap().into(),
            bytes[6],
        ))
    }
}
pub(crate) struct Device {
    raw: NonNull<std::ffi::c_void>,
    buffer: Vec<u8>,
}
unsafe impl Send for Device {}
impl Drop for Device {
    fn drop(&mut self) {
        unsafe { ffi::dgn_ip_close(self.raw.as_ptr()) }
    }
}
impl Device {
    pub fn open(config: &IpConfig) -> Result<Self> {
        config.validate()?;
        let p = unsafe {
            ffi::dgn_ip_open(
                ffi::Text::of(&config.interface),
                ffi::Text::of(&config.library),
            )
        }
        .checked()?;
        Ok(Self {
            raw: NonNull::new(p).unwrap(),
            buffer: vec![0; 65535],
        })
    }
    #[cfg(all(test, unix))]
    pub fn adopt(fd: i64) -> Result<Self> {
        let p = unsafe { ffi::dgn_ip_adopt(fd) }.checked()?;
        Ok(Self {
            raw: NonNull::new(p).unwrap(),
            buffer: vec![0; 65535],
        })
    }
    pub fn send(&mut self, bytes: &[u8]) -> Result<bool> {
        let r = unsafe { ffi::dgn_ip_send(self.raw.as_ptr(), bytes.as_ptr(), bytes.len()) };
        if r.status == 1 {
            return Ok(false);
        }
        ensure(r.checked()? == bytes.len(), 11, "partial IP packet write")?;
        Ok(true)
    }
    pub fn receive(&mut self) -> Result<Option<Vec<u8>>> {
        let p = unsafe {
            ffi::dgn_ip_receive(
                self.raw.as_ptr(),
                self.buffer.as_mut_ptr(),
                self.buffer.len(),
            )
        };
        if p.status == 1 {
            return Ok(None);
        }
        ensure(p.status == 0, p.status, "IP device receive failed")?;
        Ok(Some(self.buffer[..p.copied].to_vec()))
    }
}
