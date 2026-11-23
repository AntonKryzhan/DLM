use dlm_gateway::{
    fec,
    gateway::{Client, ClientConfig, Gateway, Outcome, ServerConfig},
    identity::{hex, Identity},
    journal::{Journal, State},
    policy::{Policy, TargetProtocol, TargetRule},
    transport::Kind,
};
use std::{
    fs,
    io::{Read, Write},
    net::{TcpListener, UdpSocket},
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc,
    },
    thread,
    time::Duration,
};
static SERIAL: AtomicUsize = AtomicUsize::new(0);
struct Work(PathBuf);
impl Work {
    fn new() -> Self {
        let p = std::env::temp_dir().join(format!(
            "dlm-gateway-{}-{}",
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
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
struct Echo {
    address: std::net::SocketAddr,
    stop: Arc<AtomicBool>,
    worker: Option<thread::JoinHandle<()>>,
}
impl Drop for Echo {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            worker.join().unwrap();
        }
    }
}
fn tcp_echo() -> Echo {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    listener.set_nonblocking(true).unwrap();
    let stop = Arc::new(AtomicBool::new(false));
    let stopped = stop.clone();
    let worker = thread::spawn(move || {
        while !stopped.load(Ordering::Acquire) {
            match listener.accept() {
                Ok((mut socket, _)) => {
                    socket
                        .set_read_timeout(Some(Duration::from_millis(20)))
                        .unwrap();
                    let mut bytes = [0; 32768];
                    while !stopped.load(Ordering::Acquire) {
                        match socket.read(&mut bytes) {
                            Ok(0) => break,
                            Ok(n) => {
                                if socket.write_all(&bytes[..n]).is_err() {
                                    break;
                                }
                            }
                            Err(e)
                                if matches!(
                                    e.kind(),
                                    std::io::ErrorKind::TimedOut | std::io::ErrorKind::WouldBlock
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
    Echo {
        address,
        stop,
        worker: Some(worker),
    }
}
fn udp_echo() -> Echo {
    let socket = UdpSocket::bind("127.0.0.1:0").unwrap();
    let address = socket.local_addr().unwrap();
    socket
        .set_read_timeout(Some(Duration::from_millis(20)))
        .unwrap();
    let stop = Arc::new(AtomicBool::new(false));
    let stopped = stop.clone();
    let worker = thread::spawn(move || {
        let mut data = [0; 65536];
        while !stopped.load(Ordering::Acquire) {
            if let Ok((n, peer)) = socket.recv_from(&mut data) {
                socket.send_to(&data[..n], peer).unwrap();
            }
        }
    });
    Echo {
        address,
        stop,
        worker: Some(worker),
    }
}
fn configuration(
    w: &Work,
    kind: Kind,
    target: &Echo,
    protocol: TargetProtocol,
    private: bool,
) -> (ServerConfig, ClientConfig) {
    let server = w.0.join("server.identity");
    let client = w.0.join("client.identity");
    let s = Identity::generate(&server).unwrap();
    let c = Identity::generate(&client).unwrap();
    let mut policy = Policy::new();
    policy.client(&hex(&c.public().unwrap().root)).unwrap();
    policy.targets.push(
        TargetRule::parse(&format!(
            "{},127.0.0.0/8,{}",
            if protocol == TargetProtocol::Tcp {
                "tcp"
            } else {
                "udp"
            },
            target.address.port()
        ))
        .unwrap(),
    );
    policy.allow_private = private;
    policy.limits.idle_ms = 2000;
    let f = root().join("examples/native/fixtures");
    let config = ServerConfig {
        bind: "127.0.0.1:0".parse().unwrap(),
        advertise: None,
        kind,
        identity: server,
        certificate: f.join("quic-test-server.pem"),
        key: f.join("quic-test-key.pem"),
        locator_output: Some(w.0.join("locator.dlr")),
        policy,
        spool: None,
    };
    let client = ClientConfig {
        gateway: "127.0.0.1:1".parse().unwrap(),
        hostname: "localhost".into(),
        ca: f.join("quic-test-ca.pem").to_string_lossy().into_owned(),
        kind,
        identity: client,
        peer: s.public().unwrap().root,
        minimum_epoch: 1,
        minimum_minor: 0,
        maximum_minor: 3,
        timeout_ms: 5000,
        locator: Some(w.0.join("locator.dlr")),
    };
    (config, client)
}

fn exchange(kind: Kind, protocol: TargetProtocol) {
    let w = Work::new();
    let echo = if protocol == TargetProtocol::Tcp {
        tcp_echo()
    } else {
        udp_echo()
    };
    let (server, mut config) = configuration(&w, kind, &echo, protocol, true);
    let gateway = Gateway::start(server).unwrap();
    config.gateway = gateway.address;
    let mut client = Client::connect(&config).unwrap();
    assert_eq!(client.minor(), 3);
    let stream = client.open(protocol, &echo.address.to_string()).unwrap();
    for size in [0, 1, 127, 767, 768] {
        let bytes: Vec<_> = (0..size).map(|i| ((i * 47 + size) % 256) as u8).collect();
        if protocol == TargetProtocol::Udp {
            assert_eq!(client.datagram(stream, &bytes).unwrap(), bytes);
        } else {
            assert_eq!(client.write(stream, &bytes).unwrap(), size);
            let mut output = Vec::new();
            while output.len() < size {
                let r = client.read(stream, size - output.len()).unwrap();
                assert_eq!(r.outcome, Outcome::EndpointData);
                output.extend(r.data);
            }
            assert_eq!(output, bytes);
        }
    }
    if protocol == TargetProtocol::Tcp {
        client.close(stream, true).unwrap();
        assert_eq!(
            client.read(stream, 1).unwrap().outcome,
            Outcome::EndOfStream
        );
    }
    client.close(stream, false).unwrap();
    drop(client);
    assert_eq!(gateway.statistics().authenticated, 1);
    drop(gateway);
}
#[test]
fn tls_tcp_and_udp_forward_to_real_endpoints() {
    exchange(Kind::Tls, TargetProtocol::Tcp);
    exchange(Kind::Tls, TargetProtocol::Udp);
}
#[test]
#[ignore = "requires pinned QUIC provider; acceptance runs with --include-ignored"]
fn quic_stream_tcp_and_udp_forward_to_real_endpoints() {
    exchange(Kind::QuicStream, TargetProtocol::Tcp);
    exchange(Kind::QuicStream, TargetProtocol::Udp);
}
#[test]
#[ignore = "requires pinned QUIC provider; acceptance runs with --include-ignored"]
fn quic_datagrams_keep_message_boundaries_and_forward_both_destination_kinds() {
    exchange(Kind::QuicDatagram, TargetProtocol::Tcp);
    exchange(Kind::QuicDatagram, TargetProtocol::Udp);
}
#[test]
fn private_and_wrong_port_destinations_are_denied_before_connect() {
    let w = Work::new();
    let echo = tcp_echo();
    let (server, mut config) = configuration(&w, Kind::Tls, &echo, TargetProtocol::Tcp, false);
    let gateway = Gateway::start(server).unwrap();
    config.gateway = gateway.address;
    let mut client = Client::connect(&config).unwrap();
    assert_eq!(
        client
            .open(TargetProtocol::Tcp, &echo.address.to_string())
            .unwrap_err()
            .code,
        16
    );
    assert_eq!(gateway.statistics().forwarded, 0);
}
#[test]
fn unpinned_identity_bad_certificate_and_stale_key_epoch_fail() {
    for mode in 0..3 {
        let w = Work::new();
        let echo = tcp_echo();
        let (mut server, mut config) =
            configuration(&w, Kind::Tls, &echo, TargetProtocol::Tcp, true);
        if mode == 0 {
            let stranger = Identity::generate(&w.0.join("other.identity")).unwrap();
            server.policy.clients.clear();
            server
                .policy
                .client(&hex(&stranger.public().unwrap().root))
                .unwrap();
        }
        if mode == 1 {
            config.ca = root()
                .join("examples/native/fixtures/quic-test-untrusted-ca.pem")
                .to_string_lossy()
                .into_owned();
        }
        if mode == 2 {
            config.minimum_epoch = 2;
        }
        let gateway = Gateway::start(server).unwrap();
        config.gateway = gateway.address;
        assert!(Client::connect(&config).is_err());
        assert_eq!(gateway.statistics().forwarded, 0);
    }
}
#[test]
fn signed_locator_tampering_and_carrier_substitution_are_refused() {
    let w = Work::new();
    let echo = tcp_echo();
    let (server, mut config) = configuration(&w, Kind::Tls, &echo, TargetProtocol::Tcp, true);
    let gateway = Gateway::start(server).unwrap();
    config.gateway = gateway.address;
    let path = config.locator.as_ref().unwrap();
    let mut bytes = fs::read(path).unwrap();
    bytes[226] ^= 1;
    fs::write(path, bytes).unwrap();
    assert!(Client::connect(&config).is_err());
    assert_eq!(gateway.statistics().accepted, 0);
}
#[test]
fn durable_outbox_reopens_and_gateway_commit_survives_server_restart() {
    let w = Work::new();
    let echo = tcp_echo();
    let (mut server, mut config) = configuration(&w, Kind::Tls, &echo, TargetProtocol::Tcp, true);
    let key_path = w.0.join("wal.key");
    dlm_gateway::journal::create_key(&key_path).unwrap();
    let key = dlm_gateway::journal::load_key(&key_path).unwrap();
    server.spool = Some((w.0.join("inbox"), key_path));
    let id = [31; 16];
    let data = b"durable binary\0message";
    {
        let mut outbox = Journal::open(&w.0.join("outbox"), key, 65536, 16).unwrap();
        outbox
            .put(
                id,
                config.peer,
                dlm_gateway::wall_seconds() + 3600,
                data,
                State::Pending,
            )
            .unwrap();
    }
    let address;
    {
        let gateway = Gateway::start(server.clone()).unwrap();
        address = gateway.address;
        config.gateway = address;
        let mut client = Client::connect(&config).unwrap();
        let mut outbox = Journal::open(&w.0.join("outbox"), key, 65536, 16).unwrap();
        assert_eq!(client.flush_outbox(&mut outbox).unwrap(), 1);
        assert_eq!(outbox.get(&id).unwrap().state, State::Acknowledged);
        assert_eq!(client.fetch(id).unwrap(), data);
        drop(client);
        drop(gateway);
    }
    server.bind = address;
    let gateway = Gateway::start(server).unwrap();
    config.gateway = gateway.address;
    let mut client = Client::connect(&config).unwrap();
    assert_eq!(client.fetch(id).unwrap(), data);
    client.consume(id).unwrap();
}
#[test]
fn journal_rejects_wrong_keys_corruption_conflicting_ids_and_quota_without_losing_commits() {
    let w = Work::new();
    let path = w.0.join("journal");
    let id = [7; 16];
    let expiry = dlm_gateway::wall_seconds() + 3600;
    {
        let mut j = Journal::open(&path, [5; 32], 1024, 2).unwrap();
        j.put(id, [9; 32], expiry, b"unchanged", State::Committed)
            .unwrap();
        assert_eq!(
            j.put(id, [9; 32], expiry, b"changed", State::Committed)
                .unwrap_err()
                .code,
            18
        );
        assert_eq!(
            j.put([8; 16], [9; 32], expiry, &vec![0; 900], State::Committed)
                .unwrap_err()
                .code,
            7
        );
        assert!(Journal::open(&path, [5; 32], 1024, 2).is_err());
    }
    assert!(Journal::open(&path, [6; 32], 1024, 2).is_err());
    {
        let j = Journal::open(&path, [5; 32], 1024, 2).unwrap();
        assert_eq!(j.get(&id).unwrap().payload, b"unchanged");
    }
    let file = path.join(format!("{}.dgw", hex(&id)));
    let mut bytes = fs::read(&file).unwrap();
    let last = bytes.len() - 1;
    bytes[last] ^= 1;
    fs::write(file, bytes).unwrap();
    assert!(Journal::open(&path, [5; 32], 1024, 2).is_err());
}
#[test]
fn systematic_fec_recovers_every_six_of_ten_and_detects_corruption() {
    let bytes: Vec<_> = (0..1027).map(|i| (i * 19) as u8).collect();
    let group = fec::encode(&bytes, 6, 10).unwrap();
    for mask in 0u32..1024 {
        if mask.count_ones() != 6 {
            continue;
        }
        let shards: Vec<_> = group
            .shards
            .iter()
            .filter(|s| mask & (1 << s.index) != 0)
            .cloned()
            .collect();
        assert_eq!(
            fec::decode(&shards, 6, 10, bytes.len(), &group.digest).unwrap(),
            bytes
        );
    }
    let mut shards = group.shards[..6].to_vec();
    shards[0].bytes[0] ^= 1;
    assert_eq!(
        fec::decode(&shards, 6, 10, bytes.len(), &group.digest)
            .unwrap_err()
            .code,
        12
    );
    assert!(fec::decode(&group.shards[..5], 6, 10, bytes.len(), &group.digest).is_err());
}
#[test]
fn network_spec_and_mixed_family_policy_bounds_are_checked() {
    dlm_gateway::protocol::specification().validate().unwrap();
    let rule = TargetRule::parse("tcp,2001:db8::/64,443").unwrap();
    assert!(!rule.accepts(TargetProtocol::Tcp, "127.0.0.1:443".parse().unwrap()));
    assert!(rule.accepts(TargetProtocol::Tcp, "[2001:db8::5]:443".parse().unwrap()));
}
