use crate::{
    crypto,
    delivery::{self, DeliveryState, Descriptor, Packet, Receipt},
    ensure, fec, ffi,
    gateway::{Client, ClientConfig, Outcome, Reply},
    identity::{Identity, Record},
    journal::Journal,
    now,
    policy::TargetProtocol,
    protocol::Decoder,
    transport::Kind,
    Error, Result,
};
use std::{
    collections::BTreeMap,
    net::SocketAddr,
    path::PathBuf,
    time::{Duration, Instant},
};
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Domains {
    pub interface: String,
    pub isp: String,
    pub egress: String,
    pub access: String,
}
impl Domains {
    pub fn shared() -> Self {
        Self {
            interface: "unknown".into(),
            isp: "unknown".into(),
            egress: "unknown".into(),
            access: "unknown".into(),
        }
    }
    fn validate(&self) -> Result<()> {
        for value in [&self.interface, &self.isp, &self.egress, &self.access] {
            ensure(
                !value.is_empty()
                    && value.len() <= 64
                    && value.is_ascii()
                    && !value.bytes().any(|b| b.is_ascii_control()),
                4,
                "invalid path failure domain label",
            )?;
        }
        Ok(())
    }
}
#[derive(Clone, Debug)]
pub struct Candidate {
    pub config: ClientConfig,
    pub source: Option<SocketAddr>,
    pub domains: Domains,
    pub cost: u32,
}
#[derive(Clone, Debug)]
pub struct PoolPolicy {
    pub probe_ms: u32,
    pub max_age_ms: u32,
    pub hold_ms: u32,
    pub hysteresis: u32,
    pub circuit_ms: u32,
    pub fec_threshold: u32,
    pub fec_budget: u32,
    pub window: usize,
}
impl Default for PoolPolicy {
    fn default() -> Self {
        Self {
            probe_ms: 1000,
            max_age_ms: 5000,
            hold_ms: 1000,
            hysteresis: 15,
            circuit_ms: 200,
            fec_threshold: 50000,
            fec_budget: 500,
            window: 4,
        }
    }
}
impl PoolPolicy {
    fn validate(&self) -> Result<()> {
        ensure(
            (50..=60000).contains(&self.probe_ms)
                && self.max_age_ms >= self.probe_ms
                && self.max_age_ms <= 60000
                && self.hold_ms <= 60000
                && self.hysteresis <= 100
                && (20..=10000).contains(&self.circuit_ms)
                && self.fec_threshold <= 1000000
                && self.fec_budget <= 1000
                && (1..=4).contains(&self.window),
            4,
            "invalid path/probe/FEC/window policy",
        )
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum PoolState {
    Ready = 1,
    Degraded = 2,
    Recovering = 3,
    Buffered = 4,
    Offline = 5,
    AuthenticationFailed = 6,
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct PathMetrics {
    pub id: u64,
    pub rtt_us: u64,
    pub jitter_us: u64,
    pub handshake_us: u64,
    pub goodput_bps: u64,
    pub samples: u64,
    pub failures: u64,
    pub probes: u64,
    pub bytes_committed: u64,
    pub loss_ppm: u32,
    pub profile: u32,
    pub ready: u32,
    pub last_error: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct PoolStatistics {
    pub migrations: u64,
    pub committed_bytes: u64,
    pub retransmitted: u64,
    pub fec_shards: u64,
    pub probes: u64,
    pub active: u64,
    pub goodput_bps: u64,
    pub ready_paths: u32,
    pub configured_paths: u32,
    pub state: u32,
    pub declared_access_domains: u32,
    pub independent_pairs: u32,
    pub reserved: u32,
}
struct Path {
    candidate: Candidate,
    client: Option<Client>,
    metric: PathMetrics,
    verified: Option<Instant>,
    retry: Instant,
    quarantined: bool,
    storage: [u8; 32],
    instance: [u8; 32],
    features: u32,
    local: Option<SocketAddr>,
    attached: bool,
    peer_epoch: u64,
    proxy: Option<crate::proxy::ClientConfig>,
}
pub struct Pool {
    paths: Vec<Path>,
    policy: PoolPolicy,
    state: PoolState,
    active: Option<usize>,
    switched: Instant,
    started: Instant,
    root: [u8; 32],
    highest_peer_epoch: u64,
    trust: Option<PathBuf>,
    storage: Option<[u8; 32]>,
    logical: [u8; 16],
    ticket: [u8; 32],
    instance: [u8; 32],
    next: u64,
    pending_logical: Option<(u32, Vec<u8>)>,
    written: BTreeMap<u32, u64>,
    read: BTreeMap<u32, u64>,
    statistics: PoolStatistics,
}
impl Pool {
    pub fn new(candidates: Vec<Candidate>, policy: PoolPolicy) -> Result<Self> {
        Self::new_with_trust(candidates, policy, None)
    }
    pub fn new_with_trust(
        candidates: Vec<Candidate>,
        policy: PoolPolicy,
        trust: Option<PathBuf>,
    ) -> Result<Self> {
        Self::new_with_proxy_map(candidates, policy, trust, BTreeMap::new())
    }
    pub fn new_with_proxy_map(
        candidates: Vec<Candidate>,
        policy: PoolPolicy,
        trust: Option<PathBuf>,
        proxies: BTreeMap<usize, crate::proxy::ClientConfig>,
    ) -> Result<Self> {
        policy.validate()?;
        for (&i, p) in &proxies {
            ensure(
                i < candidates.len()
                    && p.address.port() > 0
                    && matches!(
                        candidates[i].config.kind,
                        Kind::QuicStream | Kind::QuicDatagram
                    ),
                4,
                "invalid proxy path index or carrier",
            )?;
            ensure(
                p.http_version == 1,
                6,
                "HTTP/2 and HTTP/3 MASQUE unsupported",
            )?;
        }
        ensure(
            !candidates.is_empty() && candidates.len() <= 6,
            4,
            "path pool requires one to six candidates",
        )?;
        let root = candidates[0].config.peer;
        let identity = &candidates[0].config.identity;
        for (i, c) in candidates.iter().enumerate() {
            c.domains.validate()?;
            ensure(
                c.config.peer == root
                    && &c.config.identity == identity
                    && c.cost <= 1000000
                    && c.config.gateway.port() > 0,
                16,
                "path catalog identity/cost/address differs",
            )?;
            ensure(
                !candidates[..i].iter().enumerate().any(|(n, old)| {
                    old.config.gateway == c.config.gateway
                        && old.config.kind == c.config.kind
                        && old.source == c.source
                        && proxies.get(&n).map(|p| p.address) == proxies.get(&i).map(|p| p.address)
                }),
                4,
                "duplicate path catalog entry",
            )?;
        }
        let mut highest_peer_epoch = candidates
            .iter()
            .map(|c| c.config.minimum_epoch)
            .max()
            .unwrap_or(1);
        if let Some(path) = &trust {
            highest_peer_epoch =
                highest_peer_epoch.max(crate::identity::trust_cache(path, &root, None)?);
        }
        let started = Instant::now();
        let paths = candidates
            .into_iter()
            .enumerate()
            .map(|(i, candidate)| {
                let profile = candidate.config.kind.profile();
                Path {
                    candidate,
                    client: None,
                    metric: PathMetrics {
                        id: i as u64 + 1,
                        profile,
                        ..Default::default()
                    },
                    verified: None,
                    retry: started,
                    quarantined: false,
                    storage: [0; 32],
                    instance: [0; 32],
                    features: 0,
                    local: None,
                    attached: false,
                    peer_epoch: 0,
                    proxy: proxies.get(&i).cloned(),
                }
            })
            .collect();
        Ok(Self {
            paths,
            policy,
            state: PoolState::Offline,
            active: None,
            switched: started,
            started,
            root,
            highest_peer_epoch,
            trust,
            storage: None,
            logical: crypto::random()?,
            ticket: [0; 32],
            instance: [0; 32],
            next: 1,
            pending_logical: None,
            written: BTreeMap::new(),
            read: BTreeMap::new(),
            statistics: PoolStatistics::default(),
        })
    }
    pub fn peer(&self) -> [u8; 32] {
        self.root
    }
    pub fn set_proxy(&mut self, index: usize, proxy: crate::proxy::ClientConfig) -> Result<()> {
        let p = self
            .paths
            .get_mut(index)
            .ok_or(Error::new(4, "proxy path index out of range"))?;
        ensure(
            p.client.is_none()
                && matches!(
                    p.candidate.config.kind,
                    Kind::QuicStream | Kind::QuicDatagram
                ),
            4,
            "proxy path must be unopened QUIC",
        )?;
        ensure(
            proxy.http_version == 1,
            6,
            "HTTP/2 and HTTP/3 MASQUE are unsupported",
        )?;
        p.proxy = Some(proxy);
        Ok(())
    }
    fn elapsed(&self) -> u64 {
        self.started.elapsed().as_millis().min(u64::MAX as u128) as u64
    }
    fn optimizer(&self) -> ffi::OptimizerPolicy {
        ffi::OptimizerPolicy {
            delivery: 1,
            latency: 1000,
            loss: 10,
            throughput: 1,
            cost: 1,
            max_age: self.policy.max_age_ms,
            min_samples: 1,
            min_hold: self.policy.hold_ms,
            hysteresis: self.policy.hysteresis,
        }
    }
    fn failure(&mut self, index: usize, error: &Error) {
        let p = &mut self.paths[index];
        p.client = None;
        p.attached = false;
        p.metric.ready = 0;
        p.metric.last_error = error.code;
        p.metric.failures = p.metric.failures.saturating_add(1);
        p.metric.loss_ppm = ((p.metric.loss_ppm as u64 * 7 + 1000000) / 8).min(1000000) as u32;
        p.quarantined = matches!(error.code, 12 | 14 | 16)
            || (error.code == 9 && matches!(error.system, 51 | 58 | 60 | 77 | 83 | 90 | 91));
        let factor = 1u64 << p.metric.failures.min(5);
        p.retry = Instant::now()
            + Duration::from_millis((self.policy.circuit_ms as u64 * factor).min(10000));
        if self.active == Some(index) {
            self.active = None;
        }
        self.update_state(false);
    }
    fn probe_path(&mut self, index: usize) -> Result<()> {
        let root = self.root;
        let minimum = self.highest_peer_epoch;
        let cache = self.trust.clone();
        let expected_storage = self.storage;
        let p = &mut self.paths[index];
        let initial = p.client.is_none();
        let started = Instant::now();
        if initial {
            let mut config = p.candidate.config.clone();
            config.minimum_epoch = config.minimum_epoch.max(minimum);
            p.client = Some(Client::connect_with_proxy(
                &config,
                p.candidate.source,
                p.proxy.as_ref(),
            )?);
            p.metric.handshake_us = started.elapsed().as_micros().min(u64::MAX as u128) as u64;
            p.attached = false;
        }
        let client = p.client.as_mut().unwrap();
        ensure(
            client.minor() >= 1,
            6,
            "gateway continuity extension requires protocol minor 1",
        )?;
        if initial {
            let caps = client.request(11, &[])?;
            let mut d = Decoder::new(&caps.data);
            ensure(
                d.take(4)? == b"DGC2" && d.u32()? == 1,
                6,
                "gateway extension capabilities unsupported",
            )?;
            p.storage = d.take(32)?.try_into().unwrap();
            p.instance = d.take(32)?.try_into().unwrap();
            p.features = d.u32()?;
            ensure(
                d.u32()? == delivery::MAX_TRANSFER as u32
                    && d.u32()? == delivery::BLOCK as u32
                    && d.u32()? == delivery::SHARD as u32,
                6,
                "gateway delivery limits incompatible",
            )?;
            ensure(
                d.take(32)? == crate::protocol::descriptor_hash()?,
                12,
                "gateway continuity schema hash mismatch",
            )?;
            d.end()?;
            if let Some(storage) = expected_storage {
                ensure(
                    storage == p.storage,
                    16,
                    "candidate points at a different durable gateway store",
                )?;
            }
        }
        let nonce = crypto::random::<32>()?;
        let started = Instant::now();
        let response = client.request(12, &nonce)?;
        let rtt = started.elapsed().as_micros().clamp(1, 60000000000) as u64;
        let mut d = Decoder::new(&response.data);
        ensure(
            d.take(4)? == b"DGP1"
                && d.take(32)? == nonce
                && d.take(32)? == client.session.session_id,
            12,
            "path probe nonce/session binding mismatch",
        )?;
        let at = d.u64()?;
        ensure(now().abs_diff(at) <= 15, 14, "path probe result expired")?;
        ensure(
            d.take(32)? == p.storage && d.take(32)? == p.instance,
            12,
            "path probe service binding mismatch",
        )?;
        let n = d.u32()? as usize;
        let locator = Record::parse(d.take(n)?)?;
        locator.pinned(
            &root,
            p.candidate
                .config
                .minimum_epoch
                .max(client.peer().epoch)
                .max(minimum),
        )?;
        let signature = d.take(64)?;
        d.end()?;
        let owner = Identity::load(&p.candidate.config.identity)?.public()?.root;
        let mut signed = b"DLN-PATH-PROBE1".to_vec();
        signed.extend_from_slice(&owner);
        signed.extend_from_slice(&response.data[..response.data.len() - 64]);
        crypto::verify(&locator.key, &signed, signature)?;
        if let Some(cache) = &cache {
            crate::identity::trust_cache(cache, &root, Some(&locator))?;
        }
        self.highest_peer_epoch = self.highest_peer_epoch.max(locator.epoch);
        p.peer_epoch = locator.epoch;
        let previous = p.metric.rtt_us;
        p.metric.jitter_us = if previous == 0 {
            0
        } else {
            (p.metric.jitter_us * 3 + previous.abs_diff(rtt)) / 4
        };
        p.metric.rtt_us = if previous == 0 {
            rtt
        } else {
            (previous * 7 + rtt) / 8
        };
        p.metric.samples = p.metric.samples.saturating_add(1);
        p.metric.probes = p.metric.probes.saturating_add(1);
        p.metric.ready = 1;
        p.metric.last_error = 0;
        p.verified = Some(Instant::now());
        p.local = Some(client.bound_source());
        if self.storage.is_none() {
            self.storage = Some(p.storage);
        }
        Ok(())
    }
    pub fn refresh(&mut self) -> Result<()> {
        self.state = PoolState::Recovering;
        let mut last = None;
        for index in 0..self.paths.len() {
            let p = &self.paths[index];
            if p.quarantined || Instant::now() < p.retry {
                continue;
            }
            if p.client.is_none()
                || p.peer_epoch < self.highest_peer_epoch
                || p.verified.is_none_or(|at| {
                    at.elapsed() >= Duration::from_millis(self.policy.probe_ms as u64)
                })
            {
                if let Err(e) = self.probe_path(index) {
                    self.failure(index, &e);
                    last = Some(e);
                }
            }
        }
        self.update_state(false);
        if (0..self.paths.len()).any(|i| self.eligible(i)) {
            Ok(())
        } else {
            Err(last.unwrap_or(Error::new(
                20,
                "all authorised gateway paths are unavailable",
            )))
        }
    }
    fn eligible(&self, index: usize) -> bool {
        let p = &self.paths[index];
        !p.quarantined
            && p.client.is_some()
            && p.metric.ready != 0
            && p.peer_epoch >= self.highest_peer_epoch
            && p.verified.is_some_and(|at| {
                at.elapsed() <= Duration::from_millis(self.policy.max_age_ms as u64)
            })
    }
    fn choose(&mut self, exclude: Option<usize>) -> Result<usize> {
        let now = self.elapsed();
        let values: Vec<_> = self
            .paths
            .iter()
            .enumerate()
            .map(|(i, p)| ffi::Observation {
                id: i as u64 + 1,
                at_ms: p
                    .verified
                    .map(|v| v.saturating_duration_since(self.started).as_millis() as u64)
                    .unwrap_or(0),
                rtt_us: p.metric.rtt_us,
                goodput: p.metric.goodput_bps,
                samples: p.metric.samples,
                loss_ppm: p.metric.loss_ppm,
                profile: p.candidate.config.kind.profile(),
                properties: unsafe { ffi::dgn_path_properties(p.candidate.config.kind.profile()) },
                cost: p.candidate.cost,
                ready: self.eligible(i) && exclude != Some(i),
            })
            .collect();
        let chosen =
            unsafe { ffi::dgn_choose(self.optimizer(), values.as_ptr(), values.len(), now) };
        ensure(
            chosen.status == 0,
            20,
            "no fresh authenticated path satisfies security requirements",
        )?;
        let mut index = chosen.index as usize;
        if exclude.is_none() {
            if let Some(active) = self.active {
                if active != index && self.eligible(active) {
                    let previous =
                        unsafe { ffi::dgn_choose(self.optimizer(), &values[active], 1, now) };
                    if previous.status == 0
                        && (self.switched.elapsed()
                            < Duration::from_millis(self.policy.hold_ms as u64)
                            || chosen.score.saturating_mul(100)
                                >= previous
                                    .score
                                    .saturating_mul(100 - self.policy.hysteresis as u64))
                    {
                        index = active;
                    }
                }
            }
            if self.active != Some(index) {
                if self.active.is_some() || self.statistics.active != 0 {
                    self.statistics.migrations += 1;
                }
                self.active = Some(index);
                self.statistics.active = index as u64 + 1;
                self.switched = Instant::now();
            }
        }
        Ok(index)
    }
    fn update_state(&mut self, buffered: bool) {
        let ready = (0..self.paths.len()).filter(|&i| self.eligible(i)).count();
        self.state = if ready == self.paths.len() {
            PoolState::Ready
        } else if ready > 0 {
            PoolState::Degraded
        } else if buffered {
            PoolState::Buffered
        } else if self.paths.iter().all(|p| p.quarantined) {
            PoolState::AuthenticationFailed
        } else {
            PoolState::Offline
        };
    }
    pub fn state(&self) -> PoolState {
        let ready = (0..self.paths.len()).filter(|&i| self.eligible(i)).count();
        if ready == self.paths.len() {
            PoolState::Ready
        } else if ready > 0 {
            PoolState::Degraded
        } else if self.state == PoolState::Buffered {
            PoolState::Buffered
        } else if self.paths.iter().all(|p| p.quarantined) {
            PoolState::AuthenticationFailed
        } else {
            PoolState::Offline
        }
    }
    pub fn statistics(&self) -> PoolStatistics {
        let mut v = self.statistics;
        v.ready_paths = (0..self.paths.len()).filter(|&i| self.eligible(i)).count() as u32;
        v.configured_paths = self.paths.len() as u32;
        v.state = self.state() as u32;
        v.probes = self.paths.iter().map(|p| p.metric.probes).sum();
        v.active = self
            .active
            .filter(|&i| self.eligible(i))
            .map(|i| i as u64 + 1)
            .unwrap_or(0);
        v.goodput_bps = if v.active == 0 {
            0
        } else {
            self.paths[v.active as usize - 1].metric.goodput_bps
        };
        let mut domains = Vec::new();
        for p in &self.paths {
            let tuple = (
                &p.candidate.domains.interface,
                &p.candidate.domains.isp,
                &p.candidate.domains.egress,
                &p.candidate.domains.access,
            );
            if !domains.contains(&tuple) {
                domains.push(tuple);
            }
        }
        v.declared_access_domains = domains.len() as u32;
        v.independent_pairs = 0;
        for (i, a) in self.paths.iter().enumerate() {
            for (j, b) in self.paths.iter().enumerate().skip(i + 1) {
                if self.eligible(i)
                    && self.eligible(j)
                    && a.instance != b.instance
                    && a.local.zip(b.local).is_some_and(|(a, b)| a.ip() != b.ip())
                    && [
                        &a.candidate.domains.interface,
                        &a.candidate.domains.isp,
                        &a.candidate.domains.egress,
                        &a.candidate.domains.access,
                    ]
                    .into_iter()
                    .zip([
                        &b.candidate.domains.interface,
                        &b.candidate.domains.isp,
                        &b.candidate.domains.egress,
                        &b.candidate.domains.access,
                    ])
                    .all(|(a, b)| a != b && a != "unknown" && b != "unknown")
                {
                    v.independent_pairs += 1;
                }
            }
        }
        v
    }
    pub fn metrics(&self) -> Vec<PathMetrics> {
        self.paths
            .iter()
            .map(|p| {
                let mut m = p.metric;
                m.ready = self.eligible((m.id - 1) as usize) as u32;
                if m.ready == 0 {
                    m.goodput_bps = 0;
                }
                m
            })
            .collect()
    }
    fn control(&mut self, operation: u32, body: &[u8]) -> Result<Reply> {
        let _ = self.refresh();
        let mut last = Error::new(20, "all authorised gateway paths are unavailable");
        for _ in 0..self.paths.len() {
            let index = match self.choose(None) {
                Ok(i) => i,
                Err(_) => return Err(last),
            };
            match self.paths[index]
                .client
                .as_mut()
                .unwrap()
                .request(operation, body)
            {
                Ok(v) => return Ok(v),
                Err(e) if e.message == "gateway request rejected" => return Err(e),
                Err(e) => {
                    self.failure(index, &e);
                    last = e;
                }
            }
        }
        Err(last)
    }
    fn attach(&mut self, index: usize) -> Result<()> {
        if self.paths[index].attached {
            return Ok(());
        }
        let mut body = self.logical.to_vec();
        body.extend_from_slice(&self.ticket);
        body.extend_from_slice(&self.instance);
        let r = self.paths[index]
            .client
            .as_mut()
            .unwrap()
            .request(30, &body)?;
        ensure(
            r.data.len() == 72,
            11,
            "logical session ticket size invalid",
        )?;
        let instance: [u8; 32] = r.data[..32].try_into().unwrap();
        let ticket: [u8; 32] = r.data[32..64].try_into().unwrap();
        let next = u64::from_be_bytes(r.data[64..].try_into().unwrap());
        ensure(
            (self.ticket == [0; 32] || (ticket == self.ticket && instance == self.instance))
                && next >= self.next
                && next <= self.next + 1,
            18,
            "logical session resume acknowledgement mismatch",
        )?;
        self.instance = instance;
        self.ticket = ticket;
        self.paths[index].attached = true;
        Ok(())
    }
    fn logical(&mut self, operation: u32, body: &[u8]) -> Result<Reply> {
        if let Some((known, data)) = &self.pending_logical {
            ensure(
                *known == operation && data == body,
                18,
                "repeat the unconfirmed logical operation before issuing another",
            )?;
        } else {
            self.pending_logical = Some((operation, body.to_vec()));
        }
        let _ = self.refresh();
        let request = self.next;
        let mut last = Error::new(20, "logical session has no live path");
        for _ in 0..self.paths.len() {
            let index = match self.choose(None) {
                Ok(i) => i,
                Err(_) => return Err(last),
            };
            if let Err(e) = self.attach(index) {
                if e.message == "gateway request rejected" {
                    return Err(e);
                }
                self.failure(index, &e);
                last = e;
                continue;
            }
            let mut envelope = self.logical.to_vec();
            envelope.extend_from_slice(&self.ticket);
            envelope.extend_from_slice(&self.instance);
            envelope.extend_from_slice(&request.to_be_bytes());
            envelope.extend_from_slice(&operation.to_be_bytes());
            envelope.extend_from_slice(body);
            match self.paths[index]
                .client
                .as_mut()
                .unwrap()
                .request(31, &envelope)
            {
                Ok(reply) => {
                    self.next = self
                        .next
                        .checked_add(1)
                        .ok_or(Error::new(7, "logical request identifier exhausted"))?;
                    self.pending_logical = None;
                    return decode_reply(request, &reply.data);
                }
                Err(e) if e.message == "gateway request rejected" => return Err(e),
                Err(e) => {
                    self.failure(index, &e);
                    last = e;
                }
            }
        }
        Err(last)
    }
    pub fn open(&mut self, protocol: TargetProtocol, target: &str) -> Result<u32> {
        ensure(
            !target.is_empty() && target.len() <= 255,
            4,
            "invalid logical destination name",
        )?;
        let mut body = (target.len() as u16).to_be_bytes().to_vec();
        body.extend_from_slice(target.as_bytes());
        let r = self.logical(protocol as u32, &body)?;
        ensure(
            r.outcome == Outcome::Opened && r.value > 0 && r.value <= u32::MAX as u64,
            11,
            "invalid logical stream open response",
        )?;
        Ok(r.value as u32)
    }
    pub fn write(&mut self, stream: u32, bytes: &[u8]) -> Result<usize> {
        ensure(
            bytes.len() <= 640,
            7,
            "logical write limit is 640 bytes across every carrier",
        )?;
        let offset = self.written.get(&stream).copied().unwrap_or(0);
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&offset.to_be_bytes());
        body.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
        body.extend_from_slice(bytes);
        let r = self.logical(3, &body)?;
        ensure(
            r.outcome == Outcome::Forwarded && r.value == bytes.len() as u64,
            11,
            "invalid logical forwarded receipt",
        )?;
        self.written.insert(stream, offset + bytes.len() as u64);
        Ok(bytes.len())
    }
    pub fn read(&mut self, stream: u32, maximum: usize) -> Result<Reply> {
        ensure(
            maximum > 0 && maximum <= 640,
            4,
            "logical receive bound must be 1..640",
        )?;
        let offset = self.read.get(&stream).copied().unwrap_or(0);
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&offset.to_be_bytes());
        body.extend_from_slice(&(maximum as u32).to_be_bytes());
        let r = self.logical(4, &body)?;
        ensure(
            matches!(r.outcome, Outcome::EndpointData | Outcome::EndOfStream)
                && r.data.len() == r.value as usize
                && r.data.len() <= maximum,
            11,
            "invalid logical receive offset",
        )?;
        self.read.insert(stream, offset + r.value);
        Ok(r)
    }
    pub fn close(&mut self, stream: u32, half: bool) -> Result<()> {
        let r = self.logical(if half { 5 } else { 6 }, &stream.to_be_bytes())?;
        ensure(
            r.outcome == Outcome::Acknowledged,
            11,
            "invalid logical close receipt",
        )?;
        if !half {
            self.written.remove(&stream);
            self.read.remove(&stream);
        }
        Ok(())
    }
    pub fn datagram(&mut self, stream: u32, bytes: &[u8]) -> Result<Vec<u8>> {
        ensure(bytes.len() <= 640, 7, "logical datagram limit is 640 bytes")?;
        let mut body = stream.to_be_bytes().to_vec();
        body.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
        body.extend_from_slice(bytes);
        let r = self.logical(7, &body)?;
        ensure(
            r.outcome == Outcome::EndpointData && r.value == r.data.len() as u64,
            11,
            "invalid logical datagram response",
        )?;
        Ok(r.data)
    }
    fn receipt(&mut self, operation: u32, desc: &Descriptor) -> Result<Receipt> {
        Receipt::decode(&self.control(operation, &desc.encode())?.data, desc)
    }
    pub fn status(&mut self, desc: &Descriptor) -> Result<Receipt> {
        self.receipt(21, desc)
    }
    fn credited(&mut self, path: usize, count: usize, started: Instant) {
        let p = &mut self.paths[path];
        p.metric.bytes_committed = p.metric.bytes_committed.saturating_add(count as u64);
        let rate = (count as u128 * 8000000 / started.elapsed().as_micros().max(1))
            .min(1000000000000) as u64;
        p.metric.goodput_bps = if p.metric.goodput_bps == 0 {
            rate
        } else {
            (p.metric.goodput_bps * 7 + rate) / 8
        };
        self.statistics.committed_bytes =
            self.statistics.committed_bytes.saturating_add(count as u64);
    }
    pub fn upload(&mut self, desc: &Descriptor, bytes: &[u8]) -> Result<Receipt> {
        desc.validate()?;
        ensure(
            bytes.len() == desc.length as usize && crypto::hash(bytes)? == desc.digest,
            12,
            "upload content differs from delivery descriptor",
        )?;
        let initial = self.receipt(20, desc)?;
        if matches!(
            initial.state,
            DeliveryState::Completed | DeliveryState::Consumed
        ) {
            return Ok(initial);
        }
        let end = Instant::now() + Duration::from_secs(60);
        let missing: Vec<_> = (0..desc.blocks()).filter(|&i| !initial.has(i)).collect();
        for window in missing.chunks(self.policy.window.min(initial.credit.max(1) as usize)) {
            let mut tasks = Vec::new();
            let mut used = Vec::new();
            for &index in window {
                let best = self.choose(None)?;
                let path = if used.contains(&best) {
                    self.choose(Some(best)).unwrap_or(best)
                } else {
                    best
                };
                used.push(path);
                ensure(
                    self.paths[path].features & 2 != 0,
                    6,
                    "gateway has no durable transfer capability",
                )?;
                let bytes = &bytes
                    [index * delivery::BLOCK..index * delivery::BLOCK + desc.block_len(index)?];
                let k = bytes.len().div_ceil(delivery::SHARD) as u8;
                let p = &self.paths[path];
                let parity = if p.candidate.config.kind == Kind::QuicDatagram
                    && p.metric.loss_ppm >= self.policy.fec_threshold
                {
                    ((k as u32 * self.policy.fec_budget) / 1000).min(32 - k as u32) as u8
                } else {
                    0
                };
                let group = fec::encode(bytes, k, k + parity)?;
                let packets = group
                    .shards
                    .into_iter()
                    .map(|shard| Packet {
                        id: desc.id,
                        stream: desc.stream,
                        offset: (index * delivery::BLOCK) as u64,
                        length: bytes.len(),
                        data: k,
                        total: k + parity,
                        shard,
                        digest: group.digest,
                    })
                    .collect();
                tasks.push(Flight {
                    index,
                    path,
                    packets,
                    sent: 0,
                    received: 0,
                    committed: false,
                    attempts: 0,
                    started: Instant::now(),
                    last: Instant::now(),
                });
            }
            while tasks.iter().any(|t| !t.committed) {
                ensure(Instant::now() < end, 3, "delivery upload deadline expired")?;
                let mut progress = false;
                for task in tasks.iter_mut().filter(|t| !t.committed) {
                    if !self.eligible(task.path) {
                        let _ = self.refresh();
                        task.path = self.choose(None)?;
                        task.sent = task.received;
                        task.last = Instant::now();
                        task.attempts += 1;
                        ensure(
                            task.attempts <= 8,
                            3,
                            "delivery path retry budget exhausted",
                        )?;
                    }
                    let rto = Duration::from_micros(
                        (self.paths[task.path].metric.rtt_us * 3
                            + self.paths[task.path].metric.jitter_us * 4
                            + 20000)
                            .clamp(50000, 2000000),
                    );
                    if task.sent.count_ones() as usize >= task.packets.len()
                        && task.last.elapsed() >= rto
                    {
                        let observed = task.received.count_ones() as usize;
                        let loss = ((task.packets.len().saturating_sub(observed)) * 1000000
                            / task.packets.len()) as u32;
                        let p = &mut self.paths[task.path];
                        p.metric.loss_ppm = (p.metric.loss_ppm * 3 + loss) / 4;
                        let status = self.receipt(21, desc)?;
                        if status.has(task.index) {
                            task.committed = true;
                            self.credited(task.path, desc.block_len(task.index)?, task.started);
                            continue;
                        }
                        task.attempts += 1;
                        ensure(
                            task.attempts <= 8,
                            3,
                            "delivery selective retry budget exhausted",
                        )?;
                        task.sent = task.received;
                        if let Ok(next) = self.choose(Some(task.path)) {
                            task.path = next;
                        }
                        task.last = Instant::now();
                    }
                    if let Some(index) = (0..task.packets.len()).find(|i| task.sent & (1 << i) == 0)
                    {
                        let p = &task.packets[index];
                        match self.paths[task.path]
                            .client
                            .as_mut()
                            .unwrap()
                            .send_shard(p, end)
                        {
                            Ok(()) => {
                                if task.attempts > 0 {
                                    self.statistics.retransmitted += 1;
                                }
                                if index >= p.data as usize {
                                    self.statistics.fec_shards += 1;
                                }
                                task.sent |= 1 << index;
                                task.last = Instant::now();
                                progress = true
                            }
                            Err(e) => self.failure(task.path, &e),
                        }
                    }
                }
                for path in 0..self.paths.len() {
                    if !self.eligible(path)
                        || !tasks
                            .iter()
                            .any(|t| !t.committed && (t.path == path || t.sent != 0))
                    {
                        continue;
                    }
                    for _ in 0..8 {
                        match self.paths[path]
                            .client
                            .as_mut()
                            .unwrap()
                            .receive_ack(Instant::now() + Duration::from_millis(1))
                        {
                            Ok(ack) => {
                                if ack.id != desc.id || ack.stream != desc.stream {
                                    continue;
                                }
                                if let Some(task) = tasks
                                    .iter_mut()
                                    .find(|t| t.index * delivery::BLOCK == ack.offset as usize)
                                {
                                    ensure(
                                        ack.digest == task.packets[0].digest,
                                        12,
                                        "block ACK digest mismatch",
                                    )?;
                                    if ack.status != 0 {
                                        return Err(Error::new(
                                            ack.status,
                                            "gateway delivery shard rejected",
                                        ));
                                    }
                                    let valid = if task.packets.len() == 32 {
                                        u32::MAX
                                    } else {
                                        (1u32 << task.packets.len()) - 1
                                    };
                                    ensure(
                                        ack.received & !valid == 0,
                                        11,
                                        "block ACK bitmap exceeds FEC group",
                                    )?;
                                    task.received |= ack.received;
                                    if ack.state == DeliveryState::Committed && !task.committed {
                                        task.committed = true;
                                        self.credited(
                                            path,
                                            desc.block_len(task.index)?,
                                            task.started,
                                        );
                                    }
                                    progress = true;
                                }
                            }
                            Err(e) if e.code == 3 => break,
                            Err(e) => {
                                self.failure(path, &e);
                                break;
                            }
                        }
                    }
                }
                if !progress {
                    std::thread::sleep(Duration::from_millis(1));
                }
            }
        }
        self.receipt(23, desc)
    }
    pub fn download(&mut self, desc: &Descriptor) -> Result<Vec<u8>> {
        ensure(
            self.status(desc)?.state == DeliveryState::Completed,
            18,
            "remote delivery is incomplete or consumed",
        )?;
        let mut out = Vec::with_capacity(desc.length as usize);
        for index in 0..desc.blocks() {
            let mut offset = 0;
            let count = desc.block_len(index)?;
            while offset < count {
                let mut body = desc.encode();
                body.extend_from_slice(&(index as u32).to_be_bytes());
                body.extend_from_slice(&(offset as u32).to_be_bytes());
                body.extend_from_slice(&((count - offset).min(512) as u32).to_be_bytes());
                let reply = self.control(22, &body)?;
                ensure(
                    reply.outcome == Outcome::EndpointData
                        && !reply.data.is_empty()
                        && reply.data.len() <= (count - offset).min(512),
                    11,
                    "invalid delivery download segment",
                )?;
                offset += reply.data.len();
                out.extend_from_slice(&reply.data);
            }
        }
        ensure(
            crypto::hash(&out)? == desc.digest,
            12,
            "download end-to-end digest mismatch",
        )?;
        Ok(out)
    }
    pub fn consume(&mut self, desc: &Descriptor) -> Result<Receipt> {
        self.receipt(24, desc)
    }
    pub fn flush(&mut self, journal: &mut Journal) -> Result<usize> {
        let mut sent = 0;
        for (peer, desc) in delivery::queued(journal)? {
            ensure(
                peer == self.root,
                16,
                "queued transfer belongs to another gateway",
            )?;
            let bytes = delivery::queued_bytes(journal, &peer, &desc)?;
            match self.upload(&desc, &bytes) {
                Ok(r) => {
                    ensure(
                        matches!(r.state, DeliveryState::Completed | DeliveryState::Consumed),
                        18,
                        "remote delivery lacks completion receipt",
                    )?;
                    delivery::acknowledged(journal, &peer, &desc)?;
                    sent += 1
                }
                Err(e) => {
                    self.update_state(true);
                    return Err(e);
                }
            }
        }
        Ok(sent)
    }
    pub fn disconnect(&mut self, path: usize) -> Result<()> {
        ensure(path < self.paths.len(), 4, "path index out of bounds")?;
        self.failure(path, &Error::new(2, "path disconnected by caller"));
        Ok(())
    }
}
impl Drop for Pool {
    fn drop(&mut self) {
        if self.ticket == [0; 32] {
            return;
        }
        let mut body = self.logical.to_vec();
        body.extend_from_slice(&self.ticket);
        if let Some(client) = self
            .paths
            .iter_mut()
            .filter_map(|p| p.client.as_mut())
            .next()
        {
            client.timeout = 200;
            let _ = client.request(32, &body);
        }
    }
}
struct Flight {
    index: usize,
    path: usize,
    packets: Vec<Packet>,
    sent: u32,
    received: u32,
    committed: bool,
    attempts: u32,
    started: Instant,
    last: Instant,
}
fn decode_reply(expected: u64, bytes: &[u8]) -> Result<Reply> {
    let mut d = Decoder::new(bytes);
    let id = d.u64()?;
    ensure(id == expected, 11, "logical response correlation mismatch")?;
    let status = d.u32()? as i32;
    let outcome = d.u32()?;
    let value = d.u64()?;
    let n = d.u32()? as usize;
    let data = d.take(n)?.to_vec();
    d.end()?;
    if status != 0 {
        return Err(Error {
            code: status,
            system: value as i32,
            message: "gateway logical operation rejected",
        });
    }
    let outcome = match outcome {
        0 => Outcome::None,
        1 => Outcome::Opened,
        2 => Outcome::Forwarded,
        3 => Outcome::EndpointData,
        4 => Outcome::DurableCommitted,
        5 => Outcome::EndOfStream,
        6 => Outcome::Acknowledged,
        _ => return Err(Error::new(11, "unknown logical response outcome")),
    };
    Ok(Reply {
        request: id,
        outcome,
        value,
        data,
    })
}
