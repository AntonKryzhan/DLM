use crate::{
    capsule, crypto, deadline, ensure, ffi,
    http_carrier::{self, Head},
    identity::hex,
    journal,
    policy::{Policy, TargetProtocol},
    proxy_ip::{self, Device, Scope},
    transport::{Channel, Kind, Listener},
    Error, Result,
};
pub use crate::proxy_ip::{IpConfig, Network};
use std::{
    io::{Read, Write},
    net::{Shutdown, SocketAddr, TcpStream, UdpSocket},
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};
const BUFFER: usize = 2 * (capsule::MAX + 32);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Mode {
    Tcp = 1,
    Udp = 2,
    Ip = 3,
}
impl Mode {
    pub fn parse(n: u32) -> Result<Self> {
        match n {
            1 => Ok(Self::Tcp),
            2 => Ok(Self::Udp),
            3 => Ok(Self::Ip),
            _ => Err(Error::new(4, "unknown proxy tunnel mode")),
        }
    }
}
#[derive(Clone, Debug)]
pub struct ClientConfig {
    pub address: SocketAddr,
    pub hostname: String,
    pub ca: String,
    pub token: PathBuf,
    pub timeout_ms: u32,
    pub http_version: u32,
}
#[derive(Clone, Debug)]
pub struct ServerConfig {
    pub bind: SocketAddr,
    pub certificate: String,
    pub key: String,
    pub token: PathBuf,
    pub policy: Policy,
    pub ip: Option<IpConfig>,
}
#[derive(Clone, Copy, Default, Debug)]
#[repr(C)]
pub struct Statistics {
    pub accepted: u64,
    pub authenticated: u64,
    pub denied: u64,
    pub active: u64,
    pub incoming: u64,
    pub outgoing: u64,
    pub udp: u64,
    pub ip: u64,
    pub failed: u64,
}
#[derive(Default)]
struct Counters {
    rate: Mutex<Rate>,
    handshakes: AtomicU64,
    accepted: AtomicU64,
    authenticated: AtomicU64,
    denied: AtomicU64,
    active: AtomicU64,
    incoming: AtomicU64,
    outgoing: AtomicU64,
    udp: AtomicU64,
    ip: AtomicU64,
    failed: AtomicU64,
}
struct Rate {
    at: Instant,
    bytes: u64,
}
impl Default for Rate {
    fn default() -> Self {
        Self {
            at: Instant::now(),
            bytes: 0,
        }
    }
}
impl Counters {
    fn charge(&self, n: usize, limit: u64) -> Result<()> {
        let mut r = self
            .rate
            .lock()
            .map_err(|_| Error::new(5, "proxy rate budget lock failed"))?;
        if r.at.elapsed() >= Duration::from_secs(1) {
            r.at = Instant::now();
            r.bytes = 0;
        }
        ensure(
            r.bytes.saturating_add(n as u64) <= limit,
            7,
            "proxy aggregate byte rate exceeded",
        )?;
        r.bytes += n as u64;
        Ok(())
    }
    fn snapshot(&self) -> Statistics {
        Statistics {
            accepted: self.accepted.load(Ordering::Relaxed),
            authenticated: self.authenticated.load(Ordering::Relaxed),
            denied: self.denied.load(Ordering::Relaxed),
            active: self.active.load(Ordering::Acquire),
            incoming: self.incoming.load(Ordering::Relaxed),
            outgoing: self.outgoing.load(Ordering::Relaxed),
            udp: self.udp.load(Ordering::Relaxed),
            ip: self.ip.load(Ordering::Relaxed),
            failed: self.failed.load(Ordering::Relaxed),
        }
    }
}
struct Handshake(Arc<Counters>);
impl Drop for Handshake {
    fn drop(&mut self) {
        self.0.handshakes.fetch_sub(1, Ordering::AcqRel);
    }
}
struct Active(Arc<Counters>);
impl Drop for Active {
    fn drop(&mut self) {
        self.0.active.fetch_sub(1, Ordering::AcqRel);
    }
}
fn token(path: &std::path::Path) -> Result<crypto::Bytes> {
    let key = journal::load_key(path)?;
    Ok(crypto::Bytes(format!("Bearer {}", hex(&key)).into_bytes()))
}
fn equal(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0;
    for (i, j) in a.iter().zip(b) {
        diff |= i ^ j;
    }
    diff == 0
}
fn host(config: &ClientConfig) -> String {
    if config.hostname.contains(':') {
        format!("[{}]:{}", config.hostname, config.address.port())
    } else {
        format!("{}:{}", config.hostname, config.address.port())
    }
}
fn udp_path(target: &str) -> Result<String> {
    http_carrier::authority(target)?;
    let (h, p) = target.rsplit_once(':').unwrap();
    let h = h.trim_start_matches('[').trim_end_matches(']');
    ensure(!h.contains('%'), 4, "scoped IPv6 proxy targets unsupported")?;
    Ok(format!(
        "/.well-known/masque/udp/{}/{p}/",
        capsule::percent_encode(h)
    ))
}
fn target_path<'a>(path: &'a str, authority: &str) -> Result<&'a str> {
    let path = if let Some(rest) = path.strip_prefix("https://") {
        let (a, _) = rest
            .split_once('/')
            .ok_or(Error::new(11, "absolute proxy URI has no path"))?;
        ensure(a == authority, 11, "proxy URI origin differs from Host")?;
        &rest[a.len()..]
    } else {
        path
    };
    ensure(
        path.starts_with('/') && !path.contains(['#', '?']),
        11,
        "unsupported proxy request URI",
    )?;
    Ok(path)
}
fn udp_target(path: &str) -> Result<String> {
    let f: Vec<_> = path.split('/').collect();
    ensure(
        f.len() == 7 && f[..4] == ["", ".well-known", "masque", "udp"] && f[6].is_empty(),
        11,
        "invalid CONNECT-UDP URI",
    )?;
    let h = capsule::percent_decode(f[4])?;
    ensure(
        !h.contains(['[', ']', '%', '/', '@']),
        11,
        "invalid CONNECT-UDP host",
    )?;
    let target = if h.contains(':') {
        format!("[{h}]:{}", f[5])
    } else {
        format!("{h}:{}", f[5])
    };
    http_carrier::authority(&target)?;
    Ok(target)
}
fn upgrade(head: &Head, mode: Mode) -> Result<()> {
    ensure(
        head.start.split(' ').next() == Some("HTTP/1.1")
            && head.start.split(' ').nth(1) == Some("101")
            && head.token("connection", "upgrade")
            && head
                .get("upgrade")
                .eq_ignore_ascii_case(if mode == Mode::Udp {
                    "connect-udp"
                } else {
                    "connect-ip"
                })
            && head
                .get("capsule-protocol")
                .split(';')
                .next()
                .unwrap_or("")
                .trim()
                == "?1",
        6,
        "MASQUE upgrade or capsule capability unsupported",
    )?;
    head.no_content()
}
pub struct Tunnel {
    channel: Channel,
    mode: Mode,
    decoder: capsule::Decoder,
    stop: Arc<AtomicBool>,
    timeout: u32,
    closed: bool,
    pub assignments: Vec<Network>,
    pub routes: Vec<u8>,
    packet_pending: Option<Vec<u8>>,
    requested_protocol: Option<u8>,
}
impl Tunnel {
    pub fn connect(
        config: &ClientConfig,
        mode: Mode,
        target: &str,
        source: Option<SocketAddr>,
    ) -> Result<Self> {
        ensure(
            config.http_version == 1,
            6,
            "this MASQUE adapter requires HTTP/1.1; HTTP/2 and HTTP/3 are not negotiated",
        )?;
        let end = deadline(config.timeout_ms)?;
        let secret = token(&config.token)?;
        let stop = Arc::new(AtomicBool::new(false));
        let mut channel = Channel::connect_from(
            config.address,
            &config.hostname,
            &config.ca,
            Kind::Tls,
            config.timeout_ms,
            source,
        )?;
        channel.set_cancel(stop.clone());
        let authority = host(config);
        http_carrier::authority(&authority)?;
        let auth = std::str::from_utf8(&secret.0).unwrap();
        let request = if mode == Mode::Tcp {
            http_carrier::authority(target)?;
            format!("CONNECT {target} HTTP/1.1\r\nHost: {authority}\r\nProxy-Authorization: {auth}\r\n\r\n")
        } else {
            let path = if mode == Mode::Udp {
                udp_path(target)?
            } else {
                let (s, p) = target.rsplit_once(',').ok_or(Error::new(
                    4,
                    "IP target requires prefix-or-*,protocol-or-*",
                ))?;
                Scope::parse(&capsule::percent_encode(s), p)?;
                format!("/.well-known/masque/ip/{}/{p}/", capsule::percent_encode(s))
            };
            let name = if mode == Mode::Udp {
                "connect-udp"
            } else {
                "connect-ip"
            };
            format!("GET {path} HTTP/1.1\r\nHost: {authority}\r\nConnection: Upgrade\r\nUpgrade: {name}\r\nCapsule-Protocol: ?1\r\nProxy-Authorization: {auth}\r\n\r\n")
        };
        let tls = channel.tls_mut()?;
        tls.write_all(request.as_bytes(), end, &stop)?;
        let head = tls.read_head(end, &stop)?;
        let status = head.start.split(' ').nth(1).unwrap_or("");
        match status {
            "407" | "403" => {
                return Err(Error::new(16, "proxy authentication or destination denied"))
            }
            "503" | "429" => return Err(Error::new(7, "proxy session quota unavailable")),
            "502" => return Err(Error::new(17, "proxy destination unavailable")),
            "504" => return Err(Error::new(3, "proxy destination timeout")),
            "400" => return Err(Error::new(11, "proxy request refused as malformed")),
            _ => {}
        }
        if mode == Mode::Tcp {
            head.no_content()?;
            ensure(
                head.start.starts_with("HTTP/1.1 ")
                    && status.len() == 3
                    && status.starts_with('2')
                    && status.bytes().all(|c| c.is_ascii_digit()),
                6,
                "HTTPS proxy CONNECT unsupported",
            )?;
        } else {
            upgrade(&head, mode)?;
        }
        let mut tunnel = Self {
            channel,
            mode,
            decoder: capsule::Decoder::default(),
            stop,
            timeout: config.timeout_ms,
            closed: false,
            assignments: Vec::new(),
            routes: Vec::new(),
            packet_pending: None,
            requested_protocol: if mode == Mode::Ip {
                let (_, p) = target.rsplit_once(',').unwrap();
                if p == "*" {
                    None
                } else {
                    Some(
                        p.parse()
                            .map_err(|_| Error::new(4, "invalid IP protocol scope"))?,
                    )
                }
            } else {
                None
            },
        };
        if mode == Mode::Ip {
            let mut have_routes = false;
            while tunnel.assignments.is_empty() || !have_routes {
                let Some((kind, body)) = tunnel.next(end)? else {
                    continue;
                };
                match kind {
                    1 => {
                        tunnel.assignments = proxy_ip::assignments(&body, false)?
                            .into_iter()
                            .map(|(_, n)| n)
                            .collect()
                    }
                    3 => {
                        proxy_ip::routes(&body)?;
                        ensure(!body.is_empty(), 16, "IP proxy granted no routes")?;
                        tunnel.routes = body;
                        have_routes = true;
                    }
                    0 => {
                        return Err(Error::new(
                            11,
                            "IP payload arrived before assignment/route grant",
                        ))
                    }
                    2 => {
                        return Err(Error::new(
                            6,
                            "IP proxy requested reciprocal address assignment",
                        ))
                    }
                    _ => {}
                }
            }
        }
        Ok(tunnel)
    }
    pub fn mode(&self) -> Mode {
        self.mode
    }
    pub fn local(&self) -> SocketAddr {
        self.channel.local()
    }
    pub fn cancel(&self) {
        self.stop.store(true, Ordering::Release);
    }
    pub(crate) fn cancellation(&self) -> Arc<AtomicBool> {
        self.stop.clone()
    }
    pub fn send(&mut self, bytes: &[u8]) -> Result<()> {
        ensure(!self.closed, 2, "proxy tunnel closed")?;
        let wire = if self.mode == Mode::Tcp {
            ensure(bytes.len() <= 65535, 7, "proxy stream write limit")?;
            bytes.to_vec()
        } else {
            ensure(
                bytes.len() <= if self.mode == Mode::Udp { 65527 } else { 65535 },
                7,
                "proxy packet exceeds transport limit",
            )?;
            if self.mode == Mode::Ip {
                let (s, d, p) = proxy_ip::packet(bytes)?;
                ensure(
                    self.requested_protocol
                        .is_none_or(|v| v == p || p == 1 || p == 58)
                        && self.assignments.iter().any(|n| n.contains(s))
                        && proxy_ip::route_permits(&self.routes, d, p)?,
                    16,
                    "IP source or destination outside tunnel grants",
                )?;
            }
            capsule::data(bytes)?
        };
        let result = self
            .channel
            .tls_mut()?
            .write_all(&wire, deadline(self.timeout)?, &self.stop);
        if result.is_err() {
            self.closed = true;
        }
        result
    }
    fn next(&mut self, end: Instant) -> Result<Option<(u64, Vec<u8>)>> {
        let tls = self.channel.tls_mut()?;
        if let Some(value) = self.decoder.next(&mut tls.input)? {
            return Ok(Some(value));
        }
        match tls.read_more(BUFFER) {
            Ok(true) => {}
            Ok(false) => http_carrier::pause(&self.stop, end)?,
            Err(e) => {
                self.closed = true;
                if e.code == 2 {
                    self.decoder.finish(&tls.input)?;
                }
                return Err(e);
            }
        }
        Ok(None)
    }
    pub fn receive(&mut self, maximum: usize) -> Result<Vec<u8>> {
        ensure(
            !self.closed && maximum > 0 && maximum <= 65535,
            4,
            "invalid proxy receive capacity or closed tunnel",
        )?;
        let end = deadline(self.timeout)?;
        if self.mode == Mode::Tcp {
            let tls = self.channel.tls_mut()?;
            loop {
                if !tls.input.is_empty() {
                    return Ok(tls.input.drain(..maximum.min(tls.input.len())).collect());
                }
                if !tls.read_more(BUFFER)? {
                    http_carrier::pause(&self.stop, end)?;
                }
            }
        }
        if let Some(data) = self.packet_pending.take() {
            if data.len() > maximum {
                self.packet_pending = Some(data);
                return Err(Error::new(10, "proxy receive capacity too small"));
            }
            return Ok(data);
        }
        let mut controls = 0;
        loop {
            if let Some((kind, body)) = self.next(end)? {
                controls += 1;
                ensure(controls <= 64, 7, "proxy control capsule budget")?;
                match kind {
                    0 => {
                        if let Some(data) = capsule::datagram(&body)? {
                            ensure(
                                data.len() <= if self.mode == Mode::Udp { 65527 } else { 65535 },
                                7,
                                "oversized proxy datagram",
                            )?;
                            if self.mode == Mode::Ip {
                                let (s, d, p) = proxy_ip::packet(data)?;
                                ensure(
                                    self.requested_protocol
                                        .is_none_or(|v| v == p || p == 1 || p == 58)
                                        && self.assignments.iter().any(|n| n.contains(d))
                                        && proxy_ip::route_permits(&self.routes, s, p)?,
                                    16,
                                    "IP reply outside assigned addresses or route grants",
                                )?;
                            }
                            if data.len() > maximum {
                                self.packet_pending = Some(data.to_vec());
                                return Err(Error::new(10, "proxy receive capacity too small"));
                            }
                            return Ok(data.to_vec());
                        }
                    }
                    1 if self.mode == Mode::Ip => {
                        self.assignments = proxy_ip::assignments(&body, false)?
                            .into_iter()
                            .map(|(_, n)| n)
                            .collect();
                    }
                    3 if self.mode == Mode::Ip => {
                        proxy_ip::routes(&body)?;
                        self.routes = body;
                    }
                    2 if self.mode == Mode::Ip => {
                        return Err(Error::new(6, "reciprocal address requests unsupported"))
                    }
                    _ => {}
                }
            }
        }
    }
    pub fn probe_udp(config: &ClientConfig, target: &str, bytes: &[u8]) -> Result<Vec<u8>> {
        let mut t = Self::connect(config, Mode::Udp, target, None)?;
        t.send(bytes)?;
        t.receive(65535)
    }
}
pub struct Server {
    pub address: SocketAddr,
    stop: Arc<AtomicBool>,
    worker: Option<JoinHandle<()>>,
    counters: Arc<Counters>,
}
impl Drop for Server {
    fn drop(&mut self) {
        self.stop();
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
impl Server {
    pub fn start(config: ServerConfig) -> Result<Self> {
        let device = config.ip.as_ref().map(Device::open).transpose()?;
        Self::start_device(config, device)
    }
    fn start_device(config: ServerConfig, device: Option<Device>) -> Result<Self> {
        config.policy.limits.validate()?;
        ensure(
            config.policy.limits.sessions <= 64,
            4,
            "proxy maximum is sixty-four concurrent sessions",
        )?;
        let secret = Arc::new(token(&config.token)?);
        let stop = Arc::new(AtomicBool::new(false));
        let mut listener = Listener::bind(
            config.bind,
            Kind::Tls,
            &config.certificate,
            &config.key,
            config.policy.limits.sessions,
            stop.clone(),
        )?;
        let address = listener.address;
        let counters = Arc::new(Counters::default());
        let ip = device.map(|d| Arc::new(Mutex::new(d)));
        let s = stop.clone();
        let c = counters.clone();
        let config = Arc::new(config);
        let worker = thread::spawn(move || {
            let mut workers: Vec<JoinHandle<()>> = Vec::new();
            while !s.load(Ordering::Acquire) {
                let mut n = 0;
                while n < workers.len() {
                    if workers[n].is_finished() {
                        let _ = workers.swap_remove(n).join();
                    } else {
                        n += 1;
                    }
                }
                match listener.accept() {
                    Ok(Some(mut channel)) => {
                        c.accepted.fetch_add(1, Ordering::Relaxed);
                        if c.active.load(Ordering::Acquire) >= config.policy.limits.sessions as u64
                            || c.handshakes.load(Ordering::Acquire)
                                >= config.policy.limits.handshakes as u64
                        {
                            c.denied.fetch_add(1, Ordering::Relaxed);
                            continue;
                        }
                        c.active.fetch_add(1, Ordering::AcqRel);
                        let active = Active(c.clone());
                        c.handshakes.fetch_add(1, Ordering::AcqRel);
                        let handshake = Handshake(c.clone());
                        let cfg = config.clone();
                        let key = secret.clone();
                        let counts = c.clone();
                        let cancelled = s.clone();
                        let device = ip.clone();
                        workers.push(thread::spawn(move || {
                            let _active = active;
                            channel.set_cancel(cancelled.clone());
                            if let Err(e) = serve(
                                &mut channel,
                                &cfg,
                                &key.0,
                                device.as_ref(),
                                &cancelled,
                                &counts,
                                handshake,
                            ) {
                                if e.code != 2 && e.code != 9 {
                                    counts.failed.fetch_add(1, Ordering::Relaxed);
                                }
                            }
                        }));
                    }
                    Ok(None) => listener.pause(),
                    Err(_) => {
                        c.failed.fetch_add(1, Ordering::Relaxed);
                        listener.pause();
                    }
                }
            }
            for worker in workers {
                let _ = worker.join();
            }
        });
        Ok(Self {
            address,
            stop,
            worker: Some(worker),
            counters,
        })
    }
    pub fn statistics(&self) -> Statistics {
        self.counters.snapshot()
    }
    pub fn stop(&self) {
        self.stop.store(true, Ordering::Release);
    }
}
enum Endpoint {
    Tcp(TcpStream),
    Udp(UdpSocket),
}
fn response(
    tls: &mut crate::transport::Tls,
    status: &str,
    end: Instant,
    stop: &AtomicBool,
) -> Result<()> {
    let extra = if status.starts_with("407") {
        "Proxy-Authenticate: Bearer realm=\"dlm\"\r\n"
    } else {
        ""
    };
    let r = format!("HTTP/1.1 {status}\r\n{extra}Content-Length: 0\r\nConnection: close\r\n\r\n");
    tls.write_all(r.as_bytes(), end, stop)
}
fn serve(
    channel: &mut Channel,
    config: &ServerConfig,
    secret: &[u8],
    ip: Option<&Arc<Mutex<Device>>>,
    stop: &AtomicBool,
    counters: &Counters,
    handshake: Handshake,
) -> Result<()> {
    let end = deadline(config.policy.limits.handshake_ms)?;
    unsafe { ffi::dgn_tls_http1(channel.tls_mut()?.raw.as_ptr()) }.checked()?;
    channel.handshake(end)?;
    let tls = channel.tls_mut()?;
    let head = match tls.read_head(end, stop) {
        Ok(h) => h,
        Err(e) => {
            let _ = response(tls, "400 Bad Request", end, stop);
            return Err(e);
        }
    };
    if !equal(head.get("proxy-authorization").as_bytes(), secret) {
        counters.denied.fetch_add(1, Ordering::Relaxed);
        response(tls, "407 Proxy Authentication Required", end, stop)?;
        return Err(Error::new(16, "proxy authorization denied"));
    }
    let prepare = (|| -> Result<(Mode, Option<Endpoint>, Scope)> {
        head.no_content()?;
        http_carrier::authority(head.get("host"))?;
        let p: Vec<_> = head.start.split(' ').collect();
        ensure(
            p.len() == 3 && p[2] == "HTTP/1.1",
            6,
            "proxy requires HTTP/1.1",
        )?;
        if p[0] == "CONNECT" {
            http_carrier::authority(p[1])?;
            let address = config.policy.resolve(TargetProtocol::Tcp, p[1])?[0];
            let stream = TcpStream::connect_timeout(
                &address,
                Duration::from_millis(config.policy.limits.request_ms as u64),
            )?;
            stream.set_nonblocking(true)?;
            stream.set_nodelay(true)?;
            return Ok((Mode::Tcp, Some(Endpoint::Tcp(stream)), Scope::default()));
        }
        ensure(
            p[0] == "GET"
                && head.token("connection", "upgrade")
                && head
                    .get("capsule-protocol")
                    .split(';')
                    .next()
                    .unwrap_or("")
                    .trim()
                    == "?1",
            11,
            "malformed MASQUE request",
        )?;
        let path = target_path(p[1], head.get("host"))?;
        if head.get("upgrade").eq_ignore_ascii_case("connect-udp") {
            let target = udp_target(path)?;
            let address = config.policy.resolve(TargetProtocol::Udp, &target)?[0];
            let bind = if address.is_ipv4() {
                "0.0.0.0:0"
            } else {
                "[::]:0"
            };
            let socket = UdpSocket::bind(bind)?;
            socket.connect(address)?;
            socket.set_nonblocking(true)?;
            dont_fragment(&socket, address.is_ipv4())?;
            return Ok((Mode::Udp, Some(Endpoint::Udp(socket)), Scope::default()));
        }
        ensure(
            head.get("upgrade").eq_ignore_ascii_case("connect-ip"),
            6,
            "proxy upgrade unsupported",
        )?;
        ensure(
            ip.is_some() && config.ip.is_some(),
            6,
            "CONNECT-IP requires a configured packet device and separate IP policy",
        )?;
        let f: Vec<_> = path.split('/').collect();
        ensure(
            f.len() == 7 && f[..4] == ["", ".well-known", "masque", "ip"] && f[6].is_empty(),
            11,
            "invalid CONNECT-IP URI",
        )?;
        let scope = Scope::parse(f[4], f[5])?;
        config.ip.as_ref().unwrap().advertisement(&scope)?;
        Ok((Mode::Ip, None, scope))
    })();
    let (mode, mut endpoint, scope) = match prepare {
        Ok(v) => v,
        Err(e) => {
            counters.denied.fetch_add(1, Ordering::Relaxed);
            let status = match e.code {
                6 => "501 Not Implemented",
                16 => "403 Forbidden",
                4 | 11 => "400 Bad Request",
                _ => "502 Bad Gateway",
            };
            let _ = response(tls, status, end, stop);
            return Err(e);
        }
    };
    let mut device = if mode == Mode::Ip {
        match ip.unwrap().try_lock() {
            Ok(d) => Some(d),
            Err(_) => {
                response(tls, "503 Service Unavailable", end, stop)?;
                return Err(Error::new(
                    7,
                    "IP interface already assigned to another tunnel",
                ));
            }
        }
    } else {
        None
    };
    let reply = if mode == Mode::Tcp {
        "HTTP/1.1 200 Connection Established\r\n\r\n".to_owned()
    } else {
        let upgrade = if mode == Mode::Udp {
            "connect-udp"
        } else {
            "connect-ip"
        };
        format!("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: {upgrade}\r\nCapsule-Protocol: ?1\r\n\r\n")
    };
    tls.write_all(reply.as_bytes(), end, stop)?;
    counters.authenticated.fetch_add(1, Ordering::Relaxed);
    drop(handshake);
    if mode == Mode::Udp {
        counters.udp.fetch_add(1, Ordering::Relaxed);
    }
    if mode == Mode::Ip {
        counters.ip.fetch_add(1, Ordering::Relaxed);
        let ip = config.ip.as_ref().unwrap();
        tls.write_all(&ip.assignment(&[])?, end, stop)?;
        tls.write_all(&ip.advertisement(&scope)?, end, stop)?;
    }
    let mut decoder = capsule::Decoder::default();
    let mut pending: Option<Vec<u8>> = None;
    let mut requests = std::collections::BTreeSet::new();
    let mut active = Instant::now();
    let mut total = 0u64;
    let mut read_eof = false;
    let mut half_closed = false;
    let mut receive_buffer = vec![0; 65535];
    while !stop.load(Ordering::Acquire) {
        ensure(
            active.elapsed()
                < Duration::from_millis(config.policy.limits.idle_ms.max(if mode == Mode::Udp {
                    120000
                } else {
                    1
                }) as u64),
            3,
            "proxy tunnel idle deadline expired",
        )?;
        let mut progress = false;
        if pending.is_none() {
            if mode == Mode::Tcp {
                if !tls.input.is_empty() {
                    pending = Some(std::mem::take(&mut tls.input));
                }
            } else {
                for _ in 0..64 {
                    let Some((kind, body)) = decoder.next(&mut tls.input)? else {
                        break;
                    };
                    progress = true;
                    if kind == 0 {
                        if let Some(packet) = capsule::datagram(&body)? {
                            ensure(
                                packet.len() <= if mode == Mode::Udp { 65527 } else { 65535 },
                                7,
                                "oversized MASQUE packet",
                            )?;
                            if mode == Mode::Ip {
                                config.ip.as_ref().unwrap().permits(packet, true, &scope)?;
                            }
                            pending = Some(packet.to_vec());
                            break;
                        }
                    } else if mode == Mode::Ip {
                        match kind {
                            1 => {
                                ensure(
                                    proxy_ip::assignments(&body, false)?.is_empty(),
                                    16,
                                    "client cannot assign proxy addresses",
                                )?;
                            }
                            2 => {
                                let values = proxy_ip::assignments(&body, true)?;
                                for (id, _) in &values {
                                    ensure(
                                        requests.len() < 64 && requests.insert(*id),
                                        11,
                                        "reused or excessive IP address request IDs",
                                    )?;
                                }
                                tls.write_all(
                                    &config.ip.as_ref().unwrap().assignment(&values)?,
                                    deadline(config.policy.limits.request_ms)?,
                                    stop,
                                )?;
                            }
                            3 => {
                                proxy_ip::routes(&body)?;
                                ensure(
                                    body.is_empty(),
                                    16,
                                    "client cannot advertise proxy routes",
                                )?;
                            }
                            _ => {}
                        }
                    }
                }
            }
        }
        if let Some(bytes) = pending.as_mut() {
            ensure(
                total.saturating_add(bytes.len() as u64) <= config.policy.limits.session_bytes,
                7,
                "proxy incoming session quota exceeded",
            )?;
            let count = match endpoint.as_mut() {
                Some(Endpoint::Tcp(s)) => match s.write(bytes) {
                    Ok(n) => Some(n),
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
                    Err(e) => return Err(e.into()),
                },
                Some(Endpoint::Udp(s)) => match s.send(bytes) {
                    Ok(n) => {
                        ensure(n == bytes.len(), 11, "partial proxied UDP write")?;
                        Some(n)
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
                    Err(e) if e.raw_os_error() == Some(90) || e.raw_os_error() == Some(10040) => {
                        pending = None;
                        counters.denied.fetch_add(1, Ordering::Relaxed);
                        continue;
                    }
                    Err(e) => return Err(e.into()),
                },
                None => {
                    if device.as_mut().unwrap().send(bytes)? {
                        Some(bytes.len())
                    } else {
                        None
                    }
                }
            };
            if let Some(count) = count {
                ensure(
                    count > 0 || (mode != Mode::Tcp && bytes.is_empty()),
                    2,
                    "proxy destination closed while writing",
                )?;
                counters.incoming.fetch_add(count as u64, Ordering::Relaxed);
                total = total.saturating_add(count as u64);
                bytes.drain(..count);
                if bytes.is_empty() {
                    pending = None;
                }
                progress = true;
            }
        }
        if read_eof && pending.is_none() && !half_closed {
            if let Some(Endpoint::Tcp(s)) = &endpoint {
                s.shutdown(Shutdown::Write)?;
                half_closed = true;
            } else {
                decoder.finish(&tls.input)?;
                break;
            }
        }
        let packet = match endpoint.as_mut() {
            Some(Endpoint::Tcp(s)) => match s.read(&mut receive_buffer) {
                Ok(0) => break,
                Ok(n) => Some(receive_buffer[..n].to_vec()),
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
                Err(e) => return Err(e.into()),
            },
            Some(Endpoint::Udp(s)) => match s.recv(&mut receive_buffer) {
                Ok(n) => Some(receive_buffer[..n].to_vec()),
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
                Err(e) => return Err(e.into()),
            },
            None => device.as_mut().unwrap().receive()?,
        };
        if let Some(bytes) = packet {
            if mode == Mode::Ip {
                config.ip.as_ref().unwrap().permits(&bytes, false, &scope)?;
            }
            let wire = if mode == Mode::Tcp {
                bytes.clone()
            } else {
                capsule::data(&bytes)?
            };
            ensure(
                total.saturating_add(bytes.len() as u64) <= config.policy.limits.session_bytes,
                7,
                "proxy outgoing session quota exceeded",
            )?;
            counters.charge(wire.len(), config.policy.limits.bytes_per_second)?;
            tls.write_all(&wire, deadline(config.policy.limits.request_ms)?, stop)?;
            total = total.saturating_add(bytes.len() as u64);
            counters
                .outgoing
                .fetch_add(bytes.len() as u64, Ordering::Relaxed);
            progress = true;
        }
        ensure(
            total <= config.policy.limits.session_bytes,
            7,
            "proxy session byte quota exceeded",
        )?;
        if !read_eof && tls.input.len() < BUFFER / 2 {
            let before = tls.input.len();
            match tls.read_more(BUFFER) {
                Ok(got) => {
                    counters.charge(
                        tls.input.len() - before,
                        config.policy.limits.bytes_per_second,
                    )?;
                    progress |= got;
                }
                Err(e) if e.code == 2 => {
                    read_eof = true;
                    progress = true;
                }
                Err(e) => return Err(e),
            }
        }
        if progress {
            active = Instant::now();
        } else {
            thread::sleep(Duration::from_millis(1));
        }
    }
    Ok(())
}
fn dont_fragment(socket: &UdpSocket, v4: bool) -> Result<()> {
    #[cfg(unix)]
    let descriptor = {
        use std::os::fd::AsRawFd;
        socket.as_raw_fd() as i64
    };
    #[cfg(windows)]
    let descriptor = {
        use std::os::windows::io::AsRawSocket;
        socket.as_raw_socket() as i64
    };
    #[cfg(not(any(unix, windows)))]
    {
        let _ = (socket, v4);
        return Err(Error::new(6, "proxy UDP platform unsupported"));
    }
    #[cfg(any(unix, windows))]
    {
        let result = unsafe { ffi::dgn_udp_dont_fragment(descriptor, if v4 { 4 } else { 6 }) };
        if result.status == 6 {
            return Ok(());
        }
        result.checked()?;
        Ok(())
    }
}
pub(crate) struct Relay {
    pub address: SocketAddr,
    pub bound: SocketAddr,
    stop: Arc<AtomicBool>,
    worker: Option<JoinHandle<()>>,
    error: Arc<Mutex<Option<Error>>>,
}
impl Drop for Relay {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(w) = self.worker.take() {
            let _ = w.join();
        }
    }
}
impl Relay {
    pub fn start(
        config: &ClientConfig,
        target: SocketAddr,
        source: Option<SocketAddr>,
    ) -> Result<Self> {
        let mut tunnel = Tunnel::connect(config, Mode::Udp, &target.to_string(), source)?;
        let bound = tunnel.local();
        tunnel.timeout = tunnel.timeout.min(1000);
        let socket = UdpSocket::bind("127.0.0.1:0")?;
        socket.set_nonblocking(true)?;
        let address = socket.local_addr()?;
        let stop = Arc::new(AtomicBool::new(false));
        let error = Arc::new(Mutex::new(None));
        let e = error.clone();
        let s = stop.clone();
        let mut peer = None;
        let worker = thread::spawn(move || {
            let result = (|| -> Result<()> {
                let mut input = vec![0; 65535];
                loop {
                    let mut progress = false;
                    for _ in 0..16 {
                        match socket.recv_from(&mut input) {
                            Ok((n, from)) => {
                                if peer.is_none() {
                                    peer = Some(from);
                                }
                                if peer == Some(from) {
                                    tunnel.send(&input[..n])?;
                                    progress = true;
                                }
                            }
                            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                            Err(e) => return Err(e.into()),
                        }
                    }
                    if s.load(Ordering::Acquire) {
                        break;
                    }
                    let end = Instant::now() + Duration::from_millis(2);
                    let next = match tunnel.next(end) {
                        Ok(v) => v,
                        Err(e) if e.code == 3 => None,
                        Err(e) => return Err(e),
                    };
                    if let Some((kind, body)) = next {
                        if kind == 0 {
                            if let Some(packet) = capsule::datagram(&body)? {
                                ensure(packet.len() <= 65527, 7, "oversized UDP proxy reply")?;
                                if let Some(peer) = peer {
                                    let n = socket.send_to(packet, peer)?;
                                    ensure(
                                        n == packet.len(),
                                        11,
                                        "partial local QUIC relay write",
                                    )?;
                                    progress = true;
                                }
                            }
                        }
                    }
                    if !progress {
                        thread::sleep(Duration::from_millis(1));
                    }
                }
                Ok(())
            })();
            if let Err(failure) = result {
                if failure.code != 9 {
                    if let Ok(mut error) = e.lock() {
                        *error = Some(failure);
                    }
                }
            }
        });
        Ok(Self {
            address,
            bound,
            stop,
            worker: Some(worker),
            error,
        })
    }
    pub fn check(&self) -> Result<()> {
        if let Some(e) = self
            .error
            .lock()
            .map_err(|_| Error::new(5, "proxy relay error lock"))?
            .clone()
        {
            Err(e)
        } else {
            Ok(())
        }
    }
}
pub fn capabilities() -> String {
    format!("{{\"format\":\"DLN-CARRIERS-v1\",\"version\":\"{}\",\"platform\":\"{}\",\"scope\":\"compiled implementation; readiness requires authenticated runtime probe\",\"direct\":[\"tls\",\"quic-stream\",\"quic-datagram\",\"websocket\",\"https-connect\"],\"websocket_extensions\":[],\"proxy\":{{\"http_versions\":[1],\"connect_tcp\":true,\"connect_udp\":true,\"connect_ip\":\"explicit IP policy and existing packet device required\",\"http2\":false,\"http3\":false,\"turn_tls\":false,\"authentication\":\"TLS and configured bearer credential\"}},\"native_correctness_proved\":false}}",env!("CARGO_PKG_VERSION"),std::env::consts::OS)
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::{collections::BTreeSet, fs, path::Path};
    struct Work(PathBuf);
    impl Work {
        fn new() -> Self {
            let p = std::env::temp_dir().join(format!(
                "dlm-proxy-{}-{}",
                std::process::id(),
                hex(&crypto::random::<8>().unwrap())
            ));
            fs::create_dir(&p).unwrap();
            Self(p)
        }
    }
    impl Drop for Work {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }
    fn ip_config() -> IpConfig {
        IpConfig {
            interface: "packet-test".into(),
            library: String::new(),
            clients: vec![
                Network::parse("10.1.0.2/32").unwrap(),
                Network::parse("fd00::2/128").unwrap(),
            ],
            routes: vec![
                Network::parse("192.0.2.0/24").unwrap(),
                Network::parse("2001:db8::/32").unwrap(),
            ],
            protocols: BTreeSet::from([17]),
        }
    }
    fn checksum(b: &[u8]) -> u16 {
        let mut sum = 0u32;
        for p in b.chunks(2) {
            sum += ((p[0] as u32) << 8) | p.get(1).copied().unwrap_or(0) as u32;
        }
        while sum > 65535 {
            sum = (sum & 65535) + (sum >> 16);
        }
        !(sum as u16)
    }
    fn packet(v6: bool, reply: bool) -> Vec<u8> {
        let (a, b) = if v6 {
            ("fd00::2", "2001:db8::9")
        } else {
            ("10.1.0.2", "192.0.2.9")
        };
        let (a, b) = if reply { (b, a) } else { (a, b) };
        let a = Network {
            address: a.parse().unwrap(),
            prefix: 0,
        }
        .bytes();
        let b = Network {
            address: b.parse().unwrap(),
            prefix: 0,
        }
        .bytes();
        let n = 11;
        let mut udp = vec![0; n];
        udp[..2].copy_from_slice(&19001u16.to_be_bytes());
        udp[2..4].copy_from_slice(&19002u16.to_be_bytes());
        udp[4..6].copy_from_slice(&(n as u16).to_be_bytes());
        udp[8..].copy_from_slice(b"A\0\xff");
        let mut pseudo = a.clone();
        pseudo.extend_from_slice(&b);
        if v6 {
            pseudo.extend_from_slice(&(n as u32).to_be_bytes());
            pseudo.extend_from_slice(&[0, 0, 0, 17]);
        } else {
            pseudo.extend_from_slice(&[0, 17]);
            pseudo.extend_from_slice(&(n as u16).to_be_bytes());
        }
        pseudo.extend_from_slice(&udp);
        let sum = checksum(&pseudo);
        udp[6..8].copy_from_slice(&if sum == 0 { 65535 } else { sum }.to_be_bytes());
        let mut ip = vec![0; if v6 { 40 } else { 20 }];
        if v6 {
            ip[0] = 0x60;
            ip[4..6].copy_from_slice(&(n as u16).to_be_bytes());
            ip[6] = 17;
            ip[7] = 64;
            ip[8..24].copy_from_slice(&a);
            ip[24..40].copy_from_slice(&b);
        } else {
            ip[0] = 0x45;
            ip[2..4].copy_from_slice(&((20 + n) as u16).to_be_bytes());
            ip[8] = 64;
            ip[9] = 17;
            ip[12..16].copy_from_slice(&a);
            ip[16..20].copy_from_slice(&b);
            let sum = checksum(&ip);
            ip[10..12].copy_from_slice(&sum.to_be_bytes());
        }
        ip.extend(udp);
        ip
    }
    #[test]
    fn ip_policy_assignments_routes_and_malformed_packets_are_bounded() {
        let c = ip_config();
        c.validate().unwrap();
        let scope = Scope::default();
        for family in [false, true] {
            let p = packet(family, false);
            c.permits(&p, true, &scope).unwrap();
            assert!(c.permits(&p, false, &scope).is_err());
            c.permits(&packet(family, true), false, &scope).unwrap();
            let encoded = c.advertisement(&scope).unwrap();
            let (_, k) = capsule::integer(&encoded).unwrap();
            let (_, n) = capsule::integer(&encoded[k..]).unwrap();
            let (_, d, p) = proxy_ip::packet(&p).unwrap();
            assert!(proxy_ip::route_permits(&encoded[k + n..], d, p).unwrap());
        }
        assert!(!Network {
            address: "127.0.0.0".parse().unwrap(),
            prefix: 128
        }
        .contains("127.0.0.1".parse().unwrap()));
        let mut rng = 148149u64;
        for n in 0..512 {
            let mut b = vec![0; n];
            for v in &mut b {
                rng ^= rng << 13;
                rng ^= rng >> 7;
                rng ^= rng << 17;
                *v = rng as u8;
            }
            let _ = proxy_ip::packet(&b);
        }
        assert!(proxy_ip::assignments(&[0, 4, 0, 0, 0, 0, 0], true).is_err());
        assert!(proxy_ip::routes(&[4, 192, 0, 2, 10, 192, 0, 2, 1, 0]).is_err());
        assert!(Network::parse("192.0.2.1/24").is_err());
        assert!(c
            .advertisement(&Scope::parse("198.51.100.0%2F24", "17").unwrap())
            .is_err());
    }
    #[cfg(unix)]
    #[test]
    fn connect_ip_transfers_valid_ipv4_ipv6_via_an_adopted_packet_device_and_refuses_replayed_address_requests(
    ) {
        use std::os::{fd::IntoRawFd, unix::net::UnixDatagram};
        let w = Work::new();
        let credentials = w.0.join("proxy.key");
        journal::create_key(&credentials).unwrap();
        let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples/native/fixtures");
        let (device, network) = UnixDatagram::pair().unwrap();
        network
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        let device = Device::adopt(device.into_raw_fd() as i64).unwrap();
        let mut policy = Policy::new();
        policy.limits.sessions = 4;
        policy.limits.handshakes = 4;
        let config = ServerConfig {
            bind: "127.0.0.1:0".parse().unwrap(),
            certificate: fixtures
                .join("quic-test-server.pem")
                .to_string_lossy()
                .into_owned(),
            key: fixtures
                .join("quic-test-key.pem")
                .to_string_lossy()
                .into_owned(),
            token: credentials.clone(),
            policy,
            ip: Some(ip_config()),
        };
        let server = Server::start_device(config, Some(device)).unwrap();
        let cfg = ClientConfig {
            address: server.address,
            hostname: "localhost".into(),
            ca: fixtures
                .join("quic-test-ca.pem")
                .to_string_lossy()
                .into_owned(),
            token: credentials,
            timeout_ms: 3000,
            http_version: 1,
        };
        let mut client = Tunnel::connect(&cfg, Mode::Ip, "*,17", None).unwrap();
        assert_eq!(client.assignments.len(), 2);
        assert!(Tunnel::connect(&cfg, Mode::Ip, "*,17", None).is_err());
        let worker = thread::spawn(move || {
            let mut b = vec![0; 65535];
            for v6 in [false, true] {
                let n = network.recv(&mut b).unwrap();
                assert_eq!(&b[..n], packet(v6, false));
                network.send(&packet(v6, true)).unwrap();
            }
        });
        let mut expected = 0;
        for v6 in [false, true] {
            let p = packet(v6, false);
            expected += p.len();
            client.send(&p).unwrap();
            assert_eq!(client.receive(65535).unwrap(), packet(v6, true));
        }
        worker.join().unwrap();
        let mut denied = packet(false, false);
        denied[12..16].copy_from_slice(&[10, 1, 0, 3]);
        assert_eq!(client.send(&denied).unwrap_err().code, 16);
        let wire = capsule::encode(2, &[3, 4, 0, 0, 0, 0, 0]).unwrap();
        let end = deadline(3000).unwrap();
        client
            .channel
            .tls_mut()
            .unwrap()
            .write_all(&wire, end, &client.stop)
            .unwrap();
        let r = loop {
            if let Some(v) = client.next(end).unwrap() {
                break v;
            }
        };
        assert_eq!(r.0, 1);
        assert_eq!(proxy_ip::assignments(&r.1, false).unwrap()[0].0, 3);
        client
            .channel
            .tls_mut()
            .unwrap()
            .write_all(&wire, end, &client.stop)
            .unwrap();
        loop {
            match client.next(end) {
                Ok(_) => {}
                Err(_) => break,
            }
        }
        assert_eq!(server.statistics().incoming, expected as u64);
        assert_eq!(server.statistics().outgoing, expected as u64);
        assert_eq!(server.statistics().ip, 1);
        println!("{{\"case\":\"connect_ip_adopted_packet_device\",\"status\":\"PASS\",\"ipv4\":true,\"ipv6\":true,\"native_privileged_tun\":false}}");
    }
}
