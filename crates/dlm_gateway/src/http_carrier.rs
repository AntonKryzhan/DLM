use crate::{
    crypto, ensure, ffi,
    transport::{Kind, Tls, MAX_FRAME},
    wait, Error, Result,
};
use std::{
    collections::BTreeMap,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    time::Instant,
};

pub(crate) const HEAD_MAX: usize = 8192;
pub(crate) const WIRE_MAX: usize = MAX_FRAME + 16;
pub(crate) struct Head {
    pub start: String,
    pub fields: BTreeMap<String, String>,
}
impl Head {
    pub fn get(&self, name: &str) -> &str {
        self.fields.get(name).map(String::as_str).unwrap_or("")
    }
    pub fn token(&self, name: &str, value: &str) -> bool {
        self.get(name)
            .split(',')
            .any(|s| s.trim().eq_ignore_ascii_case(value))
    }
    pub fn no_content(&self) -> Result<()> {
        ensure(
            !self.fields.contains_key("content-length")
                && !self.fields.contains_key("transfer-encoding")
                && !self.fields.contains_key("content-type"),
            11,
            "tunnel must not carry HTTP content headers",
        )
    }
}
fn token(c: u8) -> bool {
    c.is_ascii_alphanumeric() || b"!#$%&'*+-.^_`|~".contains(&c)
}
pub(crate) fn parse_head(bytes: &[u8]) -> Result<Head> {
    ensure(
        bytes.len() <= HEAD_MAX && bytes.ends_with(b"\r\n\r\n") && bytes.is_ascii(),
        11,
        "invalid HTTP header section",
    )?;
    let text = std::str::from_utf8(bytes).map_err(|_| Error::new(11, "HTTP header encoding"))?;
    let mut lines = text[..text.len() - 4].split("\r\n");
    let start = lines
        .next()
        .ok_or(Error::new(11, "HTTP start line absent"))?;
    ensure(
        !start.is_empty() && start.len() <= 2048 && start.bytes().all(|c| (32..=126).contains(&c)),
        11,
        "invalid HTTP start line",
    )?;
    let mut fields = BTreeMap::new();
    for (index, line) in lines.enumerate() {
        ensure(
            index < 64 && !line.is_empty() && !line.starts_with([' ', '\t']),
            11,
            "HTTP field count or folded field",
        )?;
        let (name, value) = line
            .split_once(':')
            .ok_or(Error::new(11, "HTTP field separator absent"))?;
        ensure(
            !name.is_empty()
                && name.len() <= 64
                && name.bytes().all(token)
                && value.bytes().all(|c| c == 9 || (32..=126).contains(&c)),
            11,
            "invalid HTTP field",
        )?;
        let name = name.to_ascii_lowercase();
        let value = value.trim().to_owned();
        if let Some(old) = fields.get_mut(&name) {
            ensure(name == "connection", 11, "duplicate HTTP field")?;
            let old: &mut String = old;
            old.push(',');
            old.push_str(&value);
        } else {
            fields.insert(name, value);
        }
    }
    Ok(Head {
        start: start.to_owned(),
        fields,
    })
}
pub(crate) fn pause(stop: &AtomicBool, end: Instant) -> Result<()> {
    ensure(!stop.load(Ordering::Acquire), 9, "HTTP carrier cancelled")?;
    wait(end)
}
impl Tls {
    pub(crate) fn read_more(&mut self, maximum: usize) -> Result<bool> {
        ensure(self.input.len() < maximum, 7, "HTTP carrier receive budget")?;
        let mut bytes = [0; 8192];
        let n = bytes.len().min(maximum - self.input.len());
        let r = unsafe { ffi::dgn_tls_read(self.raw.as_ptr(), bytes.as_mut_ptr(), n) };
        if r.status == 1 {
            return Ok(false);
        }
        let count = r.checked()?;
        ensure(count > 0 && count <= n, 2, "HTTP carrier closed")?;
        self.input.extend_from_slice(&bytes[..count]);
        Ok(true)
    }
    pub(crate) fn write_all(
        &mut self,
        bytes: &[u8],
        end: Instant,
        stop: &AtomicBool,
    ) -> Result<()> {
        let mut offset = 0;
        while offset < bytes.len() {
            ensure(!stop.load(Ordering::Acquire), 9, "HTTP carrier cancelled")?;
            let r = unsafe {
                ffi::dgn_tls_write(
                    self.raw.as_ptr(),
                    bytes[offset..].as_ptr(),
                    bytes.len() - offset,
                )
            };
            if r.status == 1 {
                pause(stop, end)?;
                continue;
            }
            let count = r.checked()?;
            ensure(
                count > 0 && count <= bytes.len() - offset,
                11,
                "HTTP write count invalid",
            )?;
            offset += count;
            if offset < bytes.len() {
                pause(stop, end)?;
            }
        }
        Ok(())
    }
    pub(crate) fn read_head(&mut self, end: Instant, stop: &AtomicBool) -> Result<Head> {
        loop {
            if let Some(p) = self.input.windows(4).position(|w| w == b"\r\n\r\n") {
                let head = parse_head(&self.input[..p + 4])?;
                self.input.drain(..p + 4);
                return Ok(head);
            }
            if !self.read_more(HEAD_MAX)? {
                pause(stop, end)?;
            }
        }
    }
}
pub(crate) fn base64(bytes: &[u8]) -> String {
    const A: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for b in bytes.chunks(3) {
        out.push(A[(b[0] >> 2) as usize] as char);
        out.push(A[((b[0] & 3) << 4 | b.get(1).copied().unwrap_or(0) >> 4) as usize] as char);
        out.push(if b.len() > 1 {
            A[((b[1] & 15) << 2 | b.get(2).copied().unwrap_or(0) >> 6) as usize] as char
        } else {
            '='
        });
        out.push(if b.len() > 2 {
            A[(b[2] & 63) as usize] as char
        } else {
            '='
        });
    }
    out
}
pub(crate) fn websocket_accept(key: &str) -> Result<String> {
    ensure(
        key.len() == 24
            && key.ends_with("==")
            && key[..22]
                .bytes()
                .all(|c| c.is_ascii_alphanumeric() || c == b'+' || c == b'/'),
        11,
        "WebSocket key must encode sixteen bytes",
    )?;
    let last = key.as_bytes()[21];
    ensure(b"AQgw".contains(&last), 11, "noncanonical WebSocket key")?;
    let bytes = format!("{key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    let mut digest = [0; 20];
    unsafe { ffi::dgn_websocket_digest(bytes.as_ptr(), bytes.len(), digest.as_mut_ptr()) }
        .checked()?;
    Ok(base64(&digest))
}
pub(crate) fn authority(value: &str) -> Result<()> {
    ensure(
        value.len() <= 255
            && value.is_ascii()
            && !value
                .bytes()
                .any(|c| c <= 32 || c == 127 || b"/@?#\\".contains(&c)),
        11,
        "invalid HTTP tunnel authority",
    )?;
    let (host, port) = value
        .rsplit_once(':')
        .ok_or(Error::new(11, "tunnel authority requires port"))?;
    let port = port
        .parse::<u16>()
        .map_err(|_| Error::new(11, "invalid tunnel port"))?;
    ensure(
        !host.is_empty() && port > 0 && (host.starts_with('[') == host.ends_with(']')),
        11,
        "invalid tunnel authority",
    )
}
pub(crate) struct Http {
    pub tls: Tls,
    websocket: bool,
    client: bool,
    ready: bool,
    closed: bool,
    fragmented: bool,
    fragments: Vec<u8>,
    advertised: Option<std::net::SocketAddr>,
    stop: Arc<AtomicBool>,
}
impl Http {
    pub fn pending(tls: Tls, kind: Kind, stop: Arc<AtomicBool>) -> Self {
        Self {
            tls,
            websocket: kind == Kind::WebSocket,
            client: false,
            ready: false,
            closed: false,
            fragmented: false,
            fragments: Vec::new(),
            advertised: None,
            stop,
        }
    }
    pub fn connect(
        tls: Tls,
        kind: Kind,
        host: &str,
        port: u16,
        end: Instant,
        stop: Arc<AtomicBool>,
    ) -> Result<Self> {
        let mut value = Self::pending(tls, kind, stop);
        value.client = true;
        let authority = if host.contains(':') {
            format!("[{host}]:{port}")
        } else {
            format!("{host}:{port}")
        };
        self::authority(&authority)?;
        let key = base64(&crypto::random::<16>()?);
        let request = if value.websocket {
            format!("GET /dln/v1 HTTP/1.1\r\nHost: {authority}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Protocol: dln.gateway.v1\r\n\r\n")
        } else {
            format!("CONNECT {authority} HTTP/1.1\r\nHost: {authority}\r\n\r\n")
        };
        value.tls.write_all(request.as_bytes(), end, &value.stop)?;
        let head = value.tls.read_head(end, &value.stop)?;
        head.no_content()?;
        if value.websocket {
            ensure(
                head.start.starts_with("HTTP/1.1 ")
                    && head.start.split(' ').nth(1) == Some("101")
                    && head.token("connection", "upgrade")
                    && head.get("upgrade").eq_ignore_ascii_case("websocket")
                    && head.get("sec-websocket-accept") == websocket_accept(&key)?
                    && head.get("sec-websocket-protocol") == "dln.gateway.v1"
                    && head.get("sec-websocket-extensions").is_empty(),
                11,
                "WebSocket upgrade refused or changed",
            )?;
        } else {
            let status = head.start.split(' ').nth(1).unwrap_or("");
            ensure(
                head.start.starts_with("HTTP/1.1 ")
                    && status.len() == 3
                    && status.starts_with('2')
                    && status.bytes().all(|c| c.is_ascii_digit()),
                6,
                "HTTPS CONNECT refused",
            )?;
        }
        value.ready = true;
        Ok(value)
    }
    pub fn handshake(&mut self, end: Instant) -> Result<()> {
        if self.ready {
            return Ok(());
        }
        loop {
            let r = unsafe { ffi::dgn_tls_handshake(self.tls.raw.as_ptr()) };
            if r.status == 0 {
                break;
            }
            if r.status != 1 {
                r.checked()?;
            }
            pause(&self.stop, end)?;
        }
        let result = self.accept_request(end);
        if let Err(e) = &result {
            let status = if e.code == 6 {
                "501 Not Implemented"
            } else {
                "400 Bad Request"
            };
            let response =
                format!("HTTP/1.1 {status}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            let _ = self.tls.write_all(response.as_bytes(), end, &self.stop);
            self.closed = true;
        }
        result
    }
    fn accept_request(&mut self, end: Instant) -> Result<()> {
        let head = self.tls.read_head(end, &self.stop)?;
        head.no_content()?;
        let parts: Vec<_> = head.start.split(' ').collect();
        ensure(
            parts.len() == 3 && parts[2] == "HTTP/1.1",
            6,
            "only HTTP/1.1 tunnel negotiation is supported",
        )?;
        self::authority(head.get("host"))?;
        let response = if self.websocket {
            ensure(
                parts[0] == "GET"
                    && parts[1] == "/dln/v1"
                    && head.token("connection", "upgrade")
                    && head.get("upgrade").eq_ignore_ascii_case("websocket")
                    && head.get("sec-websocket-version") == "13"
                    && head.token("sec-websocket-protocol", "dln.gateway.v1"),
                11,
                "WebSocket request unsupported",
            )?;
            let accept = websocket_accept(head.get("sec-websocket-key"))?;
            format!("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: {accept}\r\nSec-WebSocket-Protocol: dln.gateway.v1\r\n\r\n")
        } else {
            ensure(
                parts[0] == "CONNECT"
                    && parts[1] == head.get("host")
                    && head.get("upgrade").is_empty(),
                11,
                "CONNECT target must identify this gateway",
            )?;
            let local = unsafe { ffi::dgn_tls_local(self.tls.raw.as_ptr()) }.checked()?;
            let expected = self.advertised.unwrap_or(local);
            let (h, p) = parts[1].rsplit_once(':').unwrap();
            let h = h.trim_start_matches('[').trim_end_matches(']');
            let p = p
                .parse::<u16>()
                .map_err(|_| Error::new(11, "invalid CONNECT port"))?;
            let mut name = [0; 256];
            let sni = unsafe {
                ffi::dgn_tls_hostname(self.tls.raw.as_ptr(), name.as_mut_ptr(), name.len())
            };
            let authorised = if sni.status == 0 {
                ensure(sni.count <= name.len(), 11, "TLS server name size invalid")?;
                h.as_bytes().eq_ignore_ascii_case(&name[..sni.count])
            } else {
                h.parse::<std::net::IpAddr>()
                    .is_ok_and(|ip| ip == expected.ip() || ip == local.ip())
            };
            ensure(
                authorised && (p == expected.port() || p == local.port()),
                16,
                "CONNECT destination differs from the gateway TLS origin",
            )?;
            "HTTP/1.1 200 Connection Established\r\n\r\n".to_owned()
        };
        self.tls.write_all(response.as_bytes(), end, &self.stop)?;
        self.ready = true;
        Ok(())
    }
    fn send_frame(&mut self, opcode: u8, bytes: &[u8], end: Instant) -> Result<()> {
        ensure(
            !self.closed && bytes.len() <= WIRE_MAX,
            7,
            "WebSocket closed or message exceeds bound",
        )?;
        let mut wire = Vec::with_capacity(bytes.len() + 14);
        wire.push(0x80 | opcode);
        let mask = if self.client { 0x80 } else { 0 };
        if bytes.len() < 126 {
            wire.push(mask | bytes.len() as u8);
        } else if bytes.len() <= 65535 {
            wire.push(mask | 126);
            wire.extend_from_slice(&(bytes.len() as u16).to_be_bytes());
        } else {
            wire.push(mask | 127);
            wire.extend_from_slice(&(bytes.len() as u64).to_be_bytes());
        }
        if self.client {
            let key = crypto::random::<4>()?;
            wire.extend_from_slice(&key);
            wire.extend(bytes.iter().enumerate().map(|(i, b)| b ^ key[i & 3]));
        } else {
            wire.extend_from_slice(bytes);
        }
        if let Err(e) = self.tls.write_all(&wire, end, &self.stop) {
            self.closed = true;
            return Err(e);
        }
        Ok(())
    }
    pub fn send(&mut self, wire: &[u8], end: Instant) -> Result<()> {
        if self.websocket {
            self.send_frame(2, wire, end)
        } else {
            ensure(!self.closed, 2, "HTTP tunnel closed")?;
            let r = self.tls.write_all(wire, end, &self.stop);
            if r.is_err() {
                self.closed = true;
            }
            r
        }
    }
    pub fn receive(&mut self, end: Instant) -> Result<Vec<u8>> {
        let mut controls = 0;
        loop {
            ensure(!self.closed, 2, "HTTP tunnel closed")?;
            if !self.websocket {
                if self.tls.input.len() >= 16 {
                    let n = super::transport::frame_size(&self.tls.input, MAX_FRAME)?;
                    if self.tls.input.len() >= 16 + n {
                        return Ok(self.tls.input.drain(..16 + n).collect());
                    }
                }
            } else if let Some((opcode, fin, data)) = self.frame()? {
                match opcode {
                    2 => {
                        ensure(!self.fragmented, 11, "interleaved WebSocket messages")?;
                        if fin {
                            return Ok(data);
                        }
                        self.fragmented = true;
                        self.fragments = data;
                    }
                    0 => {
                        ensure(
                            self.fragmented && self.fragments.len() + data.len() <= WIRE_MAX,
                            11,
                            "invalid or excessive WebSocket continuation",
                        )?;
                        self.fragments.extend_from_slice(&data);
                        if fin {
                            self.fragmented = false;
                            return Ok(std::mem::take(&mut self.fragments));
                        }
                    }
                    8 => {
                        ensure(data.len() != 1, 11, "invalid WebSocket close payload")?;
                        if data.len() >= 2 {
                            let code = u16::from_be_bytes(data[..2].try_into().unwrap());
                            ensure(
                                (1000..=1014).contains(&code) && !matches!(code, 1004..=1006)
                                    || (3000..=4999).contains(&code),
                                11,
                                "invalid WebSocket close code",
                            )?;
                            ensure(
                                std::str::from_utf8(&data[2..]).is_ok(),
                                11,
                                "invalid WebSocket close reason",
                            )?;
                        }
                        self.send_frame(8, &data, end)?;
                        self.closed = true;
                        return Err(Error::new(2, "WebSocket peer closed"));
                    }
                    9 => self.send_frame(10, &data, end)?,
                    10 => {}
                    _ => return Err(Error::new(11, "binary WebSocket messages required")),
                }
                controls += 1;
                ensure(
                    controls <= 64,
                    7,
                    "WebSocket control/fragment processing budget",
                )?;
                continue;
            }
            if !self.tls.read_more(2 * WIRE_MAX + 28)? {
                pause(&self.stop, end)?;
            }
        }
    }
    fn frame(&mut self) -> Result<Option<(u8, bool, Vec<u8>)>> {
        let b = &self.tls.input;
        if b.len() < 2 {
            return Ok(None);
        }
        let fin = b[0] & 128 != 0;
        let op = b[0] & 15;
        let masked = b[1] & 128 != 0;
        ensure(
            b[0] & 0x70 == 0 && masked != self.client && matches!(op, 0 | 2 | 8 | 9 | 10),
            11,
            "WebSocket mask, RSV, or opcode invalid",
        )?;
        let short = b[1] & 127;
        let mut at = 2;
        let n = match short {
            126 => {
                if b.len() < 4 {
                    return Ok(None);
                }
                at = 4;
                let n = u16::from_be_bytes(b[2..4].try_into().unwrap()) as u64;
                ensure(n >= 126, 11, "nonminimal WebSocket length")?;
                n
            }
            127 => {
                if b.len() < 10 {
                    return Ok(None);
                }
                at = 10;
                let n = u64::from_be_bytes(b[2..10].try_into().unwrap());
                ensure(
                    n >= 65536 && n < (1u64 << 63),
                    11,
                    "invalid WebSocket 64-bit length",
                )?;
                n
            }
            n => n as u64,
        };
        ensure(
            n <= WIRE_MAX as u64 && (op < 8 || (fin && n <= 125)),
            7,
            "WebSocket frame length/control bound",
        )?;
        let mask_at = at;
        if masked {
            at += 4;
        }
        let n = n as usize;
        if b.len() < at + n {
            return Ok(None);
        }
        let mut data = b[at..at + n].to_vec();
        if masked {
            for (i, v) in data.iter_mut().enumerate() {
                *v ^= b[mask_at + (i & 3)];
            }
        }
        self.tls.input.drain(..at + n);
        Ok(Some((op, fin, data)))
    }
    pub fn cancel(&mut self, stop: Arc<AtomicBool>) {
        self.stop = stop;
    }
    pub fn advertise(&mut self, address: std::net::SocketAddr) {
        self.advertised = Some(address);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rfc_websocket_accept_and_http_smuggling_boundaries() {
        assert_eq!(
            websocket_accept("dGhlIHNhbXBsZSBub25jZQ==").unwrap(),
            "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
        );
        for bytes in [
            b"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n".as_slice(),
            b"GET / HTTP/1.1\r\n Host: a\r\n\r\n",
            b"GET / HTTP/1.1\r\nX : b\r\n\r\n",
            b"GET / HTTP/1.1\r\nX: a\nb\r\n\r\n",
        ] {
            assert!(parse_head(bytes).is_err());
        }
        let h = parse_head(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n").unwrap();
        assert!(h.no_content().is_err());
    }
}
