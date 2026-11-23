mod c_api;
pub mod config;
pub mod continuity;
mod crypto;
pub mod delivery;
pub mod fec;
mod ffi;
mod http_carrier;
pub mod gateway;
pub mod identity;
pub mod journal;
pub mod policy;
pub mod proxy;
mod capsule;
mod proxy_ip;
pub mod protocol;
mod service;
pub mod path;
pub mod transport;

use std::fmt;
pub type Result<T> = std::result::Result<T, Error>;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub code: i32,
    pub system: i32,
    pub message: &'static str,
}
impl Error {
    pub fn new(code: i32, message: &'static str) -> Self {
        Self {
            code,
            system: 0,
            message,
        }
    }
    pub fn pending(&self) -> bool {
        self.code == 1
    }
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} (status={}, system={})",
            self.message, self.code, self.system
        )
    }
}
impl std::error::Error for Error {}
impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Self {
            code: if matches!(
                e.kind(),
                std::io::ErrorKind::TimedOut | std::io::ErrorKind::WouldBlock
            ) {
                3
            } else {
                5
            },
            system: e.raw_os_error().unwrap_or(0),
            message: "operating system I/O failed",
        }
    }
}
pub(crate) fn ensure(condition: bool, code: i32, message: &'static str) -> Result<()> {
    if condition {
        Ok(())
    } else {
        Err(Error::new(code, message))
    }
}
pub(crate) fn now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}
pub(crate) fn deadline(ms: u32) -> Result<std::time::Instant> {
    ensure(ms > 0 && ms <= 60000, 4, "invalid timeout")?;
    Ok(std::time::Instant::now() + std::time::Duration::from_millis(ms as u64))
}
pub(crate) fn wait(until: std::time::Instant) -> Result<()> {
    ensure(
        std::time::Instant::now() < until,
        3,
        "operation deadline expired",
    )?;
    std::thread::sleep(std::time::Duration::from_millis(1));
    Ok(())
}
pub fn ensure_public(condition: bool, code: i32, message: &'static str) -> Result<()> {
    ensure(condition, code, message)
}
pub fn message_id() -> Result<[u8; 16]> {
    crypto::random()
}
pub fn wall_seconds() -> u64 {
    now()
}
extern "C" {
    fn dgn_signal_start();
    fn dgn_interrupted() -> i32;
}
pub fn install_signal_handlers() {
    unsafe { dgn_signal_start() }
}
pub fn interrupted() -> bool {
    unsafe { dgn_interrupted() != 0 }
}
