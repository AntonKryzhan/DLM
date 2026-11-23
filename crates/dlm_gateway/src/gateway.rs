use crate::{
    crypto, deadline, ensure,
    identity::{read_bounded, Identity, Record},
    journal::{Journal, State},
    now,
    policy::{Limits, Policy, TargetProtocol},
    protocol::{self, Decoder, Peer, Session},
    transport::{Channel, Kind, Listener},
    wait, Error, Result,
};
use std::{
    collections::{BTreeMap, VecDeque},
    io::{Read, Write},
    net::{Shutdown, SocketAddr, TcpStream, UdpSocket},
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc, Mutex,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

#[derive(Clone, Debug)]
pub struct ServerConfig {
    pub bind: SocketAddr,
    pub advertise: Option<SocketAddr>,
    pub kind: Kind,
    pub identity: PathBuf,
    pub certificate: PathBuf,
    pub key: PathBuf,
    pub locator_output: Option<PathBuf>,
    pub policy: Policy,
    pub spool: Option<(PathBuf, PathBuf)>,
}
#[derive(Default)]
pub struct Counters {
    accepted: AtomicU64,
    authenticated: AtomicU64,
    denied: AtomicU64,
    failed: AtomicU64,
    requests: AtomicU64,
    forwarded: AtomicU64,
    returned: AtomicU64,
    active: AtomicU64,
    handshakes: AtomicU64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Statistics {
    pub accepted: u64,
    pub authenticated: u64,
    pub denied: u64,
    pub failed: u64,
    pub requests: u64,
    pub forwarded: u64,
    pub returned: u64,
    pub active: u64,
    pub handshakes: u64,
}
impl Counters {
    fn snapshot(&self) -> Statistics {
        Statistics {
            accepted: self.accepted.load(Ordering::Relaxed),
            authenticated: self.authenticated.load(Ordering::Relaxed),
            denied: self.denied.load(Ordering::Relaxed),
            failed: self.failed.load(Ordering::Relaxed),
            requests: self.requests.load(Ordering::Relaxed),
            forwarded: self.forwarded.load(Ordering::Relaxed),
            returned: self.returned.load(Ordering::Relaxed),
            active: self.active.load(Ordering::Relaxed),
            handshakes: self.handshakes.load(Ordering::Relaxed),
        }
    }
}
#[derive(Default)]
struct ClientBudget {
    active: usize,
    epoch: u64,
    second: u64,
    bytes: u64,
}
type Budgets = Arc<Mutex<BTreeMap<[u8; 32], ClientBudget>>>;
struct Permit {
    budgets: Budgets,
    root: [u8; 32],
    statistics: Arc<Counters>,
}
impl Drop for Permit {
    fn drop(&mut self) {
        if let Ok(mut budgets) = self.budgets.lock() {
            if let Some(v) = budgets.get_mut(&self.root) {
                v.active = v.active.saturating_sub(1);
            }
        }
        self.statistics.active.fetch_sub(1, Ordering::AcqRel);
    }
}
fn permit(
    peer: &Peer,
    limits: &Limits,
    budgets: &Budgets,
    statistics: &Arc<Counters>,
) -> Result<Permit> {
    let mut clients = budgets
        .lock()
        .map_err(|_| Error::new(5, "client quota lock failed"))?;
    let state = clients.entry(peer.root).or_default();
    ensure(
        state.active < limits.per_client && peer.epoch >= state.epoch,
        16,
        "client concurrency or key epoch denied",
    )?;
    state.epoch = peer.epoch;
    state.active += 1;
    statistics.active.fetch_add(1, Ordering::AcqRel);
    Ok(Permit {
        budgets: budgets.clone(),
        root: peer.root,
        statistics: statistics.clone(),
    })
}
fn charge(peer: &Peer, count: usize, limits: &Limits, budgets: &Budgets) -> Result<()> {
    let mut clients = budgets
        .lock()
        .map_err(|_| Error::new(5, "client byte quota lock failed"))?;
    let state = clients
        .get_mut(&peer.root)
        .ok_or(Error::new(16, "client quota state absent"))?;
    ensure(
        peer.epoch >= state.epoch,
        16,
        "client key epoch revoked by a newer session",
    )?;
    let second = now();
    if state.second != second {
        state.second = second;
        state.bytes = 0;
    }
    ensure(
        (count as u64) <= limits.bytes_per_second.saturating_sub(state.bytes),
        7,
        "client rate quota exceeded",
    )?;
    state.bytes += count as u64;
    Ok(())
}

pub(crate) struct Shared {
    context: Arc<crate::service::Context>,
    budgets: Budgets,
    statistics: Arc<Counters>,
    policy: [u8; 32],
}
impl Shared {
    fn new(config: &ServerConfig) -> Result<Arc<Self>> {
        Ok(Arc::new(Self {
            context: Arc::new(crate::service::Context::new(
                &config.identity,
                config.spool.as_ref(),
                &config.policy.limits,
            )?),
            budgets: Arc::new(Mutex::new(BTreeMap::new())),
            statistics: Arc::new(Counters::default()),
            policy: config.policy.digest()?,
        }))
    }
}
pub struct GatewayGroup {
    pub gateways: Vec<Gateway>,
}
impl GatewayGroup {
    pub fn start(configs: Vec<ServerConfig>) -> Result<Self> {
        ensure(
            !configs.is_empty() && configs.len() <= 6,
            4,
            "gateway listener group capacity",
        )?;
        let shared = Shared::new(&configs[0])?;
        let output = configs[0].locator_output.clone();
        ensure(
            configs.iter().all(|c| c.spool == configs[0].spool),
            4,
            "gateway group storage configuration differs",
        )?;
        let mut gateways = Vec::new();
        for (index, mut config) in configs.into_iter().enumerate() {
            if index > 0 {
                config.locator_output = None;
            }
            gateways.push(Gateway::start_shared(config, shared.clone())?);
        }
        if let Some(path) = output {
            shared.context.publish(&path)?;
        }
        Ok(Self { gateways })
    }
    pub fn statistics(&self) -> Statistics {
        self.gateways[0].statistics()
    }
    pub fn stop(&self) {
        for gateway in &self.gateways {
            gateway.stop();
        }
    }
    pub fn addresses(&self) -> Vec<(SocketAddr, Kind)> {
        self.gateways.iter().map(|g| (g.address, g.kind)).collect()
    }
}
pub struct Gateway {
    pub address: SocketAddr,
    pub locator: Vec<u8>,
    pub kind: Kind,
    stop: Arc<AtomicBool>,
    statistics: Arc<Counters>,
    worker: Option<JoinHandle<()>>,
}
impl Drop for Gateway {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
impl Gateway {
    pub fn start(config: ServerConfig) -> Result<Self> {
        let shared = Shared::new(&config)?;
        Self::start_shared(config, shared)
    }
    fn start_shared(config: ServerConfig, shared: Arc<Shared>) -> Result<Self> {
        config.policy.validate()?;
        let mut identity = Identity::load(&config.identity)?.signer()?;
        ensure(
            Identity::load(&config.identity)?.public()?.root == shared.context.root
                && config.policy.digest()? == shared.policy,
            16,
            "gateway group identity/policy mismatch",
        )?;
        let certificate = config
            .certificate
            .to_str()
            .ok_or(Error::new(4, "non-UTF8 certificate path"))?;
        let key = config
            .key
            .to_str()
            .ok_or(Error::new(4, "non-UTF8 certificate key path"))?;
        ensure(
            !read_bounded(&config.certificate, 1048576)?.is_empty()
                && !read_bounded(&config.key, 1048576)?.is_empty(),
            4,
            "empty TLS certificate/key",
        )?;
        let stop = Arc::new(AtomicBool::new(false));
        let mut listener = Listener::bind(
            config.bind,
            config.kind,
            certificate,
            key,
            config.policy.limits.sessions,
            stop.clone(),
        )?;
        let address = listener.address;
        let advertise = config
            .advertise
            .map(|mut a| {
                if a.port() == 0 {
                    a.set_port(address.port())
                }
                a
            })
            .unwrap_or(address);
        ensure(
            !advertise.ip().is_unspecified(),
            4,
            "wildcard listener requires an explicit advertised address",
        )?;
        shared.context.address(advertise, config.kind.profile())?;
        let locator = if let Some(path) = &config.locator_output {
            shared.context.publish(path)?
        } else {
            identity.record(advertise, 1 << (config.kind.profile() - 1), 300)?
        };
        let journal = shared.context.journal.clone();
        let context = shared.context.clone();
        let statistics = shared.statistics.clone();
        let counters = statistics.clone();
        let stopped = stop.clone();
        let budgets = shared.budgets.clone();
        let kind = config.kind;
        let config = Arc::new(config);
        let worker = thread::Builder::new()
            .name("dln-gateway-accept".into())
            .spawn(move || {
                let mut workers: Vec<JoinHandle<()>> = Vec::new();
                let mut last_locator = Instant::now();
                let mut admission = Instant::now();
                let mut tokens = 64usize;
                while !stopped.load(Ordering::Acquire) {
                    let mut i = 0;
                    while i < workers.len() {
                        if workers[i].is_finished() {
                            let w = workers.swap_remove(i);
                            if w.join().is_err() {
                                counters.failed.fetch_add(1, Ordering::Relaxed);
                            }
                        } else {
                            i += 1;
                        }
                    }
                    if admission.elapsed() >= Duration::from_secs(1) {
                        tokens = 64;
                        admission = Instant::now();
                        context.prune();
                    }
                    if last_locator.elapsed() >= Duration::from_secs(120) {
                        if let Some(path) = &config.locator_output {
                            let _ = context.publish(path);
                        }
                        last_locator = Instant::now();
                    }
                    if tokens == 0
                        || workers.len() >= config.policy.limits.sessions
                        || counters.active.load(Ordering::Acquire)
                            + counters.handshakes.load(Ordering::Acquire)
                            >= config.policy.limits.sessions as u64
                        || counters.handshakes.load(Ordering::Acquire)
                            >= config.policy.limits.handshakes as u64
                    {
                        listener.pause();
                        continue;
                    }
                    match listener.accept() {
                        Ok(Some(channel)) => {
                            tokens -= 1;
                            counters.accepted.fetch_add(1, Ordering::Relaxed);
                            counters.handshakes.fetch_add(1, Ordering::AcqRel);
                            let config = config.clone();
                            let budgets = budgets.clone();
                            let stats = counters.clone();
                            let journal = journal.clone();
                            let context = context.clone();
                            let result = thread::Builder::new()
                                .name("dln-gateway-session".into())
                                .spawn(move || {
                                    struct HandshakeGuard(Arc<Counters>);
                                    impl Drop for HandshakeGuard {
                                        fn drop(&mut self) {
                                            self.0.handshakes.fetch_sub(1, Ordering::AcqRel);
                                        }
                                    }
                                    let guard = HandshakeGuard(stats.clone());
                                    let mut admission = None;
                                    let authenticated =
                                        Identity::load(&config.identity).and_then(|identity| {
                                            Session::server(
                                                channel,
                                                &identity,
                                                &config.policy,
                                                advertise,
                                                |peer| {
                                                    admission = Some(permit(
                                                        peer,
                                                        &config.policy.limits,
                                                        &budgets,
                                                        &stats,
                                                    )?);
                                                    Ok(())
                                                },
                                            )
                                        });
                                    drop(guard);
                                    let result = authenticated.and_then(|session| {
                                        let _permit = admission
                                            .take()
                                            .ok_or(Error::new(16, "client admission missing"))?;
                                        stats.authenticated.fetch_add(1, Ordering::Relaxed);
                                        serve_session(
                                            session,
                                            &config.policy,
                                            &budgets,
                                            &stats,
                                            journal,
                                            context,
                                        )
                                    });
                                    if let Err(error) = result {
                                        if matches!(error.code, 12 | 13 | 14 | 16 | 18) {
                                            stats.denied.fetch_add(1, Ordering::Relaxed);
                                        } else if !matches!(error.code, 2 | 3 | 9) {
                                            stats.failed.fetch_add(1, Ordering::Relaxed);
                                        }
                                    }
                                });
                            match result {
                                Ok(worker) => workers.push(worker),
                                Err(_) => {
                                    counters.handshakes.fetch_sub(1, Ordering::AcqRel);
                                    counters.failed.fetch_add(1, Ordering::Relaxed);
                                }
                            }
                        }
                        Ok(None) => listener.pause(),
                        Err(_) => {
                            counters.failed.fetch_add(1, Ordering::Relaxed);
                            listener.pause();
                        }
                    }
                }
                drop(listener);
                for worker in workers {
                    let _ = worker.join();
                }
            })?;
        Ok(Self {
            address,
            locator,
            stop,
            statistics,
            worker: Some(worker),
            kind,
        })
    }
    pub fn statistics(&self) -> Statistics {
        self.statistics.snapshot()
    }
    pub fn stop(&self) {
        self.stop.store(true, Ordering::Release)
    }
}
pub(crate) enum Destination {
    Tcp(TcpStream),
    Udp(UdpSocket),
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Outcome {
    None = 0,
    Opened = 1,
    Forwarded = 2,
    EndpointData = 3,
    DurableCommitted = 4,
    EndOfStream = 5,
    Acknowledged = 6,
}
#[derive(Clone, Debug)]
pub struct Reply {
    pub request: u64,
    pub outcome: Outcome,
    pub value: u64,
    pub data: Vec<u8>,
}
pub(crate) fn response(request: u64, result: Result<(Outcome, u64, Vec<u8>)>) -> Result<Vec<u8>> {
    let (status, outcome, value, data) = match result {
        Ok((o, v, d)) => (0, o as u32, v, d),
        Err(e) => (e.code as u32, 0, e.system as u64, Vec::new()),
    };
    ensure(
        data.len() <= protocol::DATA_MAX,
        7,
        "response payload quota",
    )?;
    let mut bytes = request.to_be_bytes().to_vec();
    bytes.extend_from_slice(&status.to_be_bytes());
    bytes.extend_from_slice(&outcome.to_be_bytes());
    bytes.extend_from_slice(&value.to_be_bytes());
    bytes.extend_from_slice(&(data.len() as u32).to_be_bytes());
    bytes.extend_from_slice(&data);
    Ok(bytes)
}
fn destination_error(_: std::io::Error) -> Error {
    Error::new(17, "destination I/O failed")
}
fn serve_session(
    mut session: Session,
    policy: &Policy,
    budgets: &Budgets,
    statistics: &Arc<Counters>,
    journal: Option<Arc<Mutex<Journal>>>,
    context: Arc<crate::service::Context>,
) -> Result<()> {
    let mut targets = BTreeMap::new();
    let mut next_stream = 1u32;
    let mut next_request = 1u64;
    let mut spent = 0u64;
    let mut cache: VecDeque<([u8; 32], Vec<u8>)> = VecDeque::new();
    loop {
        let end = deadline(policy.limits.idle_ms)?;
        let wire = session.receive_wire(end)?;
        if protocol::unpack(&wire)?.0 == 7 {
            ensure(
                session.minor >= 1,
                6,
                "delivery extension requires protocol minor 1",
            )?;
            let plain = session.decode(&wire, 7)?;
            charge(&session.peer, plain.len() + 96, &policy.limits, budgets)?;
            spent = spent
                .checked_add((plain.len() + 96) as u64)
                .ok_or(Error::new(7, "delivery byte count overflow"))?;
            ensure(
                spent <= policy.limits.session_bytes,
                7,
                "delivery session byte quota",
            )?;
            let ack = context.shard(&session.peer.root, &plain)?;
            let reply = session.encode(8, &ack)?;
            session.send_wire(&reply, deadline(policy.limits.request_ms)?)?;
            continue;
        }
        let hash = crypto::hash(&wire)?;
        if let Some((_, reply)) = cache.iter().find(|(known, _)| known == &hash) {
            session.send_wire(reply, end)?;
            continue;
        }
        let plain = match session.decode(&wire, protocol::REQUEST) {
            Ok(p) => p,
            Err(e) if e.code == 13 => continue,
            Err(e) => return Err(e),
        };
        let mut decoder = Decoder::new(&plain);
        let request = decoder.u64()?;
        ensure(
            request == next_request && request < 1048576,
            18,
            "nonmonotonic request identifier",
        )?;
        next_request += 1;
        let operation = decoder.u32()?;
        let operation_data = decoder.take(plain.len() - 12)?;
        let cost = plain
            .len()
            .checked_add(if matches!(operation, 4 | 7 | 9) {
                session.maximum_data()
            } else {
                0
            })
            .ok_or(Error::new(7, "request cost overflow"))?;
        ensure(
            spent
                .checked_add(cost as u64)
                .is_some_and(|v| v <= policy.limits.session_bytes),
            7,
            "session byte quota exceeded",
        )?;
        charge(&session.peer, cost, &policy.limits, budgets)?;
        spent += cost as u64;
        statistics.requests.fetch_add(1, Ordering::Relaxed);
        let result = if operation >= 11 {
            ensure(
                session.minor >= 1,
                6,
                "gateway extension requires protocol minor 1",
            )?;
            context.rpc(
                &session.peer.root,
                &session.session_id,
                operation,
                operation_data,
                session.maximum_data(),
                policy,
                statistics,
            )
        } else {
            execute(
                operation,
                operation_data,
                &session.peer.root,
                session.maximum_data(),
                policy,
                &mut targets,
                &mut next_stream,
                journal.as_ref(),
                statistics,
            )
        };
        let plain = response(request, result)?;
        let reply = session.encode(protocol::RESPONSE, &plain)?;
        session.send_wire(&reply, deadline(policy.limits.request_ms)?)?;
        cache.push_back((hash, reply));
        if cache.len() > policy.limits.pending {
            cache.pop_front();
        }
    }
}
pub(crate) fn execute(
    operation: u32,
    bytes: &[u8],
    owner: &[u8; 32],
    maximum: usize,
    policy: &Policy,
    targets: &mut BTreeMap<u32, Destination>,
    next: &mut u32,
    journal: Option<&Arc<Mutex<Journal>>>,
    statistics: &Arc<Counters>,
) -> Result<(Outcome, u64, Vec<u8>)> {
    let end = deadline(policy.limits.request_ms)?;
    let mut d = Decoder::new(bytes);
    match operation {
        1 | 2 => {
            ensure(
                targets.len() < policy.limits.streams,
                7,
                "destination stream quota",
            )?;
            let n = d.u16()? as usize;
            let name = std::str::from_utf8(d.take(n)?)
                .map_err(|_| Error::new(4, "invalid destination UTF-8"))?;
            d.end()?;
            let protocol = if operation == 1 {
                TargetProtocol::Tcp
            } else {
                TargetProtocol::Udp
            };
            let addresses = policy.resolve(protocol, name)?;
            let mut target = None;
            for address in addresses {
                if operation == 1 {
                    let remaining = end.saturating_duration_since(Instant::now());
                    if remaining.is_zero() {
                        return Err(Error::new(3, "destination connect deadline"));
                    }
                    if let Ok(socket) = TcpStream::connect_timeout(&address, remaining) {
                        socket.set_nonblocking(true)?;
                        ensure(
                            socket.peer_addr()? == address,
                            16,
                            "destination peer changed after authorisation",
                        )?;
                        target = Some(Destination::Tcp(socket));
                        break;
                    }
                } else {
                    let socket = UdpSocket::bind(if address.is_ipv4() {
                        "0.0.0.0:0"
                    } else {
                        "[::]:0"
                    })?;
                    socket.connect(address)?;
                    socket.set_nonblocking(true)?;
                    target = Some(Destination::Udp(socket));
                    break;
                }
            }
            let target = target.ok_or(Error::new(17, "destination connection unavailable"))?;
            let id = *next;
            *next = next
                .checked_add(1)
                .ok_or(Error::new(7, "stream identifier exhausted"))?;
            targets.insert(id, target);
            Ok((Outcome::Opened, id as u64, Vec::new()))
        }
        3 => {
            let id = d.u32()?;
            let n = d.u32()? as usize;
            ensure(n <= maximum, 7, "write payload quota")?;
            let payload = d.take(n)?;
            d.end()?;
            let target = targets
                .get_mut(&id)
                .ok_or(Error::new(4, "unknown destination stream"))?;
            match target {
                Destination::Tcp(socket) => {
                    let mut at = 0;
                    while at < payload.len() {
                        match socket.write(&payload[at..]) {
                            Ok(0) => return Err(Error::new(17, "destination write returned zero")),
                            Ok(n) => at += n,
                            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => wait(end)?,
                            Err(e) => return Err(destination_error(e)),
                        }
                    }
                }
                Destination::Udp(_) => {
                    return Err(Error::new(4, "TCP write requested for UDP stream"))
                }
            }
            statistics.forwarded.fetch_add(n as u64, Ordering::Relaxed);
            Ok((Outcome::Forwarded, n as u64, Vec::new()))
        }
        4 => {
            let id = d.u32()?;
            let count = d.u32()? as usize;
            d.end()?;
            ensure(count > 0 && count <= maximum, 7, "read payload quota")?;
            let target = targets
                .get_mut(&id)
                .ok_or(Error::new(4, "unknown destination stream"))?;
            let Destination::Tcp(socket) = target else {
                return Err(Error::new(4, "TCP read requested for UDP stream"));
            };
            let mut out = vec![0; count];
            loop {
                match socket.read(&mut out) {
                    Ok(0) => return Ok((Outcome::EndOfStream, 0, Vec::new())),
                    Ok(n) => {
                        out.truncate(n);
                        statistics.returned.fetch_add(n as u64, Ordering::Relaxed);
                        return Ok((Outcome::EndpointData, n as u64, out));
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => wait(end)?,
                    Err(e) => return Err(destination_error(e)),
                }
            }
        }
        5 | 6 => {
            let id = d.u32()?;
            d.end()?;
            if operation == 6 {
                ensure(
                    targets.remove(&id).is_some(),
                    4,
                    "unknown destination stream",
                )?;
            } else {
                let Some(Destination::Tcp(socket)) = targets.get_mut(&id) else {
                    return Err(Error::new(4, "TCP half-close requested for non-TCP stream"));
                };
                socket
                    .shutdown(Shutdown::Write)
                    .map_err(destination_error)?;
            }
            Ok((Outcome::Acknowledged, 0, Vec::new()))
        }
        7 => {
            let id = d.u32()?;
            let n = d.u32()? as usize;
            ensure(n <= maximum, 7, "datagram payload quota")?;
            let payload = d.take(n)?;
            d.end()?;
            let Some(Destination::Udp(socket)) = targets.get_mut(&id) else {
                return Err(Error::new(4, "UDP request on non-UDP stream"));
            };
            loop {
                match socket.send(payload) {
                    Ok(count) => {
                        ensure(count == n, 11, "partial UDP send")?;
                        break;
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => wait(end)?,
                    Err(e) => return Err(destination_error(e)),
                }
            }
            statistics.forwarded.fetch_add(n as u64, Ordering::Relaxed);
            let mut out = vec![0; maximum + 1];
            loop {
                match socket.recv(&mut out) {
                    Ok(count) => {
                        ensure(
                            count <= maximum,
                            10,
                            "destination datagram exceeds receive bound",
                        )?;
                        out.truncate(count);
                        statistics
                            .returned
                            .fetch_add(count as u64, Ordering::Relaxed);
                        return Ok((Outcome::EndpointData, count as u64, out));
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => wait(end)?,
                    Err(e) => return Err(destination_error(e)),
                }
            }
        }
        8 => {
            let id = d.take(16)?.try_into().unwrap();
            let expires = d.u64()?;
            let n = d.u32()? as usize;
            ensure(n <= maximum, 7, "durable message payload quota")?;
            let payload = d.take(n)?;
            d.end()?;
            let journal =
                journal.ok_or(Error::new(6, "durable gateway storage is not configured"))?;
            let mut journal = journal
                .lock()
                .map_err(|_| Error::new(5, "durable storage lock failed"))?;
            let state = journal.put(id, *owner, expires, payload, State::Committed)?;
            ensure(
                matches!(state, State::Committed | State::Acknowledged),
                18,
                "durable receipt has invalid state",
            )?;
            Ok((Outcome::DurableCommitted, n as u64, Vec::new()))
        }
        9 | 10 => {
            let id = d.take(16)?.try_into().unwrap();
            d.end()?;
            let journal =
                journal.ok_or(Error::new(6, "durable gateway storage is not configured"))?;
            let mut journal = journal
                .lock()
                .map_err(|_| Error::new(5, "durable storage lock failed"))?;
            let entry = journal
                .get(&id)
                .ok_or(Error::new(2, "durable message not found"))?;
            ensure(
                entry.owner == *owner && entry.expires > now(),
                16,
                "durable message belongs to another client or expired",
            )?;
            if operation == 9 {
                ensure(
                    entry.payload.len() <= maximum,
                    7,
                    "durable payload exceeds active carrier size",
                )?;
                Ok((
                    Outcome::EndpointData,
                    entry.payload.len() as u64,
                    entry.payload.clone(),
                ))
            } else {
                journal.mark(id, State::Acknowledged)?;
                Ok((Outcome::Acknowledged, 0, Vec::new()))
            }
        }
        _ => Err(Error::new(6, "gateway operation unsupported")),
    }
}

#[derive(Clone, Debug)]
pub struct ClientConfig {
    pub gateway: SocketAddr,
    pub hostname: String,
    pub ca: String,
    pub kind: Kind,
    pub identity: PathBuf,
    pub peer: [u8; 32],
    pub minimum_epoch: u64,
    pub minimum_minor: u16,
    pub maximum_minor: u16,
    pub timeout_ms: u32,
    pub locator: Option<PathBuf>,
}
pub struct Client {
    pub(crate) session: Session,
    next: u64,
    pub(crate) timeout: u32,
    poisoned: bool,
    pub(crate) acks: VecDeque<crate::delivery::BlockAck>,
    relay: Option<crate::proxy::Relay>,
}
impl Client {
    pub fn connect(config: &ClientConfig) -> Result<Self> {
        Self::connect_from(config, None)
    }
    pub fn connect_from(config: &ClientConfig, source: Option<SocketAddr>) -> Result<Self> {
        Self::connect_with_proxy(config, source, None)
    }
    pub fn connect_with_proxy(
        config: &ClientConfig,
        source: Option<SocketAddr>,
        proxy: Option<&crate::proxy::ClientConfig>,
    ) -> Result<Self> {
        if let Some(path) = &config.locator {
            let locator = Record::parse(&read_bounded(path, 572)?)?;
            locator.pinned(&config.peer, config.minimum_epoch)?;
            ensure(
                locator.locators.iter().any(|(address, profiles)| {
                    *address == config.gateway && profiles & (1 << (config.kind.profile() - 1)) != 0
                }),
                16,
                "gateway address/profile absent from signed locator",
            )?;
        }
        let identity = Identity::load(&config.identity)?;
        let relay = if let Some(proxy) = proxy {
            ensure(
                matches!(config.kind, Kind::QuicStream | Kind::QuicDatagram),
                6,
                "MASQUE proxy requires a QUIC gateway carrier",
            )?;
            Some(crate::proxy::Relay::start(proxy, config.gateway, source)?)
        } else {
            None
        };
        let channel = Channel::connect_from(
            relay.as_ref().map(|r| r.address).unwrap_or(config.gateway),
            &config.hostname,
            &config.ca,
            config.kind,
            config.timeout_ms,
            if relay.is_some() { None } else { source },
        )?;
        let session = Session::client(
            channel,
            &identity,
            &config.peer,
            config.minimum_epoch,
            config.minimum_minor,
            config.maximum_minor,
            config.timeout_ms,
        )?;
        Ok(Self {
            session,
            next: 1,
            timeout: config.timeout_ms,
            poisoned: false,
            acks: VecDeque::new(),
            relay,
        })
    }
    pub fn bound_source(&self) -> SocketAddr {
        self.relay
            .as_ref()
            .map(|r| r.bound)
            .unwrap_or_else(|| self.session.channel.local())
    }
    pub fn peer(&self) -> &Peer {
        &self.session.peer
    }
    pub fn maximum_data(&self) -> usize {
        self.session.maximum_data()
    }
    pub fn minor(&self) -> u16 {
        self.session.minor
    }
    pub(crate) fn request(&mut self, operation: u32, body: &[u8]) -> Result<Reply> {
        if let Some(relay) = &self.relay {
            relay.check()?;
        }
        ensure(
            !self.poisoned,
            18,
            "session must reconnect after an unconfirmed request",
        )?;
        let id = self.next;
        let next = id
            .checked_add(1)
            .ok_or(Error::new(7, "request identifier exhausted"))?;
        let mut plain = id.to_be_bytes().to_vec();
        plain.extend_from_slice(&operation.to_be_bytes());
        plain.extend_from_slice(body);
        let wire = self.session.encode(protocol::REQUEST, &plain)?;
        match self.exchange_request(id, &wire) {
            Ok(reply) => {
                self.next = next;
                reply
            }
            Err(error) => {
                self.poisoned = true;
                Err(error)
            }
        }
    }
    fn exchange_request(&mut self, id: u64, wire: &[u8]) -> Result<Result<Reply>> {
        let end = deadline(self.timeout)?;
        loop {
            self.session.send_wire(wire, end)?;
            let until = if self.session.channel.kind == Kind::QuicDatagram {
                end.min(Instant::now() + Duration::from_millis(500))
            } else {
                end
            };
            loop {
                let received = match self.session.receive_wire(until) {
                    Ok(v) => v,
                    Err(e)
                        if e.code == 3
                            && self.session.channel.kind == Kind::QuicDatagram
                            && Instant::now() < end =>
                    {
                        break
                    }
                    Err(e) => return Err(e),
                };
                if protocol::unpack(&received)?.0 == 8 {
                    match self.session.decode(&received, 8) {
                        Ok(plain) => {
                            let ack = crate::delivery::BlockAck::decode(&plain)?;
                            if self.acks.len() >= 64 {
                                self.acks.pop_front();
                            }
                            self.acks.push_back(ack);
                        }
                        Err(e) if e.code == 13 => {}
                        Err(e) => return Err(e),
                    }
                    continue;
                }
                let plain = match self.session.decode(&received, protocol::RESPONSE) {
                    Ok(v) => v,
                    Err(e) if e.code == 13 => continue,
                    Err(e) => return Err(e),
                };
                let mut d = Decoder::new(&plain);
                let response = d.u64()?;
                if response < id {
                    continue;
                }
                ensure(response == id, 11, "response correlation mismatch")?;
                let status = d.u32()? as i32;
                let outcome = d.u32()?;
                let value = d.u64()?;
                let n = d.u32()? as usize;
                let data = d.take(n)?.to_vec();
                d.end()?;
                if status != 0 {
                    return Ok(Err(Error {
                        code: status,
                        system: value as i32,
                        message: "gateway request rejected",
                    }));
                }
                let outcome = match outcome {
                    0 => Outcome::None,
                    1 => Outcome::Opened,
                    2 => Outcome::Forwarded,
                    3 => Outcome::EndpointData,
                    4 => Outcome::DurableCommitted,
                    5 => Outcome::EndOfStream,
                    6 => Outcome::Acknowledged,
                    _ => return Err(Error::new(11, "unknown response outcome")),
                };
                return Ok(Ok(Reply {
                    request: id,
                    outcome,
                    value,
                    data,
                }));
            }
        }
    }
    pub(crate) fn send_shard(
        &mut self,
        packet: &crate::delivery::Packet,
        end: Instant,
    ) -> Result<()> {
        ensure(
            !self.poisoned,
            18,
            "session must reconnect after an unconfirmed request",
        )?;
        let wire = self.session.encode(7, &packet.encode())?;
        let result = self.session.send_wire(&wire, end);
        if result.is_err() {
            self.poisoned = true;
        }
        result
    }
    pub(crate) fn receive_ack(&mut self, end: Instant) -> Result<crate::delivery::BlockAck> {
        if let Some(ack) = self.acks.pop_front() {
            return Ok(ack);
        }
        loop {
            let wire = self.session.receive_wire(end)?;
            let kind = protocol::unpack(&wire)?.0;
            if kind == 6 {
                match self.session.decode(&wire, 6) {
                    Ok(plain) => {
                        let mut d = Decoder::new(&plain);
                        ensure(d.u64()? < self.next, 11, "unexpected pending RPC response")?;
                    }
                    Err(e) if e.code == 13 => {}
                    Err(e) => return Err(e),
                }
                continue;
            }
            ensure(kind == 8, 11, "delivery ACK expected")?;
            match self.session.decode(&wire, 8) {
                Ok(plain) => return crate::delivery::BlockAck::decode(&plain),
                Err(e) if e.code == 13 => continue,
                Err(e) => return Err(e),
            }
        }
    }
    pub fn open(&mut self, protocol: TargetProtocol, target: &str) -> Result<u32> {
        ensure(target.len() <= 255, 4, "destination name too long")?;
        let mut body = (target.len() as u16).to_be_bytes().to_vec();
        body.extend_from_slice(target.as_bytes());
        let reply = self.request(protocol as u32, &body)?;
        ensure(
            reply.outcome == Outcome::Opened && reply.value > 0 && reply.value <= u32::MAX as u64,
            11,
            "invalid open result",
        )?;
        Ok(reply.value as u32)
    }
    pub fn write(&mut self, stream: u32, bytes: &[u8]) -> Result<usize> {
        ensure(
            bytes.len() <= self.maximum_data(),
            7,
            "client write payload limit",
        )?;
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
        body.extend_from_slice(bytes);
        let reply = self.request(3, &body)?;
        ensure(
            reply.outcome == Outcome::Forwarded && reply.value == bytes.len() as u64,
            11,
            "invalid forwarded byte receipt",
        )?;
        Ok(bytes.len())
    }
    pub fn read(&mut self, stream: u32, maximum: usize) -> Result<Reply> {
        ensure(
            maximum > 0 && maximum <= self.maximum_data(),
            4,
            "invalid client receive bound",
        )?;
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&(maximum as u32).to_be_bytes());
        let r = self.request(4, &body)?;
        ensure(
            matches!(r.outcome, Outcome::EndpointData | Outcome::EndOfStream)
                && r.data.len() <= maximum
                && r.value == r.data.len() as u64,
            11,
            "invalid endpoint response",
        )?;
        Ok(r)
    }
    pub fn close(&mut self, stream: u32, half: bool) -> Result<()> {
        let r = self.request(if half { 5 } else { 6 }, &stream.to_be_bytes())?;
        ensure(
            r.outcome == Outcome::Acknowledged,
            11,
            "invalid stream close acknowledgement",
        )
    }
    pub fn datagram(&mut self, stream: u32, bytes: &[u8]) -> Result<Vec<u8>> {
        ensure(
            bytes.len() <= self.maximum_data(),
            7,
            "client datagram limit",
        )?;
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
        body.extend_from_slice(bytes);
        let r = self.request(7, &body)?;
        ensure(
            r.outcome == Outcome::EndpointData && r.data.len() == r.value as usize,
            11,
            "invalid datagram response",
        )?;
        Ok(r.data)
    }
    pub fn store(&mut self, id: [u8; 16], expires: u64, payload: &[u8]) -> Result<()> {
        ensure(
            payload.len() <= self.maximum_data(),
            7,
            "durable message carrier quota",
        )?;
        let mut body = id.to_vec();
        body.extend_from_slice(&expires.to_be_bytes());
        body.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        body.extend_from_slice(payload);
        let r = self.request(8, &body)?;
        ensure(
            r.outcome == Outcome::DurableCommitted && r.value == payload.len() as u64,
            11,
            "gateway did not confirm durable commit",
        )
    }
    pub fn fetch(&mut self, id: [u8; 16]) -> Result<Vec<u8>> {
        let r = self.request(9, &id)?;
        ensure(
            r.outcome == Outcome::EndpointData,
            11,
            "invalid durable retrieval result",
        )?;
        Ok(r.data)
    }
    pub fn consume(&mut self, id: [u8; 16]) -> Result<()> {
        let r = self.request(10, &id)?;
        ensure(
            r.outcome == Outcome::Acknowledged,
            11,
            "invalid durable consume result",
        )
    }
    pub fn flush_outbox(&mut self, journal: &mut Journal) -> Result<usize> {
        let mut count = 0;
        for entry in journal.pending() {
            ensure(
                entry.owner == self.session.peer.root,
                16,
                "outbox entry is for a different gateway",
            )?;
            self.store(entry.id, entry.expires, &entry.payload)?;
            journal.mark(entry.id, State::Acknowledged)?;
            count += 1;
        }
        Ok(count)
    }
}
