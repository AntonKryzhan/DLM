use crate::{ensure, Error, Result};
pub(crate) const MAX: usize = 65536;
pub(crate) fn integer(bytes: &[u8]) -> Option<(u64, usize)> {
    let first = *bytes.first()?;
    let n = 1usize << (first >> 6);
    if bytes.len() < n {
        return None;
    }
    let mut value = (first & 63) as u64;
    for &b in &bytes[1..n] {
        value = (value << 8) | b as u64;
    }
    Some((value, n))
}
pub(crate) fn put(value: u64, out: &mut Vec<u8>) -> Result<()> {
    ensure(value < (1u64 << 62), 4, "capsule integer exceeds 62 bits")?;
    let (n, tag) = if value < 64 {
        (1, 0)
    } else if value < 16384 {
        (2, 64)
    } else if value < (1 << 30) {
        (4, 128)
    } else {
        (8, 192)
    };
    let bytes = value.to_be_bytes();
    let offset = out.len();
    out.extend_from_slice(&bytes[8 - n..]);
    out[offset] |= tag;
    Ok(())
}
pub(crate) fn encode(kind: u64, data: &[u8]) -> Result<Vec<u8>> {
    ensure(data.len() <= MAX, 7, "capsule payload bound")?;
    let mut out = Vec::with_capacity(data.len() + 16);
    put(kind, &mut out)?;
    put(data.len() as u64, &mut out)?;
    out.extend_from_slice(data);
    Ok(out)
}
#[derive(Default)]
pub(crate) struct Decoder {
    skip: u64,
    skipped: u64,
}
impl Decoder {
    pub fn next(&mut self, bytes: &mut Vec<u8>) -> Result<Option<(u64, Vec<u8>)>> {
        let mut cursor = 0;
        loop {
            if self.skip > 0 {
                let n = self.skip.min((bytes.len() - cursor) as u64) as usize;
                cursor += n;
                self.skip -= n as u64;
                self.skipped += n as u64;
                ensure(
                    self.skipped <= 1048576,
                    7,
                    "unknown capsule skip budget exhausted",
                )?;
                if self.skip > 0 {
                    bytes.drain(..cursor);
                    return Ok(None);
                }
            }
            let Some((kind, k)) = integer(&bytes[cursor..]) else {
                bytes.drain(..cursor);
                return Ok(None);
            };
            let Some((size, n)) = integer(&bytes[cursor + k..]) else {
                bytes.drain(..cursor);
                return Ok(None);
            };
            if kind > 3 {
                cursor += k + n;
                self.skip = size;
                continue;
            }
            ensure(
                size <= MAX as u64,
                7,
                "capsule exceeds bounded packet storage",
            )?;
            let size = size as usize;
            if bytes.len() < cursor + k + n + size {
                bytes.drain(..cursor);
                return Ok(None);
            }
            let body = bytes[cursor + k + n..cursor + k + n + size].to_vec();
            bytes.drain(..cursor + k + n + size);
            self.skipped = 0;
            return Ok(Some((kind, body)));
        }
    }
    pub fn finish(&self, bytes: &[u8]) -> Result<()> {
        ensure(
            bytes.is_empty() && self.skip == 0,
            11,
            "truncated capsule at stream close",
        )
    }
}
pub(crate) fn datagram(body: &[u8]) -> Result<Option<&[u8]>> {
    let (context, n) = integer(body).ok_or(Error::new(11, "capsule datagram context absent"))?;
    if context != 0 {
        return Ok(None);
    }
    Ok(Some(&body[n..]))
}
pub(crate) fn data(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut body = Vec::with_capacity(bytes.len() + 1);
    body.push(0);
    body.extend_from_slice(bytes);
    encode(0, &body)
}
pub(crate) fn percent_decode(text: &str) -> Result<String> {
    ensure(
        text.len() <= 768 && text.is_ascii(),
        4,
        "URI component size or encoding",
    )?;
    let mut out = Vec::new();
    let mut b = text.as_bytes();
    while !b.is_empty() {
        if b[0] == b'%' {
            ensure(b.len() >= 3, 11, "truncated URI escape")?;
            let n = u8::from_str_radix(std::str::from_utf8(&b[1..3]).unwrap(), 16)
                .map_err(|_| Error::new(11, "invalid URI escape"))?;
            out.push(n);
            b = &b[3..];
        } else {
            out.push(b[0]);
            b = &b[1..];
        }
    }
    ensure(
        out.iter().all(|&c| (33..=126).contains(&c)),
        11,
        "invalid decoded URI component",
    )?;
    Ok(String::from_utf8(out).unwrap())
}
pub(crate) fn percent_encode(text: &str) -> String {
    let mut out = String::new();
    for b in text.bytes() {
        if b.is_ascii_alphanumeric() || b"-._~*".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn split_varints_large_unknown_capsules_and_truncated_close() {
        for value in [
            0,
            63,
            64,
            16383,
            16384,
            (1 << 30) - 1,
            1 << 30,
            (1u64 << 62) - 1,
        ] {
            let mut b = Vec::new();
            put(value, &mut b).unwrap();
            for n in 0..b.len() {
                assert!(integer(&b[..n]).is_none());
            }
            assert_eq!(integer(&b).unwrap().0, value);
        }
        assert_eq!(integer(&[0x40, 0]).unwrap(), (0, 2));
        let mut d = Decoder::default();
        let mut b = encode(99, &vec![7; 65536]).unwrap();
        b.extend(data(b"\0\xff").unwrap());
        let mut input = Vec::new();
        let mut got = None;
        for byte in b {
            input.push(byte);
            if let Some(value) = d.next(&mut input).unwrap() {
                got = Some(value);
            }
        }
        assert_eq!(datagram(&got.unwrap().1).unwrap().unwrap(), b"\0\xff");
        d.finish(&input).unwrap();
        input.push(0);
        assert!(d.finish(&input).is_err());
        assert!(percent_decode("%0D%0A").is_err());
        assert_eq!(percent_decode("2001%3Adb8%3A%3A1").unwrap(), "2001:db8::1");
    }
}
