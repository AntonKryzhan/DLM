use crate::{deadline, ensure, ffi, wait, Error, Result};
use std::{
    collections::{BTreeMap, VecDeque},
    ffi::CString,
    net::SocketAddr,
    ptr::NonNull,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
pub const MAX_FRAME: usize = 65500;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Tls,
    QuicStream,
    QuicDatagram,
    WebSocket,
    HttpsConnect,
}
impl Kind {
    pub fn parse(value: &str) -> Result<Self> {
        match value {
            "tls" => Ok(Self::Tls),
            "quic-stream" => Ok(Self::QuicStream),
            "quic-datagram" => Ok(Self::QuicDatagram),
            "websocket" => Ok(Self::WebSocket),
            "https-connect" => Ok(Self::HttpsConnect),
            _ => Err(Error::new(6, "unsupported gateway transport")),
        }
    }
    pub fn name(self) -> &'static str {
        match self {
            Self::Tls => "tls",
            Self::QuicStream => "quic-stream",
            Self::QuicDatagram => "quic-datagram",
            Self::WebSocket => "websocket",
            Self::HttpsConnect => "https-connect",
        }
    }
    pub fn profile(self) -> u32 {
        match self {
            Self::Tls | Self::WebSocket | Self::HttpsConnect => 2,
            Self::QuicStream => 4,
            Self::QuicDatagram => 6,
        }
    }
    pub fn binding(self) -> u32 {
        match self {
            Self::WebSocket => 7,
            Self::HttpsConnect => 8,
            _ => self.profile(),
        }
    }
    pub fn is_tls(self) -> bool {
        matches!(self, Self::Tls | Self::WebSocket | Self::HttpsConnect)
    }
    pub fn maximum(self) -> usize {
        if self == Self::QuicDatagram {
            1000
        } else {
            MAX_FRAME
        }
    }
}
fn frame(data: &[u8]) -> Result<Vec<u8>> {
    ensure(data.len() <= MAX_FRAME, 7, "transport frame limit")?;
    let mut out = vec![0; 16 + data.len()];
    unsafe { ffi::dgn_frame(out.as_mut_ptr(), data.len() as u32) };
    out[16..].copy_from_slice(data);
    Ok(out)
}
pub(crate) fn frame_size(bytes: &[u8], maximum: usize) -> Result<usize> {
    ensure(
        bytes.len() >= 16 && maximum <= MAX_FRAME,
        11,
        "incomplete transport header",
    )?;
    let n = unsafe { ffi::dgn_frame_size(bytes.as_ptr(), maximum as u32) };
    ensure(n >= 0, 11, "invalid or oversized DLT1 frame")?;
    Ok(n as usize)
}
pub(crate) struct Tls {
    pub(crate) raw: NonNull<std::ffi::c_void>,
    pub(crate) input: Vec<u8>,
}
unsafe impl Send for Tls {}
impl Drop for Tls {
    fn drop(&mut self) {
        unsafe { ffi::dgn_tls_close(self.raw.as_ptr()) }
    }
}
#[derive(Default)]
struct Incoming {
    ready: bool,
    closed: bool,
    stream: Option<i64>,
    bytes: Vec<u8>,
    packets: VecDeque<Vec<u8>>,
}
struct QuicState {
    raw: NonNull<std::ffi::c_void>,
    server: bool,
    connections: BTreeMap<u64, Incoming>,
    pending: VecDeque<u64>,
    maximum: usize,
}
unsafe impl Send for QuicState {}
impl Drop for QuicState {
    fn drop(&mut self) {
        unsafe { ffi::dgn_quic_close(self.raw.as_ptr()) }
    }
}
impl QuicState {
    fn pump(&mut self) -> Result<()> {
        let progress = unsafe { ffi::dgn_quic_poll(self.raw.as_ptr()) };
        if !matches!(progress.status, 0 | 1 | 3) {
            return Err(Error {
                code: progress.status,
                system: progress.system,
                message: "QUIC endpoint progress failed",
            });
        }
        for _ in 0..256 {
            let event = unsafe { ffi::dgn_quic_next(self.raw.as_ptr()) };
            if event.status == 1 {
                break;
            }
            ensure(event.status == 0, 5, "QUIC event read failed")?;
            if event.kind == 1 {
                if self.connections.len() < self.maximum
                    || self.connections.contains_key(&event.connection)
                {
                    let entry = self.connections.entry(event.connection).or_default();
                    entry.ready = true;
                    if self.server && !self.pending.contains(&event.connection) {
                        self.pending.push_back(event.connection);
                    }
                } else {
                    unsafe { ffi::dgn_quic_disconnect(self.raw.as_ptr(), event.connection) };
                }
            }
            if matches!(event.kind, 2 | 3 | 4 | 5 | 6 | 8) {
                if let Some(entry) = self.connections.get_mut(&event.connection) {
                    if event.kind == 2 || event.kind == 3 {
                        if event.stream != 0 {
                            entry.closed = true;
                        } else {
                            entry.stream = Some(event.stream);
                        }
                    }
                    if event.kind == 3 || event.kind == 5 {
                        if event.count > MAX_FRAME + 16
                            || entry.bytes.len() + event.count > 2 * (MAX_FRAME + 16)
                        {
                            entry.closed = true;
                        } else if event.kind == 5 && entry.packets.len() >= 128 {
                            // DATAGRAM has erasure semantics; retain bounded storage without killing the authenticated connection.
                        } else {
                            let mut data = vec![0; event.count];
                            let n = unsafe {
                                ffi::dgn_quic_copy(
                                    self.raw.as_ptr(),
                                    event.id,
                                    data.as_mut_ptr(),
                                    data.len(),
                                )
                            }
                            .checked()?;
                            ensure(n == data.len(), 11, "short QUIC event copy")?;
                            if event.kind == 5 {
                                entry.packets.push_back(data);
                            } else {
                                entry.bytes.extend_from_slice(&data);
                            }
                        }
                    }
                    if matches!(event.kind, 4 | 6 | 8) {
                        entry.closed = true;
                    }
                }
            }
            unsafe { ffi::dgn_quic_ack(self.raw.as_ptr(), event.id) }.checked()?;
        }
        let retired: Vec<_> = self
            .connections
            .iter()
            .filter_map(|(&id, v)| if v.closed { Some(id) } else { None })
            .collect();
        for id in retired {
            unsafe { ffi::dgn_quic_disconnect(self.raw.as_ptr(), id) };
            let r = unsafe { ffi::dgn_quic_forget(self.raw.as_ptr(), id) };
            if r.status == 0 {
                self.connections.remove(&id);
                self.pending.retain(|v| *v != id);
            }
        }
        Ok(())
    }
}
type Endpoint = Arc<Mutex<QuicState>>;
enum Backend {
    Tls(Tls),
    Http(Box<crate::http_carrier::Http>),
    Quic { endpoint: Endpoint, id: u64 },
}
pub struct Channel {
    backend: Backend,
    pub kind: Kind,
    stop: Arc<AtomicBool>,
    local: SocketAddr,
}
impl Drop for Channel {
    fn drop(&mut self) {
        if let Backend::Quic { endpoint, id } = &self.backend {
            if let Ok(mut state) = endpoint.lock() {
                let _ = unsafe { ffi::dgn_quic_disconnect(state.raw.as_ptr(), *id) };
                // Flush CONNECTION_CLOSE before the last endpoint owner releases its socket.
                let _ = unsafe { ffi::dgn_quic_poll(state.raw.as_ptr()) };
                if let Some(entry) = state.connections.get_mut(id) {
                    entry.closed = true;
                }
                let _ = state.pump();
            }
        }
    }
}
impl Channel {
    pub(crate) fn advertise(&mut self, address: SocketAddr) {
        if let Backend::Http(http) = &mut self.backend {
            http.advertise(address);
        }
    }
    pub(crate) fn tls_mut(&mut self) -> Result<&mut Tls> {
        match &mut self.backend {
            Backend::Tls(tls) => Ok(tls),
            _ => Err(Error::new(4, "raw TLS channel required")),
        }
    }
    pub fn connect(
        peer: SocketAddr,
        hostname: &str,
        ca: &str,
        kind: Kind,
        ms: u32,
    ) -> Result<Self> {
        Self::connect_from(peer, hostname, ca, kind, ms, None)
    }
    pub fn connect_from(
        peer: SocketAddr,
        hostname: &str,
        ca: &str,
        kind: Kind,
        ms: u32,
        source: Option<SocketAddr>,
    ) -> Result<Self> {
        if let Some(source) = source {
            ensure(
                source.is_ipv4() == peer.is_ipv4()
                    && source.port() == 0
                    && !source.ip().is_unspecified(),
                4,
                "source address must be concrete, matching peer family, with port zero",
            )?;
        }
        let end = deadline(ms)?;
        let stop = Arc::new(AtomicBool::new(false));
        if kind.is_tls() {
            let p = unsafe {
                ffi::dgn_tls_connect_source(
                    peer.into(),
                    ffi::Text::of(hostname),
                    ffi::Text::of(ca),
                    ms,
                    source.map(Into::into).unwrap_or_default(),
                )
            }
            .checked()?;
            let raw = NonNull::new(p).unwrap();
            let local = unsafe { ffi::dgn_tls_local(p) }.checked();
            if let Err(e) = local {
                unsafe { ffi::dgn_tls_close(p) };
                return Err(e);
            }
            let tls = Tls {
                raw,
                input: Vec::new(),
            };
            let backend = if kind == Kind::Tls {
                Backend::Tls(tls)
            } else {
                Backend::Http(Box::new(crate::http_carrier::Http::connect(
                    tls,
                    kind,
                    hostname,
                    peer.port(),
                    end,
                    stop.clone(),
                )?))
            };
            return Ok(Self {
                backend,
                kind,
                stop,
                local: local.unwrap(),
            });
        }
        let bind = SocketAddr::new(
            if peer.is_ipv4() {
                "0.0.0.0".parse().unwrap()
            } else {
                "::".parse().unwrap()
            },
            0,
        );
        // The native route chooser binds a concrete source address for QUIC.
        let probe = std::net::UdpSocket::bind(source.unwrap_or(bind))?;
        probe.connect(peer)?;
        let bind = SocketAddr::new(probe.local_addr()?.ip(), 0);
        drop(probe);
        let p = unsafe {
            ffi::dgn_quic_new(bind.into(), false, ffi::Text::of(ca), ffi::Text::of(""), 1)
        }
        .checked()?;
        let mut state = QuicState {
            raw: NonNull::new(p).unwrap(),
            server: false,
            connections: BTreeMap::new(),
            pending: VecDeque::new(),
            maximum: 1,
        };
        let local = unsafe { ffi::dgn_quic_local(p) }.checked()?;
        let connected = unsafe { ffi::dgn_quic_connect(p, peer.into(), ffi::Text::of(hostname)) };
        ensure(
            connected.status == 0,
            connected.status,
            "QUIC connection rejected",
        )?;
        let id = connected.connection;
        state.connections.insert(id, Incoming::default());
        loop {
            state.pump()?;
            let statistics = unsafe { ffi::dgn_quic_stats(p, id) };
            if statistics.status != 0 {
                return Err(Error {
                    code: statistics.status,
                    system: statistics.system,
                    message: "QUIC handshake failed",
                });
            }
            if state.connections.get(&id).is_some_and(|v| v.ready) {
                break;
            }
            wait(end)?;
        }
        Ok(Self {
            backend: Backend::Quic {
                endpoint: Arc::new(Mutex::new(state)),
                id,
            },
            kind,
            stop,
            local,
        })
    }
    pub fn local(&self) -> SocketAddr {
        self.local
    }
    fn pause(&self, end: Instant) -> Result<()> {
        ensure(!self.stop.load(Ordering::Acquire), 9, "operation cancelled")?;
        wait(end)
    }
    pub fn send(&mut self, data: &[u8], end: Instant) -> Result<()> {
        ensure(
            data.len() <= self.kind.maximum(),
            7,
            "carrier payload limit",
        )?;
        let wire = frame(data)?;
        if let Backend::Http(http) = &mut self.backend {
            return http.send(&wire, end);
        }
        let mut at = 0;
        loop {
            ensure(!self.stop.load(Ordering::Acquire), 9, "operation cancelled")?;
            let sent = match &mut self.backend {
                Backend::Http(_) => unreachable!(),
                Backend::Tls(t) => unsafe {
                    ffi::dgn_tls_write(t.raw.as_ptr(), wire[at..].as_ptr(), wire.len() - at)
                },
                Backend::Quic { endpoint, id } => {
                    let mut state = endpoint
                        .lock()
                        .map_err(|_| Error::new(5, "QUIC endpoint lock failed"))?;
                    state.pump()?;
                    let raw = state.raw.as_ptr();
                    if self.kind == Kind::QuicDatagram {
                        ensure(
                            state.connections.get(id).is_some_and(|entry| !entry.closed),
                            2,
                            "QUIC connection closed",
                        )?;
                        unsafe { ffi::dgn_quic_datagram(raw, *id, wire.as_ptr(), wire.len()) }
                    } else {
                        let entry = state
                            .connections
                            .get_mut(id)
                            .ok_or(Error::new(2, "QUIC connection closed"))?;
                        if entry.stream.is_none() {
                            let stream = unsafe { ffi::dgn_quic_stream(raw, *id) };
                            ensure(stream.status == 0, stream.status, "QUIC stream open failed")?;
                            entry.stream = Some(stream.stream);
                        }
                        unsafe {
                            ffi::dgn_quic_write(
                                raw,
                                *id,
                                entry.stream.unwrap(),
                                wire[at..].as_ptr(),
                                wire.len() - at,
                            )
                        }
                    }
                }
            };
            if sent.status == 1 {
                self.pause(end)?;
                continue;
            }
            let count = sent.checked()?;
            ensure(
                count > 0 && count <= wire.len() - at,
                11,
                "invalid transport write count",
            )?;
            at += count;
            if at == wire.len() {
                return Ok(());
            }
            self.pause(end)?;
        }
    }
    pub fn receive(&mut self, maximum: usize, end: Instant) -> Result<Vec<u8>> {
        ensure(
            maximum <= self.kind.maximum(),
            4,
            "invalid carrier receive bound",
        )?;
        if let Backend::Http(http) = &mut self.backend {
            let wire = http.receive(end)?;
            let count = frame_size(&wire, maximum)?;
            ensure(
                wire.len() == count + 16,
                11,
                "HTTP binary message must contain one DLT1 frame",
            )?;
            return Ok(wire[16..].to_vec());
        }
        loop {
            let ready = match &mut self.backend {
                Backend::Http(_) => unreachable!(),
                Backend::Tls(t) => {
                    if t.input.len() >= 16 {
                        let n = frame_size(&t.input, maximum)?;
                        if t.input.len() >= 16 + n {
                            let result = t.input[16..16 + n].to_vec();
                            t.input.drain(..16 + n);
                            return Ok(result);
                        }
                    }
                    let mut bytes = [0; 8192];
                    let got = unsafe {
                        ffi::dgn_tls_read(t.raw.as_ptr(), bytes.as_mut_ptr(), bytes.len())
                    };
                    if got.status == 1 {
                        false
                    } else {
                        let n = got.checked()?;
                        ensure(
                            n > 0 && t.input.len() + n <= 2 * (MAX_FRAME + 16),
                            7,
                            "TLS receive storage bound",
                        )?;
                        t.input.extend_from_slice(&bytes[..n]);
                        true
                    }
                }
                Backend::Quic { endpoint, id } => {
                    let mut state = endpoint
                        .lock()
                        .map_err(|_| Error::new(5, "QUIC endpoint lock failed"))?;
                    state.pump()?;
                    let entry = state
                        .connections
                        .get_mut(id)
                        .ok_or(Error::new(2, "QUIC connection closed"))?;
                    if self.kind == Kind::QuicDatagram {
                        if let Some(packet) = entry.packets.pop_front() {
                            let n = frame_size(&packet, maximum)?;
                            ensure(packet.len() == n + 16, 11, "malformed datagram frame")?;
                            return Ok(packet[16..].to_vec());
                        }
                    } else if entry.bytes.len() >= 16 {
                        let n = frame_size(&entry.bytes, maximum)?;
                        if entry.bytes.len() >= 16 + n {
                            let result = entry.bytes[16..16 + n].to_vec();
                            entry.bytes.drain(..16 + n);
                            return Ok(result);
                        }
                    }
                    ensure(!entry.closed, 2, "QUIC stream closed")?;
                    false
                }
            };
            if !ready {
                self.pause(end)?;
            }
        }
    }
    pub fn set_cancel(&mut self, stop: Arc<AtomicBool>) {
        if let Backend::Http(http) = &mut self.backend {
            http.cancel(stop.clone());
        }
        self.stop = stop;
    }
    pub fn handshake(&mut self, end: Instant) -> Result<()> {
        if let Backend::Http(http) = &mut self.backend {
            return http.handshake(end);
        }
        loop {
            if let Backend::Tls(t) = &mut self.backend {
                let r = unsafe { ffi::dgn_tls_handshake(t.raw.as_ptr()) };
                if r.status == 0 {
                    return Ok(());
                }
                if r.status != 1 {
                    return Err(Error {
                        code: r.status,
                        system: r.system,
                        message: "TLS handshake failed",
                    });
                }
            } else {
                return Ok(());
            }
            self.pause(end)?;
        }
    }
}
enum Listening {
    Tls {
        raw: NonNull<std::ffi::c_void>,
        certificate: CString,
        key: CString,
    },
    Quic(Endpoint),
}
pub struct Listener {
    backend: Listening,
    pub address: SocketAddr,
    pub kind: Kind,
    stop: Arc<AtomicBool>,
}
unsafe impl Send for Listener {}
impl Drop for Listener {
    fn drop(&mut self) {
        if let Listening::Tls { raw, .. } = self.backend {
            unsafe { ffi::dgn_socket_close(raw.as_ptr()) }
        }
    }
}
impl Listener {
    pub fn bind(
        address: SocketAddr,
        kind: Kind,
        certificate: &str,
        key: &str,
        maximum: usize,
        stop: Arc<AtomicBool>,
    ) -> Result<Self> {
        ensure((1..=256).contains(&maximum), 4, "listener capacity bound")?;
        if kind.is_tls() {
            let certificate = CString::new(certificate)
                .map_err(|_| Error::new(4, "certificate path contains NUL"))?;
            let key = CString::new(key).map_err(|_| Error::new(4, "key path contains NUL"))?;
            unsafe { ffi::dgn_tls_validate(certificate.as_ptr(), key.as_ptr()) }.checked()?;
            let p = unsafe { ffi::dgn_listen(address.into()) }.checked()?;
            let actual = unsafe { ffi::dgn_local(p) }.checked();
            if let Err(e) = actual {
                unsafe { ffi::dgn_socket_close(p) };
                return Err(e);
            }
            return Ok(Self {
                backend: Listening::Tls {
                    raw: NonNull::new(p).unwrap(),
                    certificate,
                    key,
                },
                address: actual.unwrap(),
                kind,
                stop,
            });
        }
        let p = unsafe {
            ffi::dgn_quic_new(
                address.into(),
                true,
                ffi::Text::of(certificate),
                ffi::Text::of(key),
                maximum as u32,
            )
        }
        .checked()?;
        let state = QuicState {
            raw: NonNull::new(p).unwrap(),
            server: true,
            connections: BTreeMap::new(),
            pending: VecDeque::new(),
            maximum,
        };
        let actual = unsafe { ffi::dgn_quic_local(p) }.checked()?;
        Ok(Self {
            backend: Listening::Quic(Arc::new(Mutex::new(state))),
            address: actual,
            kind,
            stop,
        })
    }
    pub fn accept(&mut self) -> Result<Option<Channel>> {
        match &mut self.backend {
            Listening::Tls {
                raw,
                certificate,
                key,
            } => {
                let opened = unsafe {
                    ffi::dgn_tls_accept(raw.as_ptr(), certificate.as_ptr(), key.as_ptr())
                };
                if opened.status == 1 {
                    return Ok(None);
                }
                let p = opened.checked()?;
                let t = Tls {
                    raw: NonNull::new(p).unwrap(),
                    input: Vec::new(),
                };
                let local = unsafe { ffi::dgn_tls_local(p) }.checked()?;
                let backend = if self.kind == Kind::Tls {
                    Backend::Tls(t)
                } else {
                    unsafe { ffi::dgn_tls_http1(p) }.checked()?;
                    Backend::Http(Box::new(crate::http_carrier::Http::pending(
                        t,
                        self.kind,
                        self.stop.clone(),
                    )))
                };
                Ok(Some(Channel {
                    backend,
                    kind: self.kind,
                    stop: self.stop.clone(),
                    local,
                }))
            }
            Listening::Quic(endpoint) => {
                let mut state = endpoint
                    .lock()
                    .map_err(|_| Error::new(5, "QUIC listener lock failed"))?;
                state.pump()?;
                if let Some(id) = state.pending.pop_front() {
                    Ok(Some(Channel {
                        backend: Backend::Quic {
                            endpoint: endpoint.clone(),
                            id,
                        },
                        kind: self.kind,
                        stop: self.stop.clone(),
                        local: self.address,
                    }))
                } else {
                    Ok(None)
                }
            }
        }
    }
    pub fn pause(&self) {
        std::thread::sleep(Duration::from_millis(1));
    }
}
