use dlm_gateway::{
    config, ensure_public,
    gateway::{Client, GatewayGroup},
    identity::{hex, read_bounded, replace, unhex, write_new, Identity},
    journal::{self, Journal, State},
    policy::TargetProtocol,
    Error, Result,
};
use std::{
    collections::BTreeMap,
    fs::{self, File},
    io::Read,
    path::{Path, PathBuf},
    time::Duration,
};
struct Options(BTreeMap<String, String>);
impl Options {
    fn parse(args: &[String]) -> Result<Self> {
        ensure_public(
            args.len() <= 64 && args.len() % 2 == 0,
            4,
            "options require --name value pairs",
        )?;
        let mut values = BTreeMap::new();
        for pair in args.chunks(2) {
            ensure_public(
                pair[0].starts_with("--") && pair[0].len() <= 64 && pair[1].len() <= 4096,
                4,
                "invalid command option",
            )?;
            ensure_public(
                values
                    .insert(pair[0][2..].to_owned(), pair[1].clone())
                    .is_none(),
                4,
                "duplicate command option",
            )?;
        }
        Ok(Self(values))
    }
    fn take(&mut self, key: &str) -> Result<String> {
        self.0
            .remove(key)
            .ok_or(Error::new(4, "required command option absent"))
    }
    fn optional(&mut self, key: &str) -> Option<String> {
        self.0.remove(key)
    }
    fn finish(&self) -> Result<()> {
        ensure_public(self.0.is_empty(), 4, "unknown command option")
    }
}
fn configuration(path: &Path) -> Result<(String, PathBuf)> {
    let bytes = read_bounded(path, 65536)?;
    let text =
        String::from_utf8(bytes).map_err(|_| Error::new(4, "configuration must be UTF-8"))?;
    let base = fs::canonicalize(path)?
        .parent()
        .unwrap_or(Path::new("."))
        .to_path_buf();
    Ok((text, base))
}
fn identity_result(identity: &Identity) -> Result<()> {
    let record = identity.public()?;
    println!(
        "{{\"root\":\"{}\",\"identity\":\"{}\",\"epoch\":{}}}",
        hex(&record.root),
        hex(&record.id),
        record.epoch
    );
    Ok(())
}
fn run(command: &str, args: &[String]) -> Result<()> {
    let mut options = Options::parse(args)?;
    match command {
        "proxy-serve" | "proxy-exchange" => return proxy_command(command, &mut options),
        "capabilities" => {
            options.finish()?;
            println!("{}", dlm_gateway::proxy::capabilities());
            Ok(())
        }
        "keygen" | "rotate" | "identity" => {
            let path = PathBuf::from(options.take("identity")?);
            options.finish()?;
            let identity = match command {
                "keygen" => Identity::generate(&path)?,
                "rotate" => Identity::rotate(&path)?,
                _ => Identity::load(&path)?,
            };
            identity_result(&identity)
        }
        "journal-keygen" | "proxy-keygen" => {
            let path = PathBuf::from(options.take("output")?);
            options.finish()?;
            journal::create_key(&path)?;
            println!("{{\"status\":\"created\"}}");
            Ok(())
        }
        "descriptor" => {
            let path = PathBuf::from(options.take("output")?);
            options.finish()?;
            let spec = dlm_gateway::protocol::specification();
            spec.validate()
                .map_err(|_| Error::new(11, "gateway descriptor invalid"))?;
            write_new(&path, &spec.to_bytes())?;
            println!("{{\"status\":\"written\",\"assurance\":\"engineering\",\"native_proof_pending\":true}}");
            Ok(())
        }
        "serve" => {
            let path = PathBuf::from(options.take("config")?);
            let duration = options
                .optional("duration")
                .map(|v| {
                    v.parse::<u64>()
                        .map_err(|_| Error::new(4, "invalid run duration"))
                })
                .transpose()?;
            options.finish()?;
            let (text, base) = configuration(&path)?;
            let gateway = GatewayGroup::start(config::server_group(&text, &base)?)?;
            println!(
                "{{\"status\":\"READY\",\"address\":\"{}\",\"listeners\":[{}]}}",
                gateway.gateways[0].address,
                gateway
                    .addresses()
                    .iter()
                    .map(|(a, k)| format!("{{\"address\":\"{a}\",\"transport\":\"{}\"}}", k.name()))
                    .collect::<Vec<_>>()
                    .join(",")
            );
            let started = std::time::Instant::now();
            loop {
                if duration.is_some_and(|v| started.elapsed() >= Duration::from_secs(v)) {
                    break;
                }
                if dlm_gateway::interrupted() {
                    break;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            gateway.stop();
            let stats = gateway.statistics();
            drop(gateway);
            println!(
                "{{\"status\":\"stopped\",\"authenticated\":{},\"requests\":{},\"denied\":{}}}",
                stats.authenticated, stats.requests, stats.denied
            );
            Ok(())
        }
        "pool-status" | "pool-upload" | "pool-download" | "pool-consume" | "pool-queue"
        | "pool-flush" | "continuity-model" => return continuity_command(command, &mut options),
        "exchange" => {
            let path = PathBuf::from(options.take("config")?);
            let protocol = TargetProtocol::parse(&options.take("protocol")?)?;
            let target = options.take("target")?;
            let input = PathBuf::from(options.take("input")?);
            let output = PathBuf::from(options.take("output")?);
            options.finish()?;
            let (text, base) = configuration(&path)?;
            let mut client = configured_client(&text, &base)?;
            let stream = client.open(protocol, &target)?;
            let mut reader = File::open(input)?;
            let mut result = Vec::new();
            let mut block = vec![0; client.maximum_data()];
            let mut sent = 0u64;
            loop {
                let n = reader.read(&mut block)?;
                if n == 0 {
                    if sent == 0 && protocol == TargetProtocol::Udp {
                        result.extend_from_slice(&client.datagram(stream, &[])?);
                    }
                    break;
                }
                if protocol == TargetProtocol::Udp {
                    result.extend_from_slice(&client.datagram(stream, &block[..n])?);
                } else {
                    client.write(stream, &block[..n])?;
                    let mut remaining = n;
                    while remaining > 0 {
                        let reply = client.read(stream, remaining)?;
                        ensure_public(
                            !reply.data.is_empty(),
                            17,
                            "destination closed before the expected exchange response",
                        )?;
                        remaining -= reply.data.len();
                        result.extend_from_slice(&reply.data);
                    }
                }
                sent += n as u64;
                ensure_public(
                    result.len() <= 67108864,
                    7,
                    "exchange output quota exceeded",
                )?;
            }
            client.close(stream, false)?;
            replace(&output, &result)?;
            println!(
                "{{\"status\":\"endpoint_response\",\"sent\":{},\"received\":{},\"minor\":{}}}",
                sent,
                result.len(),
                client.minor()
            );
            Ok(())
        }
        "enqueue" => {
            let directory = PathBuf::from(options.take("outbox")?);
            let key = journal::load_key(Path::new(&options.take("key")?))?;
            let peer = unhex(&options.take("peer")?)?;
            let input = read_bounded(Path::new(&options.take("input")?), 32768)?;
            let ttl = options
                .optional("ttl")
                .unwrap_or_else(|| "3600".into())
                .parse::<u64>()
                .map_err(|_| Error::new(4, "invalid message TTL"))?;
            ensure_public(ttl > 0 && ttl <= 86400, 4, "message TTL outside bound")?;
            let id = options
                .optional("message-id")
                .map(|s| unhex(&s))
                .transpose()?
                .unwrap_or(dlm_gateway::message_id()?);
            options.finish()?;
            let mut journal = Journal::open(&directory, key, 67108864, 1024)?;
            journal.put(
                id,
                peer,
                dlm_gateway::wall_seconds() + ttl,
                &input,
                State::Pending,
            )?;
            println!(
                "{{\"status\":\"durable_local\",\"message\":\"{}\"}}",
                hex(&id)
            );
            Ok(())
        }
        "flush" => {
            let path = PathBuf::from(options.take("config")?);
            let directory = PathBuf::from(options.take("outbox")?);
            let key = journal::load_key(Path::new(&options.take("key")?))?;
            options.finish()?;
            let (text, base) = configuration(&path)?;
            let mut client = configured_client(&text, &base)?;
            let mut journal = Journal::open(&directory, key, 67108864, 1024)?;
            let count = client.flush_outbox(&mut journal)?;
            println!("{{\"status\":\"durable_remote\",\"messages\":{count}}}");
            Ok(())
        }
        "fetch" | "consume" => {
            let path = PathBuf::from(options.take("config")?);
            let id = unhex(&options.take("message-id")?)?;
            let output = options.optional("output");
            options.finish()?;
            let (text, base) = configuration(&path)?;
            let mut client = configured_client(&text, &base)?;
            if command == "fetch" {
                let output = output.ok_or(Error::new(4, "fetch requires an output path"))?;
                let bytes = client.fetch(id)?;
                replace(Path::new(&output), &bytes)?;
                println!("{{\"status\":\"retrieved\",\"bytes\":{}}}", bytes.len());
            } else {
                ensure_public(output.is_none(), 4, "consume does not accept output")?;
                client.consume(id)?;
                println!("{{\"status\":\"consumed\"}}");
            }
            Ok(())
        }
        _ => Err(Error::new(4, "unknown dlm-net command")),
    }
}
fn continuity_command(command: &str, options: &mut Options) -> Result<()> {
    if command == "continuity-model" {
        let output = PathBuf::from(options.take("output")?);
        options.finish()?;
        let checked = dlm_gateway::continuity::audit()?;
        replace(&output, checked.as_bytes())?;
        println!("{checked}");
        return Ok(());
    }
    if command == "pool-queue" {
        let path = PathBuf::from(options.take("config")?);
        let input = PathBuf::from(options.take("input")?);
        let output = PathBuf::from(options.take("descriptor")?);
        let id = options
            .optional("message-id")
            .map(|v| unhex(&v))
            .transpose()?
            .unwrap_or(dlm_gateway::message_id()?);
        let stream = number_option(options, "stream", 1)?;
        let ttl = number_option(options, "ttl", 3600)?;
        options.finish()?;
        let bytes = read_bounded(&input, dlm_gateway::delivery::MAX_TRANSFER)?;
        let desc = dlm_gateway::delivery::Descriptor::new(
            id,
            stream,
            dlm_gateway::wall_seconds()
                .checked_add(ttl)
                .ok_or(Error::new(4, "delivery TTL overflow"))?,
            &bytes,
        )?;
        let (text, base) = configuration(&path)?;
        let (mut journal, peer) = config::outbox(&text, &base)?;
        dlm_gateway::delivery::enqueue(&mut journal, peer, &desc, &bytes)?;
        replace(&output, &desc.encode())?;
        println!(
            "{{\"status\":\"accepted_local\",\"message\":\"{}\",\"bytes\":{}}}",
            hex(&desc.id),
            desc.length
        );
        return Ok(());
    }
    let path = PathBuf::from(options.take("config")?);
    let (text, base) = configuration(&path)?;
    let mut pool = config::pool(&text, &base)?;
    match command {
        "pool-status" => {
            options.finish()?;
            let _ = pool.refresh();
            let v = pool.statistics();
            println!("{{\"state\":{},\"ready_paths\":{},\"configured_paths\":{},\"goodput_bps\":{},\"independent_pairs\":{},\"declared_access_domains\":{}}}",v.state,v.ready_paths,v.configured_paths,v.goodput_bps,v.independent_pairs,v.declared_access_domains);
        }
        "pool-flush" => {
            let path = PathBuf::from(options.take("outbox-config")?);
            options.finish()?;
            let (text, base) = configuration(&path)?;
            let (mut journal, peer) = config::outbox(&text, &base)?;
            ensure_public(
                peer == pool.peer(),
                16,
                "outbox belongs to a different gateway",
            )?;
            let count = pool.flush(&mut journal)?;
            println!("{{\"status\":\"completed_remote\",\"messages\":{count}}}");
        }
        "pool-upload" => {
            let input = PathBuf::from(options.take("input")?);
            let output = PathBuf::from(options.take("descriptor")?);
            let supplied_id = options
                .optional("message-id")
                .map(|v| unhex::<16>(&v))
                .transpose()?;
            let ttl = number_option(options, "ttl", 3600)?;
            let stream = number_option(options, "stream", 1)?;
            options.finish()?;
            let bytes = read_bounded(&input, dlm_gateway::delivery::MAX_TRANSFER)?;
            let desc = if output.exists() {
                let value = dlm_gateway::delivery::Descriptor::decode(&read_bounded(&output, 80)?)?;
                ensure_public(
                    supplied_id.is_none_or(|id| id == value.id),
                    18,
                    "existing delivery descriptor identifier differs",
                )?;
                value
            } else {
                let value = dlm_gateway::delivery::Descriptor::new(
                    supplied_id.unwrap_or(dlm_gateway::message_id()?),
                    stream,
                    dlm_gateway::wall_seconds()
                        .checked_add(ttl)
                        .ok_or(Error::new(4, "delivery TTL overflow"))?,
                    &bytes,
                )?;
                write_new(&output, &value.encode())?;
                value
            };
            let result = pool.upload(&desc, &bytes)?;
            let stats = pool.statistics();
            println!("{{\"status\":\"completed_remote\",\"state\":{},\"bytes\":{},\"retransmitted\":{},\"fec_shards\":{}}}",result.state as u32,desc.length,stats.retransmitted,stats.fec_shards);
        }
        "pool-download" | "pool-consume" => {
            let descriptor = PathBuf::from(options.take("descriptor")?);
            let output = options.optional("output");
            options.finish()?;
            let desc = dlm_gateway::delivery::Descriptor::decode(&read_bounded(&descriptor, 80)?)?;
            if command == "pool-download" {
                let output = output.ok_or(Error::new(4, "pool-download requires output"))?;
                let bytes = pool.download(&desc)?;
                replace(Path::new(&output), &bytes)?;
                println!(
                    "{{\"status\":\"verified_download\",\"bytes\":{}}}",
                    bytes.len()
                );
            } else {
                ensure_public(output.is_none(), 4, "pool-consume does not accept output")?;
                pool.consume(&desc)?;
                println!("{{\"status\":\"consumed\"}}");
            }
        }
        _ => return Err(Error::new(4, "unknown continuity command")),
    }
    Ok(())
}
fn number_option(options: &mut Options, name: &str, default: u64) -> Result<u64> {
    options
        .optional(name)
        .map(|v| {
            v.parse()
                .map_err(|_| Error::new(4, "invalid numeric command option"))
        })
        .transpose()
        .map(|v| v.unwrap_or(default))
}

fn configured_client(text: &str, base: &Path) -> Result<Client> {
    let (c, p) = config::client_with_proxy(text, base)?;
    Client::connect_with_proxy(&c, None, p.as_ref())
}
fn proxy_command(command: &str, options: &mut Options) -> Result<()> {
    let path = PathBuf::from(options.take("config")?);
    let (text, base) = configuration(&path)?;
    if command == "proxy-serve" {
        let duration = options
            .optional("duration")
            .map(|s| {
                s.parse::<u64>()
                    .map_err(|_| Error::new(4, "invalid proxy duration"))
            })
            .transpose()?;
        options.finish()?;
        let c = config::proxy_server(&text, &base)?;
        let ip = c.ip.is_some();
        let server = dlm_gateway::proxy::Server::start(c)?;
        println!("{{\"status\":\"READY\",\"address\":\"{}\",\"http_version\":1,\"connect_udp\":true,\"connect_ip\":{}}}",server.address,ip);
        let started = std::time::Instant::now();
        while !dlm_gateway::interrupted()
            && duration.is_none_or(|d| started.elapsed() < Duration::from_secs(d))
        {
            std::thread::sleep(Duration::from_millis(50));
        }
        server.stop();
        let stats = server.statistics();
        drop(server);
        println!(
            "{{\"status\":\"stopped\",\"authenticated\":{},\"incoming\":{},\"outgoing\":{}}}",
            stats.authenticated, stats.incoming, stats.outgoing
        );
        return Ok(());
    }
    let target = options.take("target")?;
    let mode = match options.take("protocol")?.as_str() {
        "tcp" => dlm_gateway::proxy::Mode::Tcp,
        "udp" => dlm_gateway::proxy::Mode::Udp,
        "ip" => dlm_gateway::proxy::Mode::Ip,
        _ => return Err(Error::new(4, "unknown proxy protocol")),
    };
    let input = PathBuf::from(options.take("input")?);
    let output = PathBuf::from(options.take("output")?);
    options.finish()?;
    let data = read_bounded(
        &input,
        if mode == dlm_gateway::proxy::Mode::Udp {
            65527
        } else {
            65535
        },
    )?;
    let cfg = config::proxy_client(&text, &base)?;
    let mut tunnel = dlm_gateway::proxy::Tunnel::connect(&cfg, mode, &target, None)?;
    tunnel.send(&data)?;
    let mut reply = Vec::new();
    if mode == dlm_gateway::proxy::Mode::Tcp {
        while reply.len() < data.len() {
            let block = tunnel.receive((data.len() - reply.len()).min(65535))?;
            ensure_public(
                !block.is_empty(),
                17,
                "proxy endpoint closed before complete reply",
            )?;
            reply.extend(block);
        }
    } else {
        reply = tunnel.receive(65535)?;
    }
    replace(&output, &reply)?;
    println!(
        "{{\"status\":\"endpoint_response\",\"sent\":{},\"received\":{},\"http_version\":1}}",
        data.len(),
        reply.len()
    );
    Ok(())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 2 && args[1] == "--version" {
        println!("dlm-net {}", env!("CARGO_PKG_VERSION"));
        return;
    }
    if args.len() < 2 || matches!(args[1].as_str(), "help" | "--help" | "-h") {
        println!("dlm-net keygen|rotate|identity --identity FILE\ndlm-net journal-keygen|descriptor --output FILE\ndlm-net serve --config FILE [--duration SECONDS]\ndlm-net exchange --config FILE --protocol tcp|udp --target HOST:PORT --input FILE --output FILE\ndlm-net enqueue --outbox DIR --key FILE --peer ROOT_HEX --input FILE [--message-id HEX] [--ttl SECONDS]\ndlm-net flush --config FILE --outbox DIR --key FILE\ndlm-net fetch --config FILE --message-id HEX --output FILE\ndlm-net consume --config FILE --message-id HEX\nServer configuration keys: listen, advertise, transport, identity, certificate, key, locator_output, allow_client (ROOT_HEX[@MIN_EPOCH], repeatable), allow_target (tcp|udp,CIDR,PORT[-PORT], repeatable), allow_private, max_sessions, max_client_sessions, max_handshakes, max_streams, max_pending, handshake_ms, idle_ms, request_ms, session_bytes, bytes_per_second, spool, spool_key.\nClient configuration keys: gateway, hostname, ca, transport, identity, peer, locator, minimum_epoch, minimum_minor, maximum_minor, timeout_ms.\nAdditional commands: pool-status --config FILE; pool-upload --config FILE --input FILE --descriptor FILE [--message-id HEX] [--stream N] [--ttl SECONDS]; pool-download --config FILE --descriptor FILE --output FILE; pool-consume --config FILE --descriptor FILE; pool-queue --config OUTBOX_FILE --input FILE --descriptor FILE [--message-id HEX] [--stream N] [--ttl SECONDS]; pool-flush --config POOL_FILE --outbox-config OUTBOX_FILE; continuity-model --output FILE. Pool path entries: transport,address,source-or-*,interface,ISP,egress,access,cost. Pool keys: path (repeatable), probe_ms, path_max_age_ms, min_hold_ms, hysteresis_percent, circuit_ms, fec_threshold_ppm, fec_budget_per_mille, delivery_window, trust_cache plus client keys. Outbox keys: directory,key,peer. Server supports listen_alternate=transport,address[,advertise]. Transports: tls, quic-stream, quic-datagram, websocket, https-connect. HTTP adapters use HTTP/1.1; HTTP/2, HTTP/3 and TURN are unsupported. Proxy commands: capabilities; proxy-keygen --output FILE; proxy-serve --config FILE [--duration SECONDS]; proxy-exchange --config FILE --protocol tcp|udp|ip --target HOST:PORT|CIDR,PROTOCOL --input FILE --output FILE. Proxy client keys: proxy_address, proxy_hostname, proxy_ca, proxy_token, proxy_timeout_ms, proxy_http_version. Proxy server keys: listen, certificate, key, token, allow_private, allow_target, max_sessions, max_handshakes, handshake_ms, idle_ms, session_bytes, bytes_per_second, request_ms, ip_interface, ip_library, ip_client, ip_route, ip_protocol. Pool proxy_paths selects zero-based path indices. Forwarded bytes and remote durable commit are distinct outcomes.");
        return;
    }
    dlm_gateway::install_signal_handlers();
    if let Err(error) = run(&args[1], &args[2..]) {
        eprintln!("dlm-net: {error}");
        std::process::exit(error.code.clamp(1, 125));
    }
}
