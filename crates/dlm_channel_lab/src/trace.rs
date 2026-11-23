use crate::{
    codec::{Codec, Event, Fec, Mode, Plan, Trace},
    crc32c, require, Error, Result, MAX_CAPTURE, MAX_EVENTS,
};
pub fn encode(trace: &Trace) -> Result<Vec<u8>> {
    trace.validate()?;
    let c = &trace.plan.codec;
    let mut b = Vec::with_capacity(76 + 16 * trace.events.len());
    b.extend(b"DLCT0001");
    for n in [
        c.mode as u32,
        c.fec as u32,
        c.interleave as u32,
        c.small_size,
        c.large_size,
    ] {
        b.extend(n.to_be_bytes());
    }
    for n in [
        c.slot_us,
        c.tolerance_us,
        c.sync_search_us,
        trace.plan.frame_id,
    ] {
        b.extend(n.to_be_bytes());
    }
    b.extend((trace.plan.payload_bytes as u32).to_be_bytes());
    b.extend((trace.events.len() as u32).to_be_bytes());
    b.extend(0u32.to_be_bytes());
    b.extend(crc32c(&b).to_be_bytes());
    debug_assert_eq!(b.len(), 76);
    for e in &trace.events {
        b.extend(e.time_us.to_be_bytes());
        b.extend(e.size.to_be_bytes());
        b.push(e.kind);
        b.extend([0; 3]);
    }
    Ok(b)
}
pub fn decode(bytes: &[u8]) -> Result<Trace> {
    require(
        bytes.len() >= 76 && bytes.len() <= MAX_CAPTURE,
        11,
        "capture length outside bounds",
    )?;
    require(&bytes[..8] == b"DLCT0001", 11, "capture format unsupported")?;
    let u32at = |i| u32::from_be_bytes(bytes[i..i + 4].try_into().unwrap());
    let u64at = |i| u64::from_be_bytes(bytes[i..i + 8].try_into().unwrap());
    require(
        u32at(68) == 0 && crc32c(&bytes[..72]) == u32at(72),
        12,
        "capture metadata checksum or flags invalid",
    )?;
    let count = u32at(64) as usize;
    require(
        count <= MAX_EVENTS && bytes.len() == 76 + 16 * count,
        11,
        "capture count differs from exact encoded length",
    )?;
    let codec = Codec {
        mode: Mode::from_id(u32at(8))?,
        fec: Fec::from_id(u32at(12))?,
        interleave: u32at(16) as usize,
        small_size: u32at(20),
        large_size: u32at(24),
        slot_us: u64at(28),
        tolerance_us: u64at(36),
        sync_search_us: u64at(44),
    };
    let plan = Plan {
        codec,
        frame_id: u64at(52),
        payload_bytes: u32at(60) as usize,
    };
    plan.validate()?;
    let mut events = Vec::with_capacity(count);
    for b in bytes[76..].chunks_exact(16) {
        require(
            b[13..] == [0; 3],
            11,
            "capture event reserved fields nonzero",
        )?;
        events.push(Event {
            time_us: u64::from_be_bytes(b[..8].try_into().unwrap()),
            size: u32::from_be_bytes(b[8..12].try_into().unwrap()),
            kind: b[12],
        });
    }
    let trace = Trace { plan, events };
    trace.validate()?;
    Ok(trace)
}
pub fn read(path: &std::path::Path) -> Result<Trace> {
    let mut f = std::fs::File::open(path)?;
    use std::io::Read;
    require(
        f.metadata()?.len() <= MAX_CAPTURE as u64,
        7,
        "capture file quota exceeded",
    )?;
    let mut b = Vec::new();
    f.by_ref()
        .take(MAX_CAPTURE as u64 + 1)
        .read_to_end(&mut b)?;
    if b.len() > MAX_CAPTURE {
        return Err(Error::new(7, "capture file grew beyond quota"));
    }
    decode(&b)
}
