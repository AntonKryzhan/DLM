use crate::{
    ensure,
    gateway::{ClientConfig, ServerConfig},
    identity::unhex,
    policy::{Policy, TargetRule},
    transport::Kind,
    Error, Result,
};
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
};
struct Values {
    values: BTreeMap<String, Vec<String>>,
}
impl Values {
    fn parse(text: &str) -> Result<Self> {
        ensure(
            text.len() <= 65536 && !text.contains('\0'),
            4,
            "configuration size or NUL error",
        )?;
        let mut values: BTreeMap<String, Vec<String>> = BTreeMap::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let (key, value) = line
                .split_once('=')
                .ok_or(Error::new(4, "configuration requires key=value lines"))?;
            let key = key.trim();
            let value = value.trim();
            ensure(
                !key.is_empty() && key.len() <= 64 && value.len() <= 4096,
                4,
                "configuration field bound",
            )?;
            let existing = values.entry(key.to_owned()).or_default();
            ensure(
                existing.is_empty()
                    || matches!(
                        key,
                        "allow_client"
                            | "allow_target"
                            | "path"
                            | "listen_alternate"
                            | "ip_client"
                            | "ip_route"
                            | "ip_protocol"
                    ),
                4,
                "duplicate configuration field",
            )?;
            existing.push(value.to_owned());
        }
        Ok(Self { values })
    }
    fn required(&mut self, key: &str) -> Result<String> {
        self.optional(key)
            .filter(|v| !v.is_empty())
            .ok_or(Error::new(4, "required configuration field absent"))
    }
    fn optional(&mut self, key: &str) -> Option<String> {
        self.values.remove(key).and_then(|v| v.into_iter().next())
    }
    fn number<T: std::str::FromStr>(&mut self, key: &str, default: T) -> Result<T> {
        self.optional(key)
            .map(|s| {
                s.parse()
                    .map_err(|_| Error::new(4, "invalid numeric configuration"))
            })
            .unwrap_or(Ok(default))
    }
    fn finish(&self) -> Result<()> {
        ensure(self.values.is_empty(), 4, "unknown configuration field")
    }
}
fn path(base: &Path, value: String) -> PathBuf {
    let p = PathBuf::from(value);
    if p.is_absolute() {
        p
    } else {
        base.join(p)
    }
}
pub fn server(text: &str, base: &Path) -> Result<ServerConfig> {
    let mut v = Values::parse(text)?;
    let bind = v
        .required("listen")?
        .parse()
        .map_err(|_| Error::new(4, "invalid listener socket address"))?;
    let advertise = v
        .optional("advertise")
        .map(|s| {
            s.parse()
                .map_err(|_| Error::new(4, "invalid advertised socket address"))
        })
        .transpose()?;
    let kind = Kind::parse(&v.required("transport")?)?;
    let identity = path(base, v.required("identity")?);
    let certificate = path(base, v.required("certificate")?);
    let key = path(base, v.required("key")?);
    let locator_output = v.optional("locator_output").map(|s| path(base, s));
    let mut policy = Policy::new();
    for client in v.values.remove("allow_client").unwrap_or_default() {
        policy.client(&client)?;
    }
    for target in v.values.remove("allow_target").unwrap_or_default() {
        policy.targets.push(TargetRule::parse(&target)?);
    }
    policy.allow_private = match v.optional("allow_private").as_deref() {
        None | Some("false") => false,
        Some("true") => true,
        _ => return Err(Error::new(4, "allow_private must be true or false")),
    };
    let l = &mut policy.limits;
    l.sessions = v.number("max_sessions", l.sessions)?;
    l.per_client = v.number("max_client_sessions", l.per_client)?;
    l.handshakes = v.number("max_handshakes", l.handshakes)?;
    l.streams = v.number("max_streams", l.streams)?;
    l.pending = v.number("max_pending", l.pending)?;
    l.handshake_ms = v.number("handshake_ms", l.handshake_ms)?;
    l.idle_ms = v.number("idle_ms", l.idle_ms)?;
    l.request_ms = v.number("request_ms", l.request_ms)?;
    l.session_bytes = v.number("session_bytes", l.session_bytes)?;
    l.bytes_per_second = v.number("bytes_per_second", l.bytes_per_second)?;
    let spool = v.optional("spool");
    let spool_key = v.optional("spool_key");
    let spool = match (spool, spool_key) {
        (Some(a), Some(b)) => Some((path(base, a), path(base, b))),
        (None, None) => None,
        _ => {
            return Err(Error::new(
                4,
                "spool and spool_key must be configured together",
            ))
        }
    };
    v.finish()?;
    policy.validate()?;
    Ok(ServerConfig {
        bind,
        advertise,
        kind,
        identity,
        certificate,
        key,
        locator_output,
        policy,
        spool,
    })
}
pub fn client(text: &str, base: &Path) -> Result<ClientConfig> {
    let mut v = Values::parse(text)?;
    let gateway = v
        .required("gateway")?
        .parse()
        .map_err(|_| Error::new(4, "invalid gateway socket address"))?;
    let hostname = v.required("hostname")?;
    let ca = v
        .optional("ca")
        .filter(|s| !s.is_empty())
        .map(|s| path(base, s).to_string_lossy().into_owned())
        .unwrap_or_default();
    let kind = Kind::parse(&v.required("transport")?)?;
    let identity = path(base, v.required("identity")?);
    let peer = unhex(&v.required("peer")?)?;
    let locator = v.optional("locator").map(|s| path(base, s));
    let minimum_epoch = v.number("minimum_epoch", 1)?;
    let minimum_minor = v.number("minimum_minor", 0)?;
    let maximum_minor = v.number("maximum_minor", crate::protocol::MINOR_MAX)?;
    let timeout_ms = v.number("timeout_ms", 5000)?;
    v.finish()?;
    ensure(
        minimum_epoch > 0
            && minimum_minor <= maximum_minor
            && maximum_minor <= crate::protocol::MINOR_MAX
            && (1..=60000).contains(&timeout_ms),
        4,
        "invalid client protocol/key/time bounds",
    )?;
    Ok(ClientConfig {
        gateway,
        hostname,
        ca,
        kind,
        identity,
        peer,
        minimum_epoch,
        minimum_minor,
        maximum_minor,
        timeout_ms,
        locator,
    })
}
fn remaining(v: &Values) -> String {
    let mut text = String::new();
    for (key, items) in &v.values {
        for item in items {
            text.push_str(key);
            text.push('=');
            text.push_str(item);
            text.push('\n');
        }
    }
    text
}
pub fn server_group(text: &str, base: &Path) -> Result<Vec<ServerConfig>> {
    let mut v = Values::parse(text)?;
    let alternatives = v.values.remove("listen_alternate").unwrap_or_default();
    ensure(alternatives.len() < 6, 7, "gateway listener group capacity")?;
    let primary = server(&remaining(&v), base)?;
    let mut configs = vec![primary.clone()];
    for item in alternatives {
        let f: Vec<_> = item.split(',').map(str::trim).collect();
        ensure(
            (2..=3).contains(&f.len()),
            4,
            "alternate listener requires transport,address[,advertise]",
        )?;
        let mut config = primary.clone();
        config.kind = Kind::parse(f[0])?;
        config.bind = f[1]
            .parse()
            .map_err(|_| Error::new(4, "invalid alternate listener address"))?;
        config.advertise = if f.len() == 3 {
            Some(
                f[2].parse()
                    .map_err(|_| Error::new(4, "invalid alternate advertised address"))?,
            )
        } else {
            None
        };
        config.locator_output = None;
        configs.push(config);
    }
    Ok(configs)
}
pub fn pool(text: &str, base: &Path) -> Result<crate::path::Pool> {
    use crate::path::{Candidate, Domains, Pool, PoolPolicy};
    let mut v = Values::parse(text)?;
    let paths = v.values.remove("path").unwrap_or_default();
    let proxy_paths = v.optional("proxy_paths");
    let proxy = take_proxy(&mut v, base)?;
    ensure(
        !paths.is_empty() && paths.len() <= 6,
        4,
        "pool configuration requires one to six path entries",
    )?;
    let trust = v.optional("trust_cache").map(|value| path(base, value));
    let mut p = PoolPolicy::default();
    p.probe_ms = v.number("probe_ms", p.probe_ms)?;
    p.max_age_ms = v.number("path_max_age_ms", p.max_age_ms)?;
    p.hold_ms = v.number("min_hold_ms", p.hold_ms)?;
    p.hysteresis = v.number("hysteresis_percent", p.hysteresis)?;
    p.circuit_ms = v.number("circuit_ms", p.circuit_ms)?;
    p.fec_threshold = v.number("fec_threshold_ppm", p.fec_threshold)?;
    p.fec_budget = v.number("fec_budget_per_mille", p.fec_budget)?;
    p.window = v.number("delivery_window", p.window)?;
    let mut parsed = Vec::new();
    for path in paths {
        let f: Vec<_> = path.split(',').map(str::trim).collect();
        ensure(
            f.len() == 8,
            4,
            "path requires transport,address,source-or-*,interface,ISP,egress,access,cost",
        )?;
        let kind = Kind::parse(f[0])?;
        let gateway: std::net::SocketAddr = f[1]
            .parse()
            .map_err(|_| Error::new(4, "invalid path gateway address"))?;
        let source: Option<std::net::SocketAddr> = if f[2] == "*" {
            None
        } else {
            Some(
                f[2].parse()
                    .map_err(|_| Error::new(4, "invalid concrete source socket address"))?,
            )
        };
        let domains = Domains {
            interface: f[3].into(),
            isp: f[4].into(),
            egress: f[5].into(),
            access: f[6].into(),
        };
        let cost: u32 = f[7]
            .parse()
            .map_err(|_| Error::new(4, "invalid path cost"))?;
        parsed.push((kind, gateway, source, domains, cost));
    }
    v.values
        .entry("gateway".into())
        .or_insert_with(|| vec![parsed[0].1.to_string()]);
    v.values
        .entry("transport".into())
        .or_insert_with(|| vec![parsed[0].0.name().into()]);
    let config = client(&remaining(&v), base)?;
    let mut proxies = BTreeMap::new();
    if let Some(proxy) = proxy {
        let indices =
            proxy_paths.ok_or(Error::new(4, "pool proxy requires proxy_paths indices"))?;
        for n in indices.split(',') {
            let index = n
                .trim()
                .parse::<usize>()
                .map_err(|_| Error::new(4, "invalid proxy path index"))?;
            ensure(
                proxies.insert(index, proxy.clone()).is_none(),
                4,
                "duplicate proxy path index",
            )?;
        }
    } else {
        ensure(
            proxy_paths.is_none(),
            4,
            "proxy_paths requires proxy settings",
        )?;
    }
    let pool = Pool::new_with_proxy_map(
        parsed
            .into_iter()
            .map(|(kind, gateway, source, domains, cost)| {
                let mut config = config.clone();
                config.kind = kind;
                config.gateway = gateway;
                Candidate {
                    config,
                    source,
                    domains,
                    cost,
                }
            })
            .collect(),
        p,
        trust,
        proxies,
    )?;
    Ok(pool)
}
fn take_proxy(v: &mut Values, base: &Path) -> Result<Option<crate::proxy::ClientConfig>> {
    let Some(address) = v.optional("proxy_address") else {
        return Ok(None);
    };
    let address = address
        .parse()
        .map_err(|_| Error::new(4, "invalid proxy socket address"))?;
    let hostname = v.required("proxy_hostname")?;
    let ca = path(base, v.required("proxy_ca")?)
        .to_string_lossy()
        .into_owned();
    let token = path(base, v.required("proxy_token")?);
    let timeout_ms = v.number("proxy_timeout_ms", 5000)?;
    let http_version = v.number("proxy_http_version", 1)?;
    ensure(
        (1..=60000).contains(&timeout_ms),
        4,
        "proxy timeout out of bounds",
    )?;
    ensure(
        http_version == 1,
        6,
        "HTTP/2 and HTTP/3 MASQUE are not supported",
    )?;
    Ok(Some(crate::proxy::ClientConfig {
        address,
        hostname,
        ca,
        token,
        timeout_ms,
        http_version,
    }))
}
pub fn client_with_proxy(
    text: &str,
    base: &Path,
) -> Result<(ClientConfig, Option<crate::proxy::ClientConfig>)> {
    let mut v = Values::parse(text)?;
    let proxy = take_proxy(&mut v, base)?;
    Ok((client(&remaining(&v), base)?, proxy))
}
pub fn proxy_client(text: &str, base: &Path) -> Result<crate::proxy::ClientConfig> {
    let mut v = Values::parse(text)?;
    let proxy = take_proxy(&mut v, base)?.ok_or(Error::new(4, "proxy_address required"))?;
    v.finish()?;
    Ok(proxy)
}
pub fn proxy_server(text: &str, base: &Path) -> Result<crate::proxy::ServerConfig> {
    let mut v = Values::parse(text)?;
    let bind = v
        .required("listen")?
        .parse()
        .map_err(|_| Error::new(4, "invalid proxy listener"))?;
    let certificate = path(base, v.required("certificate")?)
        .to_string_lossy()
        .into_owned();
    let key = path(base, v.required("key")?)
        .to_string_lossy()
        .into_owned();
    let token = path(base, v.required("token")?);
    let mut policy = Policy::new();
    for target in v.values.remove("allow_target").unwrap_or_default() {
        policy.targets.push(TargetRule::parse(&target)?);
    }
    policy.allow_private = match v.optional("allow_private").as_deref() {
        None | Some("false") => false,
        Some("true") => true,
        _ => return Err(Error::new(4, "allow_private must be true or false")),
    };
    let l = &mut policy.limits;
    l.sessions = v.number("max_sessions", 16)?;
    l.per_client = l.per_client.min(l.sessions);
    l.handshakes = v.number("max_handshakes", l.handshakes.min(l.sessions))?;
    l.handshake_ms = v.number("handshake_ms", l.handshake_ms)?;
    l.idle_ms = v.number("idle_ms", 30000)?;
    l.request_ms = v.number("request_ms", l.request_ms)?;
    l.session_bytes = v.number("session_bytes", l.session_bytes)?;
    l.bytes_per_second = v.number("bytes_per_second", l.bytes_per_second)?;
    l.validate()?;
    let interface = v.optional("ip_interface");
    let library = v
        .optional("ip_library")
        .map(|p| path(base, p).to_string_lossy().into_owned())
        .unwrap_or_default();
    let clients = v
        .values
        .remove("ip_client")
        .unwrap_or_default()
        .iter()
        .map(|v| crate::proxy::Network::parse(v))
        .collect::<Result<Vec<_>>>()?;
    let routes = v
        .values
        .remove("ip_route")
        .unwrap_or_default()
        .iter()
        .map(|v| crate::proxy::Network::parse(v))
        .collect::<Result<Vec<_>>>()?;
    let protocols = v
        .values
        .remove("ip_protocol")
        .unwrap_or_default()
        .iter()
        .map(|v| {
            v.parse::<u8>()
                .map_err(|_| Error::new(4, "IP protocol must be 0..255"))
        })
        .collect::<Result<std::collections::BTreeSet<_>>>()?;
    let ip = if let Some(interface) = interface {
        let ip = crate::proxy::IpConfig {
            interface,
            library,
            clients,
            routes,
            protocols,
        };
        ip.validate()?;
        Some(ip)
    } else {
        ensure(
            library.is_empty() && clients.is_empty() && routes.is_empty() && protocols.is_empty(),
            4,
            "IP policy requires ip_interface",
        )?;
        None
    };
    ensure(
        !policy.targets.is_empty() || ip.is_some(),
        4,
        "proxy requires explicit target or IP policy",
    )?;
    v.finish()?;
    Ok(crate::proxy::ServerConfig {
        bind,
        certificate,
        key,
        token,
        policy,
        ip,
    })
}
pub fn outbox(text: &str, base: &Path) -> Result<(crate::journal::Journal, [u8; 32])> {
    let mut v = Values::parse(text)?;
    let directory = path(base, v.required("directory")?);
    let key = path(base, v.required("key")?);
    let peer = unhex(&v.required("peer")?)?;
    v.finish()?;
    Ok((
        crate::journal::Journal::open(&directory, crate::journal::load_key(&key)?, 67108864, 4096)?,
        peer,
    ))
}
