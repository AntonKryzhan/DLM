use dlm_gateway::{
    delivery::{self, DeliveryState, Descriptor, Packet, Store},
    fec,
    gateway::{ClientConfig, GatewayGroup, Outcome, ServerConfig},
    identity::{hex, Identity},
    journal::Journal,
    path::{Candidate, Domains, Pool, PoolPolicy, PoolState},
    policy::{Policy, TargetProtocol, TargetRule},
    transport::Kind,
};
use std::{
    fs,
    io::{Read, Write},
    net::{SocketAddr, TcpListener},
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc, Mutex,
    },
    thread,
    time::Duration,
};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
struct Work(PathBuf);
impl Work {
    fn new() -> Self {
        let p = std::env::temp_dir().join(format!(
            "dlm-continuity-{}-{}",
            std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&p).unwrap();
        Self(p)
    }
}
impl Drop for Work {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
struct Echo {
    address: SocketAddr,
    bytes: Arc<AtomicUsize>,
    stop: Arc<AtomicBool>,
    worker: Option<thread::JoinHandle<()>>,
}
impl Echo {
    fn new(v6: bool) -> Self {
        let listener = TcpListener::bind(if v6 { "[::1]:0" } else { "127.0.0.1:0" }).unwrap();
        let address = listener.local_addr().unwrap();
        listener.set_nonblocking(true).unwrap();
        let bytes = Arc::new(AtomicUsize::new(0));
        let received = bytes.clone();
        let stop = Arc::new(AtomicBool::new(false));
        let stopped = stop.clone();
        let worker = thread::spawn(move || {
            let mut buf = [0; 8192];
            while !stopped.load(Ordering::Acquire) {
                match listener.accept() {
                    Ok((mut peer, _)) => {
                        peer.set_read_timeout(Some(Duration::from_millis(20)))
                            .unwrap();
                        while !stopped.load(Ordering::Acquire) {
                            match peer.read(&mut buf) {
                                Ok(0) => break,
                                Ok(n) => {
                                    received.fetch_add(n, Ordering::Relaxed);
                                    if peer.write_all(&buf[..n]).is_err() {
                                        break;
                                    }
                                }
                                Err(e)
                                    if matches!(
                                        e.kind(),
                                        std::io::ErrorKind::WouldBlock
                                            | std::io::ErrorKind::TimedOut
                                    ) => {}
                                Err(_) => break,
                            }
                        }
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                        thread::sleep(Duration::from_millis(1))
                    }
                    Err(e) => panic!("{e}"),
                }
            }
        });
        Self {
            address,
            bytes,
            stop,
            worker: Some(worker),
        }
    }
}
impl Drop for Echo {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            worker.join().unwrap();
        }
    }
}
fn config(w: &Work, echo: &Echo, kinds: &[Kind]) -> (Vec<ServerConfig>, ClientConfig) {
    let server = w.0.join("server.identity");
    let client = w.0.join("client.identity");
    let s = Identity::generate(&server).unwrap();
    let c = Identity::generate(&client).unwrap();
    let mut policy = Policy::new();
    policy.client(&hex(&c.public().unwrap().root)).unwrap();
    policy.allow_private = true;
    policy.limits.per_client = 6;
    policy.limits.idle_ms = 60000;
    policy.targets.push(
        TargetRule::parse(&format!(
            "tcp,{},{}",
            if echo.address.is_ipv6() {
                "::1/128"
            } else {
                "127.0.0.0/8"
            },
            echo.address.port()
        ))
        .unwrap(),
    );
    let key = w.0.join("store.key");
    dlm_gateway::journal::create_key(&key).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let f = root.join("examples/native/fixtures");
    let configs = kinds
        .iter()
        .map(|&kind| ServerConfig {
            bind: SocketAddr::new(echo.address.ip(), 0),
            advertise: None,
            kind,
            identity: server.clone(),
            certificate: f.join("quic-test-server.pem"),
            key: f.join("quic-test-key.pem"),
            locator_output: Some(w.0.join("gateway.dlr")),
            policy: policy.clone(),
            spool: Some((w.0.join("inbox"), key.clone())),
        })
        .collect();
    let client = ClientConfig {
        gateway: SocketAddr::new(echo.address.ip(), 1),
        hostname: "localhost".into(),
        ca: f.join("quic-test-ca.pem").to_string_lossy().into_owned(),
        kind: kinds[0],
        identity: client,
        peer: s.public().unwrap().root,
        minimum_epoch: 1,
        minimum_minor: 1,
        maximum_minor: 3,
        timeout_ms: 3000,
        locator: Some(w.0.join("gateway.dlr")),
    };
    (configs, client)
}
fn pool(group: &GatewayGroup, base: &ClientConfig, source: bool) -> Pool {
    let candidates = group
        .addresses()
        .iter()
        .enumerate()
        .map(|(index, (address, kind))| {
            let mut config = base.clone();
            config.gateway = *address;
            config.kind = *kind;
            Candidate {
                config,
                source: if source {
                    Some(SocketAddr::new(address.ip(), 0))
                } else {
                    None
                },
                domains: Domains::shared(),
                cost: index as u32 * 100000,
            }
        })
        .collect();
    let mut policy = PoolPolicy::default();
    policy.hold_ms = 0;
    Pool::new(candidates, policy).unwrap()
}
#[test]
fn logical_tcp_offsets_survive_primary_listener_failure() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, client) = config(&w, &echo, &[Kind::Tls, Kind::Tls]);
    let mut group = GatewayGroup::start(configs).unwrap();
    let mut p = pool(&group, &client, false);
    p.refresh().unwrap();
    let stream = p
        .open(TargetProtocol::Tcp, &echo.address.to_string())
        .unwrap();
    for n in 0..2 {
        let data = vec![n as u8 + 31; 389];
        p.write(stream, &data).unwrap();
        let mut out = Vec::new();
        while out.len() < data.len() {
            out.extend(p.read(stream, data.len() - out.len()).unwrap().data);
        }
        assert_eq!(out, data);
        if n == 0 {
            let active = p.statistics().active as usize - 1;
            drop(group.gateways.remove(active));
        }
    }
    assert_eq!(echo.bytes.load(Ordering::Relaxed), 778);
    assert!(p.statistics().migrations > 0);
    assert_eq!(p.statistics().independent_pairs, 0);
    p.close(stream, true).unwrap();
    assert_eq!(p.read(stream, 1).unwrap().outcome, Outcome::EndOfStream);
    p.close(stream, false).unwrap();
}
#[test]
fn reliable_paths_do_not_add_fec_and_completed_upload_is_not_retransmitted() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, client) = config(&w, &echo, &[Kind::Tls, Kind::Tls]);
    let group = GatewayGroup::start(configs).unwrap();
    let mut p = pool(&group, &client, false);
    for size in [0, 1, 512, 8192, 8193, 65537] {
        let data: Vec<_> = (0..size).map(|n| (n * 53 + size) as u8).collect();
        let d = Descriptor::new(
            (size as u128 + 1).to_be_bytes(),
            1,
            dlm_gateway::wall_seconds() + 3600,
            &data,
        )
        .unwrap();
        assert_eq!(p.upload(&d, &data).unwrap().state, DeliveryState::Completed);
        assert_eq!(p.download(&d).unwrap(), data);
        let before = p.statistics().committed_bytes;
        assert_eq!(p.upload(&d, &data).unwrap().state, DeliveryState::Completed);
        assert_eq!(p.statistics().committed_bytes, before);
        assert_eq!(p.statistics().fec_shards, 0);
        assert_eq!(p.consume(&d).unwrap().state, DeliveryState::Consumed);
    }
}
#[test]
fn offline_preserves_local_queue_zero_goodput_and_uncertain_operation_identity() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, client) = config(&w, &echo, &[Kind::Tls, Kind::Tls]);
    let group = GatewayGroup::start(configs).unwrap();
    let mut p = pool(&group, &client, false);
    let stream = p
        .open(TargetProtocol::Tcp, &echo.address.to_string())
        .unwrap();
    let data = b"buffer until a verified path returns";
    let d = Descriptor::new([89; 16], 3, dlm_gateway::wall_seconds() + 3600, data).unwrap();
    let mut j = Journal::open(&w.0.join("outbox"), [63; 32], 1048576, 64).unwrap();
    assert_eq!(
        delivery::enqueue(&mut j, p.peer(), &d, data).unwrap().state,
        DeliveryState::AcceptedLocal
    );
    drop(group);
    assert!(p.write(stream, b"unknown").is_err());
    assert_eq!(p.write(stream, b"changed").unwrap_err().code, 18);
    assert!(p.flush(&mut j).is_err());
    assert_eq!(p.state(), PoolState::Buffered);
    assert_eq!(p.statistics().goodput_bps, 0);
    assert_eq!(delivery::queued(&j).unwrap().len(), 1);
}
#[test]
fn reorder_recovery_restart_and_consume_never_publish_false_commits() {
    let w = Work::new();
    let owner = [19; 32];
    let data: Vec<_> = (0..20003).map(|i| (i * 37) as u8).collect();
    let d = Descriptor::new([23; 16], 7, dlm_gateway::wall_seconds() + 3600, &data).unwrap();
    let key = [71; 32];
    let path = w.0.join("delivery");
    {
        let j = Arc::new(Mutex::new(Journal::open(&path, key, 1048576, 128).unwrap()));
        let mut store = Store::new(j, 4);
        assert_eq!(
            store.begin(&owner, &d, true).unwrap().state,
            DeliveryState::AcceptedRemote
        );
        assert_eq!(store.complete(&owner, &d).unwrap_err().code, 1);
        for index in (0..d.blocks()).rev() {
            let part = &data
                [index * delivery::BLOCK..index * delivery::BLOCK + d.block_len(index).unwrap()];
            let k = part.len().div_ceil(delivery::SHARD) as u8;
            let group = fec::encode(part, k, k + 4).unwrap();
            let mut committed = false;
            for shard in group.shards.iter().skip(4).rev() {
                let packet = Packet {
                    id: d.id,
                    stream: d.stream,
                    offset: (index * delivery::BLOCK) as u64,
                    length: part.len(),
                    data: k,
                    total: k + 4,
                    shard: shard.clone(),
                    digest: group.digest,
                };
                let ack = store.accept(&owner, &packet.encode()).unwrap();
                assert_eq!(ack.status, 0);
                committed |= ack.state == DeliveryState::Committed;
            }
            assert!(committed);
        }
        assert_eq!(
            store.complete(&owner, &d).unwrap().state,
            DeliveryState::Completed
        );
    }
    {
        let j = Arc::new(Mutex::new(Journal::open(&path, key, 1048576, 128).unwrap()));
        let mut store = Store::new(j, 4);
        assert_eq!(
            store.begin(&owner, &d, true).unwrap().state,
            DeliveryState::Completed
        );
        let mut out = Vec::new();
        for i in 0..d.blocks() {
            out.extend(store.read(&owner, &d, i, 0, delivery::BLOCK).unwrap());
        }
        assert_eq!(out, data);
        assert!(store.begin(&[18; 32], &d, false).is_err());
        assert_eq!(
            store.consume(&owner, &d).unwrap().state,
            DeliveryState::Consumed
        );
        assert_eq!(
            store.begin(&owner, &d, true).unwrap().state,
            DeliveryState::Consumed
        );
        assert!(store.read(&owner, &d, 0, 0, 1).is_err());
    }
}
#[test]
fn a_partial_local_transaction_does_not_enter_the_pending_manifest_list() {
    let w = Work::new();
    let data = vec![31; 4096];
    let d = Descriptor::new([3; 16], 1, dlm_gateway::wall_seconds() + 3600, &data).unwrap();
    let mut j = Journal::open(&w.0, [51; 32], 65536, 1).unwrap();
    assert_eq!(
        delivery::enqueue(&mut j, [17; 32], &d, &data)
            .unwrap_err()
            .code,
        7
    );
    assert!(delivery::queued(&j).unwrap().is_empty());
    assert_eq!(j.count(), 1);
    drop(j);
    let j = Journal::open(&w.0, [51; 32], 65536, 1).unwrap();
    assert!(delivery::queued(&j).unwrap().is_empty());
}
#[test]
fn receive_credit_and_bad_fec_hash_refuse_false_ack() {
    let w = Work::new();
    let j = Arc::new(Mutex::new(Journal::open(&w.0, [7; 32], 65536, 32).unwrap()));
    let mut store = Store::new(j, 1);
    let owner = [11; 32];
    let data = vec![1; 1024];
    let d = Descriptor::new([1; 16], 1, dlm_gateway::wall_seconds() + 3600, &data).unwrap();
    let other = Descriptor::new([2; 16], 1, d.expires, &data).unwrap();
    store.begin(&owner, &d, true).unwrap();
    store.begin(&owner, &other, true).unwrap();
    let code = fec::encode(&data, 2, 2).unwrap();
    let packet = |id, shard| Packet {
        id,
        stream: 1,
        offset: 0,
        length: data.len(),
        data: 2,
        total: 2,
        shard,
        digest: code.digest,
    };
    let first = packet(d.id, code.shards[0].clone());
    assert_eq!(
        store.accept(&owner, &first.encode()).unwrap().state,
        DeliveryState::AcceptedRemote
    );
    assert_eq!(store.begin(&owner, &other, false).unwrap().credit, 0);
    assert_eq!(
        store
            .accept(&owner, &packet(other.id, code.shards[0].clone()).encode())
            .unwrap()
            .status,
        7
    );
    let mut bad = code.shards[1].clone();
    bad.bytes[0] ^= 1;
    let r = store.accept(&owner, &packet(d.id, bad).encode()).unwrap();
    assert_eq!(r.status, 12);
    assert_eq!(r.state, DeliveryState::AcceptedRemote);
    assert_eq!(store.complete(&owner, &d).unwrap_err().code, 1);
    assert_eq!(store.pending_groups(), 1);
}
#[test]
fn systematic_fec_iid_erasure_gain_does_not_invent_delivery_during_full_outage() {
    let bytes = vec![67; 1024];
    let group = fec::encode(&bytes, 8, 12).unwrap();
    let mut seed = 148u32;
    let (mut recovered, mut plain) = (0, 0);
    for _ in 0..500 {
        let mut shards = Vec::new();
        let mut raw = true;
        for shard in &group.shards {
            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let erased = seed % 100 < 10;
            if !erased {
                shards.push(shard.clone());
            } else if shard.index < 8 {
                raw = false;
            }
        }
        plain += raw as usize;
        if shards.len() >= 8 {
            assert_eq!(
                fec::decode(&shards, 8, 12, bytes.len(), &group.digest).unwrap(),
                bytes
            );
            recovered += 1;
        }
    }
    assert!(recovered > plain + 150);
    assert!(recovered > 490);
    assert!(fec::decode(&[], 8, 12, bytes.len(), &group.digest).is_err());
}
#[test]
fn stale_path_witness_is_not_reported_ready_or_given_positive_goodput() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, base) = config(&w, &echo, &[Kind::Tls]);
    let group = GatewayGroup::start(configs).unwrap();
    let mut candidate = base.clone();
    candidate.gateway = group.gateways[0].address;
    let mut policy = PoolPolicy::default();
    policy.probe_ms = 50;
    policy.max_age_ms = 50;
    let mut p = Pool::new(
        vec![Candidate {
            config: candidate,
            source: None,
            domains: Domains::shared(),
            cost: 0,
        }],
        policy,
    )
    .unwrap();
    p.refresh().unwrap();
    thread::sleep(Duration::from_millis(65));
    assert_eq!(p.statistics().ready_paths, 0);
    assert_eq!(p.statistics().goodput_bps, 0);
    assert_eq!(p.state(), PoolState::Offline);
    p.refresh().unwrap();
    assert_eq!(p.statistics().ready_paths, 1);
}
#[test]
fn distinct_gateway_stores_cannot_be_silently_joined() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (mut configs, base) = config(&w, &echo, &[Kind::Tls]);
    let first = GatewayGroup::start(configs.clone()).unwrap();
    let key = configs[0].spool.as_ref().unwrap().1.clone();
    configs[0].spool = Some((w.0.join("another-inbox"), key));
    configs[0].locator_output = None;
    let second = GatewayGroup::start(configs).unwrap();
    let mut a = base.clone();
    a.locator = None;
    a.gateway = first.gateways[0].address;
    let mut b = a.clone();
    b.gateway = second.gateways[0].address;
    let mut p = Pool::new(
        vec![
            Candidate {
                config: a,
                source: None,
                domains: Domains::shared(),
                cost: 0,
            },
            Candidate {
                config: b,
                source: None,
                domains: Domains::shared(),
                cost: 1,
            },
        ],
        PoolPolicy::default(),
    )
    .unwrap();
    p.refresh().unwrap();
    assert_eq!(p.statistics().ready_paths, 1);
    assert_eq!(p.metrics()[1].last_error, 16);
    assert_eq!(p.statistics().independent_pairs, 0);
}
#[test]
fn corrupt_persisted_storage_identity_refuses_gateway_startup() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, _) = config(&w, &echo, &[Kind::Tls]);
    let identity = configs[0].spool.as_ref().unwrap().0.join("delivery-v2/journal.lock");
    drop(GatewayGroup::start(configs.clone()).unwrap());
    let mut bytes = fs::read(&identity).unwrap();
    assert_eq!(bytes.len(), 92);
    bytes[91] ^= 1;
    fs::write(&identity, bytes).unwrap();
    assert!(GatewayGroup::start(configs.clone()).is_err());
    fs::write(&identity, [1; 7]).unwrap();
    assert!(GatewayGroup::start(configs).is_err());
}
#[test]
fn ipv6_only_tls_with_bound_source_preserves_binary_payload() {
    let w = Work::new();
    let echo = Echo::new(true);
    let (configs, client) = config(&w, &echo, &[Kind::Tls]);
    let group = GatewayGroup::start(configs).unwrap();
    let mut p = pool(&group, &client, true);
    let stream = p
        .open(TargetProtocol::Tcp, &echo.address.to_string())
        .unwrap();
    let data = b"IPv6 binary\0\xff";
    p.write(stream, data).unwrap();
    let mut out = Vec::new();
    while out.len() < data.len() {
        out.extend(p.read(stream, data.len() - out.len()).unwrap().data);
    }
    assert_eq!(out, data);
}
#[test]
#[ignore = "requires pinned QUIC provider; acceptance runs with --include-ignored"]
fn all_three_carriers_share_delivery_and_raw_tcp_without_faking_independence() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, client) = config(
        &w,
        &echo,
        &[Kind::Tls, Kind::QuicStream, Kind::QuicDatagram],
    );
    let mut group = GatewayGroup::start(configs).unwrap();
    let mut p = pool(&group, &client, false);
    p.refresh().unwrap();
    assert_eq!(p.statistics().ready_paths, 3);
    let data: Vec<_> = (0..131089).map(|i| (i * 17) as u8).collect();
    let d = Descriptor::new([73; 16], 2, dlm_gateway::wall_seconds() + 3600, &data).unwrap();
    assert_eq!(p.upload(&d, &data).unwrap().state, DeliveryState::Completed);
    let active = p.statistics().active as usize - 1;
    drop(group.gateways.remove(active));
    assert_eq!(p.download(&d).unwrap(), data);
    assert!(p.statistics().migrations > 0);
    assert_eq!(p.statistics().independent_pairs, 0);
}

#[test]
fn signed_gateway_epoch_high_water_survives_pool_restart() {
    let w = Work::new();
    let echo = Echo::new(false);
    let (configs, client) = config(&w, &echo, &[Kind::Tls]);
    let original = fs::read(&configs[0].identity).unwrap();
    Identity::rotate(&configs[0].identity).unwrap();
    let group = GatewayGroup::start(configs.clone()).unwrap();
    let mut c = client.clone();
    c.gateway = group.gateways[0].address;
    c.locator = None;
    let candidate = Candidate {
        config: c,
        source: None,
        domains: Domains::shared(),
        cost: 0,
    };
    let cache = w.0.join("trust.dlr");
    let mut p =
        Pool::new_with_trust(vec![candidate], PoolPolicy::default(), Some(cache.clone())).unwrap();
    p.refresh().unwrap();
    drop(p);
    drop(group);
    assert_eq!(
        dlm_gateway::identity::Record::historical(&fs::read(&cache).unwrap())
            .unwrap()
            .epoch,
        2
    );
    fs::write(&configs[0].identity, original).unwrap();
    let mut old_configs = configs;
    old_configs[0].locator_output = None;
    let old = GatewayGroup::start(old_configs).unwrap();
    let mut c = client;
    c.gateway = old.gateways[0].address;
    c.locator = None;
    let mut p = Pool::new_with_trust(
        vec![Candidate {
            config: c.clone(),
            source: None,
            domains: Domains::shared(),
            cost: 0,
        }],
        PoolPolicy::default(),
        Some(cache.clone()),
    )
    .unwrap();
    assert_eq!(p.refresh().unwrap_err().code, 12);
    assert_eq!(p.statistics().ready_paths, 0);
    assert_eq!(old.statistics().authenticated, 0);
    let mut corrupt = fs::read(&cache).unwrap();
    let n = corrupt.len();
    corrupt[n - 1] ^= 1;
    fs::write(&cache, corrupt).unwrap();
    assert!(Pool::new_with_trust(
        vec![Candidate {
            config: c,
            source: None,
            domains: Domains::shared(),
            cost: 0
        }],
        PoolPolicy::default(),
        Some(cache)
    )
    .is_err());
}
#[cfg(unix)]
#[test]
fn atomic_store_write_failure_never_becomes_a_committed_ack() {
    struct Restore(PathBuf, PathBuf);
    impl Drop for Restore {
        fn drop(&mut self) {
            let _ = fs::remove_file(&self.0);
            let _ = fs::rename(&self.1, &self.0);
        }
    }
    let w = Work::new();
    let dir = w.0.join("store");
    let j = Arc::new(Mutex::new(
        Journal::open(&dir, [31; 32], 65536, 16).unwrap(),
    ));
    let mut store = Store::new(j, 2);
    let data = b"commit only after disk sync";
    let d = Descriptor::new([47; 16], 1, dlm_gateway::wall_seconds() + 3600, data).unwrap();
    store.begin(&[19; 32], &d, true).unwrap();
    let code = fec::encode(data, 1, 1).unwrap();
    let packet = Packet {
        id: d.id,
        stream: d.stream,
        offset: 0,
        length: data.len(),
        data: 1,
        total: 1,
        shard: code.shards[0].clone(),
        digest: code.digest,
    };
    let saved = w.0.join("store-saved");
    fs::rename(&dir, &saved).unwrap();
    let restore = Restore(dir.clone(), saved);
    fs::write(&dir, b"not a directory").unwrap();
    let ack = store.accept(&[19; 32], &packet.encode()).unwrap();
    assert_eq!(ack.status, 5);
    assert_eq!(ack.state, DeliveryState::AcceptedRemote);
    assert_eq!(store.complete(&[19; 32], &d).unwrap_err().code, 1);
    drop(restore);
    assert!(dir.is_dir());
    assert_eq!(
        store.accept(&[19; 32], &packet.encode()).unwrap().state,
        DeliveryState::Committed
    );
    assert_eq!(
        store.complete(&[19; 32], &d).unwrap().state,
        DeliveryState::Completed
    );
}
