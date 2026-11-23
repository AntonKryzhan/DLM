use crate::{
    codec, config::Config, experiment, trace, Cancellation, Error, Result, MAX_CAPTURE, MAX_PAYLOAD,
};
use std::{
    ffi::c_void,
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
};
const CONTEXT: u64 = 0x444c4e4c41423031;
const TOKEN: u64 = 0x444c4e43414e3031;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Summary {
    pub delivery: u32,
    pub mode: u32,
    pub symbols: u64,
    pub events: u64,
    pub payload_bytes: u64,
    pub decoded_bytes: u64,
    pub corrected_words: u64,
    pub duration_us: u64,
}
#[repr(C)]
pub struct Opened {
    pub handle: *mut c_void,
    pub status: i32,
    pub reserved: i32,
}
#[repr(C)]
pub struct Io {
    pub count: u64,
    pub status: i32,
    pub reserved: i32,
}
#[repr(C)]
pub struct Transfer {
    pub required: u64,
    pub copied: u64,
    pub status: i32,
    pub delivery: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Event {
    pub time_us: u64,
    pub size: u32,
    pub kind: u32,
}
struct Context {
    magic: u64,
    config: Config,
    cancel: Cancellation,
    report: Option<Vec<u8>>,
    capture: Option<Vec<u8>>,
    payload: Option<Vec<u8>>,
    summary: Summary,
}
struct Token {
    magic: u64,
    cancel: Cancellation,
}
fn guarded<T>(f: impl FnOnce() -> Result<T>) -> Result<T> {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(Err(Error::new(99, "laboratory boundary panic")))
}
unsafe fn input<'a>(p: *const u8, n: usize, max: usize) -> Result<&'a [u8]> {
    crate::require(
        n <= max && (!p.is_null() || n == 0),
        4,
        "invalid input pointer or length",
    )?;
    Ok(if n == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(p, n)
    })
}
unsafe fn context<'a>(p: *mut c_void) -> Result<&'a mut Context> {
    crate::require(!p.is_null(), 4, "null laboratory context")?;
    let c = &mut *p.cast::<Context>();
    crate::require(c.magic == CONTEXT, 4, "invalid laboratory context")?;
    Ok(c)
}
unsafe fn context_ref<'a>(p: *mut c_void) -> Result<&'a Context> {
    crate::require(!p.is_null(), 4, "null laboratory context")?;
    let c = &*p.cast::<Context>();
    crate::require(c.magic == CONTEXT, 4, "invalid laboratory context")?;
    Ok(c)
}
fn opened(r: Result<Context>) -> Opened {
    match r {
        Ok(c) => Opened {
            handle: Box::into_raw(Box::new(c)).cast(),
            status: 0,
            reserved: 0,
        },
        Err(e) => Opened {
            handle: ptr::null_mut(),
            status: e.code,
            reserved: 0,
        },
    }
}
fn io(r: Result<u64>) -> Io {
    match r {
        Ok(count) => Io {
            count,
            status: 0,
            reserved: 0,
        },
        Err(e) => Io {
            count: 0,
            status: e.code,
            reserved: 0,
        },
    }
}
impl Context {
    fn new(config: Config) -> Self {
        Self {
            magic: CONTEXT,
            config,
            cancel: Cancellation::default(),
            report: None,
            capture: None,
            payload: None,
            summary: Summary::default(),
        }
    }
    fn clear(&mut self) {
        self.report = None;
        self.capture = None;
        self.payload = None;
        self.summary = Summary::default();
    }
    fn record(
        &mut self,
        capture: codec::Trace,
        decoded: codec::Decoded,
        report: String,
    ) -> Result<()> {
        self.capture = Some(trace::encode(&capture)?);
        self.summary = Summary {
            delivery: match decoded.delivery {
                codec::Delivery::Valid => 1,
                codec::Delivery::Erased => 2,
                codec::Delivery::SyncLost => 3,
                codec::Delivery::IntegrityFailed => 4,
            },
            mode: capture.plan.codec.mode as u32,
            symbols: capture.plan.symbols() as u64,
            events: capture.events.len() as u64,
            payload_bytes: capture.plan.payload_bytes as u64,
            decoded_bytes: decoded.payload.as_ref().map_or(0, Vec::len) as u64,
            corrected_words: decoded.corrected_words,
            duration_us: capture.plan.duration_us(),
        };
        self.report = Some(report.into_bytes());
        self.payload = decoded.payload;
        Ok(())
    }
}
#[no_mangle]
pub extern "C" fn dlm_channel_lab_abi() -> u32 {
    0x10000
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_open(config: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let text = std::str::from_utf8(input(config, count, 65536)?)
            .map_err(|_| Error::new(4, "configuration must be UTF-8"))?;
        Ok(Context::new(Config::parse(text)?))
    }))
}
unsafe fn run(p: *mut c_void, bytes: *const u8, count: usize, live: bool) -> Io {
    io(guarded(|| {
        let c = context(p)?;
        c.clear();
        let payload = input(bytes, count, MAX_PAYLOAD)?;
        let m = if live {
            experiment::loopback(&c.config, payload, &c.cancel)?
        } else {
            experiment::simulate(&c.config, payload, &c.cancel)?
        };
        c.record(m.capture, m.decoded, m.report)?;
        Ok(c.summary.delivery as u64)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_simulate(
    p: *mut c_void,
    bytes: *const u8,
    count: usize,
) -> Io {
    run(p, bytes, count, false)
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_loopback(
    p: *mut c_void,
    bytes: *const u8,
    count: usize,
) -> Io {
    run(p, bytes, count, true)
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_decode(bytes: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let capture = trace::decode(input(bytes, count, MAX_CAPTURE)?)?;
        let config = Config {
            codec: capture.plan.codec.clone(),
            frame_id: capture.plan.frame_id,
            ..Config::default()
        };
        let mut c = Context::new(config);
        let decoded = codec::decode(&capture, &c.cancel)?;
        let report = experiment::decoded_report(&decoded);
        c.record(capture, decoded, report)?;
        Ok(c)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_read(
    p: *mut c_void,
    item: u32,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    let r = guarded(|| {
        crate::require(
            !output.is_null() || capacity == 0,
            4,
            "null laboratory output buffer",
        )?;
        let c = context_ref(p)?;
        let data = match item {
            0 => c.report.as_ref(),
            1 => c.capture.as_ref(),
            2 => c.payload.as_ref(),
            _ => return Err(Error::new(4, "unknown laboratory result item")),
        }
        .ok_or(Error::new(19, "result or decoded payload unavailable"))?;
        let mut t = Transfer {
            required: data.len() as u64,
            copied: 0,
            status: 0,
            delivery: c.summary.delivery,
        };
        if capacity < data.len() {
            t.status = 10;
            return Ok(t);
        }
        if !data.is_empty() {
            ptr::copy_nonoverlapping(data.as_ptr(), output, data.len());
        }
        t.copied = data.len() as u64;
        Ok(t)
    });
    match r {
        Ok(t) => t,
        Err(e) => Transfer {
            required: 0,
            copied: 0,
            status: e.code,
            delivery: 0,
        },
    }
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_summary(p: *mut c_void) -> Summary {
    guarded(|| Ok(context_ref(p)?.summary)).unwrap_or_default()
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_event(
    p: *mut c_void,
    index: usize,
    output: *mut Event,
) -> Io {
    io(guarded(|| {
        crate::require(!output.is_null(), 4, "null laboratory event output")?;
        let c = context_ref(p)?;
        let b = c
            .capture
            .as_ref()
            .ok_or(Error::new(19, "no capture available"))?;
        crate::require(
            index < c.summary.events as usize,
            4,
            "event index outside capture",
        )?;
        let b = &b[76 + 16 * index..76 + 16 * (index + 1)];
        *output = Event {
            time_us: u64::from_be_bytes(b[..8].try_into().unwrap()),
            size: u32::from_be_bytes(b[8..12].try_into().unwrap()),
            kind: b[12] as u32,
        };
        Ok(1)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_close(p: *mut c_void) {
    let _ = guarded(|| {
        context(p)?.magic = 0;
        drop(Box::from_raw(p.cast::<Context>()));
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_cancellation(p: *mut c_void) -> Opened {
    match guarded(|| {
        let cancel = context_ref(p)?.cancel.clone();
        Ok(Box::into_raw(Box::new(Token {
            magic: TOKEN,
            cancel,
        }))
        .cast())
    }) {
        Ok(handle) => Opened {
            handle,
            status: 0,
            reserved: 0,
        },
        Err(e) => Opened {
            handle: ptr::null_mut(),
            status: e.code,
            reserved: 0,
        },
    }
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_cancel(p: *mut c_void) -> Io {
    io(guarded(|| {
        crate::require(!p.is_null(), 4, "null cancellation token")?;
        let token = &*p.cast::<Token>();
        crate::require(token.magic == TOKEN, 4, "invalid cancellation token")?;
        token.cancel.cancel();
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dlm_channel_lab_cancel_close(p: *mut c_void) {
    let _ = guarded(|| {
        crate::require(!p.is_null(), 4, "null cancellation token")?;
        let token = &mut *p.cast::<Token>();
        crate::require(token.magic == TOKEN, 4, "invalid cancellation token")?;
        token.magic = 0;
        drop(Box::from_raw(p.cast::<Token>()));
        Ok(())
    });
}
