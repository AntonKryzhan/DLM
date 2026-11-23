mod c_api;
pub mod codec;
pub mod config;
pub mod experiment;
pub mod live;
pub mod model;
pub mod statistics;
pub mod trace;

use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
pub type Result<T> = std::result::Result<T, Error>;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub code: i32,
    pub message: &'static str,
}
impl Error {
    pub fn new(code: i32, message: &'static str) -> Self {
        Self { code, message }
    }
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} (status={})", self.message, self.code)
    }
}
impl std::error::Error for Error {}
impl From<std::io::Error> for Error {
    fn from(_: std::io::Error) -> Self {
        Self::new(17, "laboratory file or loopback I/O failed")
    }
}
pub(crate) fn require(ok: bool, code: i32, message: &'static str) -> Result<()> {
    if ok {
        Ok(())
    } else {
        Err(Error::new(code, message))
    }
}
#[derive(Clone, Default)]
pub struct Cancellation(Arc<AtomicBool>);
impl Cancellation {
    pub fn cancel(&self) {
        self.0.store(true, Ordering::Release);
    }
    pub fn check(&self) -> Result<()> {
        require(!self.0.load(Ordering::Acquire), 9, "experiment cancelled")
    }
}
pub const MAX_EVENTS: usize = 1_048_576;
pub const MAX_SYMBOLS: usize = 524_288;
pub const MAX_PAYLOAD: usize = 65_536;
pub const MAX_CAPTURE: usize = 76 + 16 * MAX_EVENTS;
pub(crate) fn crc32c(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for &b in bytes {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0x82f63b78u32.wrapping_mul(crc & 1));
        }
    }
    !crc
}
pub(crate) struct Random {
    state: u64,
    spare: Option<f64>,
}
impl Random {
    pub fn new(seed: u64) -> Self {
        Self {
            state: seed,
            spare: None,
        }
    }
    pub fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }
    pub fn unit(&mut self) -> f64 {
        ((self.next() >> 11) as f64 + 0.5) / 9007199254740992.0
    }
    pub fn chance(&mut self, ppm: u32) -> bool {
        ppm == 1_000_000 || (ppm > 0 && self.next() % 1_000_000 < ppm as u64)
    }
    pub fn normal(&mut self) -> f64 {
        if let Some(v) = self.spare.take() {
            return v;
        }
        let r = (-2.0 * self.unit().ln()).sqrt();
        let t = std::f64::consts::TAU * self.unit();
        self.spare = Some(r * t.sin());
        r * t.cos()
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn crc32c_matches_the_castagnoli_check_vector() {
        assert_eq!(super::crc32c(b"123456789"), 0xe3069283);
        assert_eq!(super::crc32c(b""), 0);
    }
}
