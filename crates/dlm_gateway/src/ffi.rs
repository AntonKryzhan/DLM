#![allow(dead_code)]
use crate::{Error, Result};
use std::{
    ffi::{c_char, c_void},
    net::{IpAddr, SocketAddr},
};
pub type Handle = *mut c_void;
#[repr(C)]
#[derive(Default)]
pub struct IpPacket {
    pub required: usize,
    pub copied: usize,
    pub family: u32,
    pub ether_type: u32,
    pub offset: u32,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Io {
    pub count: usize,
    pub status: i32,
    pub system: i32,
}
impl Io {
    pub fn checked(self) -> Result<usize> {
        if self.status == 0 {
            Ok(self.count)
        } else {
            Err(Error {
                code: self.status,
                system: self.system,
                message: "native DLN provider failed",
            })
        }
    }
}
#[repr(C)]
pub struct Opened {
    pub handle: Handle,
    pub status: i32,
    pub system: i32,
}
impl Opened {
    pub fn checked(self) -> Result<Handle> {
        if self.status == 0 && !self.handle.is_null() {
            Ok(self.handle)
        } else {
            Err(Error {
                code: if self.status == 0 { 5 } else { self.status },
                system: self.system,
                message: "native DLN resource open failed",
            })
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    pub data: *const u8,
    pub len: usize,
}
impl Text {
    pub fn of(s: &str) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Address {
    pub family: u32,
    pub port: u32,
    pub scope: u32,
    pub bytes: [u8; 16],
}
impl From<SocketAddr> for Address {
    fn from(a: SocketAddr) -> Self {
        let mut r = Self {
            port: a.port() as u32,
            ..Self::default()
        };
        match a {
            SocketAddr::V4(v) => {
                r.family = 4;
                r.bytes[..4].copy_from_slice(&v.ip().octets());
            }
            SocketAddr::V6(v) => {
                r.family = 6;
                r.bytes = v.ip().octets();
                r.scope = v.scope_id();
            }
        }
        r
    }
}
impl Address {
    pub fn socket(self) -> Result<SocketAddr> {
        match self.family {
            4 => Ok(SocketAddr::new(
                IpAddr::V4(std::net::Ipv4Addr::new(
                    self.bytes[0],
                    self.bytes[1],
                    self.bytes[2],
                    self.bytes[3],
                )),
                self.port
                    .try_into()
                    .map_err(|_| Error::new(4, "invalid port"))?,
            )),
            6 => Ok(SocketAddr::V6(std::net::SocketAddrV6::new(
                self.bytes.into(),
                self.port
                    .try_into()
                    .map_err(|_| Error::new(4, "invalid port"))?,
                0,
                self.scope,
            ))),
            _ => Err(Error::new(4, "invalid address family")),
        }
    }
}
#[repr(C)]
pub struct Resolved {
    pub address: Address,
    pub status: i32,
    pub system: i32,
}
impl Resolved {
    pub fn checked(self) -> Result<SocketAddr> {
        Io {
            count: 0,
            status: self.status,
            system: self.system,
        }
        .checked()?;
        self.address.socket()
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Reference {
    pub connection: u64,
    pub stream: i64,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Event {
    pub id: u64,
    pub connection: u64,
    pub stream: i64,
    pub offset: u64,
    pub code: u64,
    pub count: usize,
    pub kind: u32,
    pub flags: u32,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Stats {
    pub used: usize,
    pub limit: usize,
    pub sent: u64,
    pub received: u64,
    pub lost: u64,
    pub cwnd: u64,
    pub in_flight: u64,
    pub rtt_ns: u64,
    pub connections: u32,
    pub streams: u32,
    pub events: u32,
    pub state: u32,
    pub version: u32,
    pub status: i32,
    pub system: i32,
}
extern "C" {
    pub fn dgn_wipe(data: *mut u8, count: usize);
    pub fn dgn_random(out: *mut u8, count: usize) -> Io;
    pub fn dgn_hash(data: *const u8, count: usize, out: *mut u8) -> Io;
    pub fn dgn_signer(
        root: *const u8,
        current: *const u8,
        epoch: u64,
        first: u64,
        last: u64,
    ) -> Opened;
    pub fn dgn_signer_close(p: Handle);
    pub fn dgn_sign(p: Handle, data: *const u8, count: usize, out: *mut u8) -> Io;
    pub fn dgn_verify(key: *const u8, data: *const u8, count: usize, signature: *const u8) -> i32;
    pub fn dgn_record(
        p: Handle,
        address: Address,
        profiles: u32,
        ttl: u32,
        out: *mut u8,
        capacity: usize,
    ) -> Io;
    pub fn dgn_record_verify(data: *const u8, count: usize) -> i32;
    pub fn dgn_ephemeral(public: *mut u8) -> Opened;
    pub fn dgn_secret_close(p: Handle);
    pub fn dgn_channel(
        p: Handle,
        peer: *const u8,
        binding: *const u8,
        session: *const u8,
        role: u32,
    ) -> Opened;
    pub fn dgn_seal(
        p: Handle,
        data: *const u8,
        count: usize,
        aad: *const u8,
        aad_count: usize,
        out: *mut u8,
        capacity: usize,
    ) -> Io;
    pub fn dgn_open(
        p: Handle,
        data: *const u8,
        count: usize,
        aad: *const u8,
        aad_count: usize,
        out: *mut u8,
        capacity: usize,
    ) -> Io;
    pub fn dgn_frame(out: *mut u8, count: u32);
    pub fn dgn_frame_size(header: *const u8, maximum: u32) -> i32;
    pub fn dgn_listen(address: Address) -> Opened;
    pub fn dgn_local(p: Handle) -> Resolved;
    pub fn dgn_socket_close(p: Handle);
    pub fn dgn_tls_validate(certificate: *const c_char, key: *const c_char) -> Io;
    pub fn dgn_tls_accept(p: Handle, certificate: *const c_char, key: *const c_char) -> Opened;
    pub fn dgn_tls_connect(peer: Address, hostname: Text, ca: Text, ms: u32) -> Opened;
    pub fn dgn_tls_handshake(p: Handle) -> Io;
    pub fn dgn_tls_local(p: Handle) -> Resolved;
    pub fn dgn_tls_read(p: Handle, out: *mut u8, count: usize) -> Io;
    pub fn dgn_tls_write(p: Handle, data: *const u8, count: usize) -> Io;
    pub fn dgn_tls_close(p: Handle);
    pub fn dgn_quic_new(
        address: Address,
        server: bool,
        certificate: Text,
        key: Text,
        connections: u32,
    ) -> Opened;
    pub fn dgn_quic_local(p: Handle) -> Resolved;
    pub fn dgn_quic_connect(p: Handle, peer: Address, hostname: Text) -> Reference;
    pub fn dgn_quic_poll(p: Handle) -> Io;
    pub fn dgn_quic_next(p: Handle) -> Event;
    pub fn dgn_quic_copy(p: Handle, id: u64, out: *mut u8, count: usize) -> Io;
    pub fn dgn_quic_ack(p: Handle, id: u64) -> Io;
    pub fn dgn_quic_stream(p: Handle, id: u64) -> Reference;
    pub fn dgn_quic_write(p: Handle, id: u64, stream: i64, data: *const u8, count: usize) -> Io;
    pub fn dgn_quic_datagram(p: Handle, id: u64, data: *const u8, count: usize) -> Io;
    pub fn dgn_quic_disconnect(p: Handle, id: u64) -> Io;
    pub fn dgn_quic_forget(p: Handle, id: u64) -> Io;
    pub fn dgn_quic_stats(p: Handle, id: u64) -> Stats;
    pub fn dgn_quic_close(p: Handle);
    pub fn dgn_journal_crypt(
        encrypt: bool,
        key: *const u8,
        aad: *const u8,
        aad_count: usize,
        input: *const u8,
        count: usize,
        output: *mut u8,
        capacity: usize,
    ) -> Io;
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Locator {
    pub address: Address,
    pub profiles: u32,
    pub priority: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OptimizerPolicy {
    pub delivery: u32,
    pub latency: u32,
    pub loss: u32,
    pub throughput: u32,
    pub cost: u32,
    pub max_age: u32,
    pub min_samples: u32,
    pub min_hold: u32,
    pub hysteresis: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Observation {
    pub id: u64,
    pub at_ms: u64,
    pub rtt_us: u64,
    pub goodput: u64,
    pub samples: u64,
    pub loss_ppm: u32,
    pub profile: u32,
    pub properties: u32,
    pub cost: u32,
    pub ready: bool,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Choice {
    pub id: u64,
    pub score: u64,
    pub profile: u32,
    pub properties: u32,
    pub index: u32,
    pub status: i32,
}
extern "C" {
    pub fn dgn_records(
        p: Handle,
        entries: *const Locator,
        count: usize,
        ttl: u32,
        out: *mut u8,
        capacity: usize,
    ) -> Io;
    pub fn dgn_record_resume(p: Handle, data: *const u8, count: usize) -> Io;
    pub fn dgn_choose(
        policy: OptimizerPolicy,
        values: *const Observation,
        count: usize,
        now: u64,
    ) -> Choice;
    pub fn dgn_path_properties(profile: u32) -> u32;
    pub fn dgn_tls_connect_source(
        peer: Address,
        hostname: Text,
        ca: Text,
        ms: u32,
        source: Address,
    ) -> Opened;
}
extern "C" {
    pub fn dgn_record_verify_historical(data: *const u8, count: usize) -> i32;
    pub fn dgn_websocket_digest(data: *const u8, count: usize, output: *mut u8) -> Io;
    pub fn dgn_tls_http1(handle: Handle) -> Io;
    pub fn dgn_tls_hostname(handle: Handle, output: *mut u8, capacity: usize) -> Io;
    pub fn dgn_ip_open(name: Text, library: Text) -> Opened;
    pub fn dgn_ip_adopt(descriptor: i64) -> Opened;
    pub fn dgn_ip_receive(handle: Handle, data: *mut u8, count: usize) -> IpPacket;
    pub fn dgn_ip_send(handle: Handle, data: *const u8, count: usize) -> Io;
    pub fn dgn_ip_packet(data: *const u8, count: usize) -> IpPacket;
    pub fn dgn_ip_close(handle: Handle);
    pub fn dgn_udp_dont_fragment(descriptor: i64, family: u32) -> Io;
}
