use crate::{crc32c, require, Cancellation, Error, Result, MAX_EVENTS, MAX_PAYLOAD, MAX_SYMBOLS};
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Absolute = 1,
    Interval = 2,
    Presence = 3,
    Count = 4,
    Protocol = 5,
    Order = 6,
    Size = 7,
    Hybrid = 8,
}
impl Mode {
    pub const ALL: [Self; 8] = [
        Self::Absolute,
        Self::Interval,
        Self::Presence,
        Self::Count,
        Self::Protocol,
        Self::Order,
        Self::Size,
        Self::Hybrid,
    ];
    pub fn parse(s: &str) -> Result<Self> {
        Self::ALL
            .into_iter()
            .find(|m| m.name() == s)
            .ok_or(Error::new(4, "unknown event alphabet"))
    }
    pub fn from_id(n: u32) -> Result<Self> {
        Self::ALL
            .into_iter()
            .find(|m| *m as u32 == n)
            .ok_or(Error::new(4, "unknown event alphabet ID"))
    }
    pub fn name(self) -> &'static str {
        match self {
            Self::Absolute => "absolute",
            Self::Interval => "interval",
            Self::Presence => "presence",
            Self::Count => "count",
            Self::Protocol => "protocol",
            Self::Order => "order",
            Self::Size => "size",
            Self::Hybrid => "hybrid",
        }
    }
    pub fn bits(self) -> usize {
        if self == Self::Hybrid {
            3
        } else {
            1
        }
    }
    pub fn alphabet(self) -> usize {
        1 << self.bits()
    }
    pub fn max_events(self) -> usize {
        match self {
            Self::Count => 4,
            Self::Interval | Self::Presence | Self::Order => 2,
            _ => 1,
        }
    }
}
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Fec {
    None = 0,
    Repeat3 = 1,
    Hamming84 = 2,
}
impl Fec {
    pub fn parse(s: &str) -> Result<Self> {
        match s {
            "none" => Ok(Self::None),
            "repeat3" => Ok(Self::Repeat3),
            "hamming84" => Ok(Self::Hamming84),
            _ => Err(Error::new(4, "unknown error correction code")),
        }
    }
    pub fn from_id(n: u32) -> Result<Self> {
        match n {
            0 => Ok(Self::None),
            1 => Ok(Self::Repeat3),
            2 => Ok(Self::Hamming84),
            _ => Err(Error::new(4, "unknown error correction code ID")),
        }
    }
    pub fn name(self) -> &'static str {
        match self {
            Self::None => "none",
            Self::Repeat3 => "repeat3",
            Self::Hamming84 => "hamming84",
        }
    }
    fn scale(self) -> usize {
        match self {
            Self::None => 1,
            Self::Repeat3 => 3,
            Self::Hamming84 => 2,
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Codec {
    pub mode: Mode,
    pub fec: Fec,
    pub interleave: usize,
    pub slot_us: u64,
    pub tolerance_us: u64,
    pub sync_search_us: u64,
    pub small_size: u32,
    pub large_size: u32,
}
impl Default for Codec {
    fn default() -> Self {
        Self {
            mode: Mode::Interval,
            fec: Fec::Hamming84,
            interleave: 16,
            slot_us: 10_000,
            tolerance_us: 1000,
            sync_search_us: 10_000,
            small_size: 64,
            large_size: 192,
        }
    }
}
impl Codec {
    pub fn validate(&self) -> Result<()> {
        require(
            (64..=1_000_000).contains(&self.slot_us),
            4,
            "symbol period must be 64..1000000 microseconds",
        )?;
        require(
            self.interleave > 0 && self.interleave <= 64,
            4,
            "interleave depth must be 1..64",
        )?;
        require(
            self.tolerance_us > 0 && self.tolerance_us <= self.slot_us / 8,
            4,
            "timing tolerance must be within an eighth symbol period",
        )?;
        require(
            self.sync_search_us <= self.slot_us * 2,
            4,
            "clock search exceeds two symbol periods",
        )?;
        require(
            self.small_size > 0 && self.large_size > self.small_size && self.large_size <= 1200,
            4,
            "event sizes must be ordered and within 1200 bytes",
        )
    }
    pub(crate) fn fingerprint(&self) -> u32 {
        let mut b = Vec::new();
        for n in [
            self.mode as u64,
            self.fec as u64,
            self.interleave as u64,
            self.slot_us,
            self.tolerance_us,
            self.sync_search_us,
            self.small_size as u64,
            self.large_size as u64,
        ] {
            b.extend(n.to_be_bytes());
        }
        crc32c(&b)
    }
}
pub const TRAINING: usize = 64;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Plan {
    pub codec: Codec,
    pub frame_id: u64,
    pub payload_bytes: usize,
}
impl Plan {
    pub fn validate(&self) -> Result<()> {
        self.codec.validate()?;
        require(
            self.payload_bytes <= MAX_PAYLOAD,
            7,
            "payload quota exceeded",
        )?;
        require(self.symbols() <= MAX_SYMBOLS, 7, "symbol quota exceeded")?;
        require(
            self.symbols()
                .checked_mul(self.codec.mode.max_events())
                .is_some_and(|n| n <= MAX_EVENTS),
            7,
            "event quota exceeded",
        )
    }
    pub fn coded_bits(&self) -> usize {
        let n = (self.payload_bytes + 28) * 8 * self.codec.fec.scale();
        let block = self.codec.interleave * 8;
        n.div_ceil(block) * block
    }
    pub fn symbols(&self) -> usize {
        TRAINING + self.coded_bits().div_ceil(self.codec.mode.bits())
    }
    pub fn duration_us(&self) -> u64 {
        self.symbols() as u64 * self.codec.slot_us
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Event {
    pub time_us: u64,
    pub size: u32,
    pub kind: u8,
}
#[derive(Clone, Debug)]
pub struct Trace {
    pub plan: Plan,
    pub events: Vec<Event>,
}
impl Trace {
    pub fn validate(&self) -> Result<()> {
        self.plan.validate()?;
        require(
            self.events.len() <= MAX_EVENTS,
            7,
            "observed event quota exceeded",
        )?;
        let mut previous = 0;
        for e in &self.events {
            require(
                e.time_us >= previous
                    && e.time_us <= self.plan.duration_us().saturating_add(10_000_000),
                11,
                "capture time outside finite observation window or not ordered",
            )?;
            require(
                e.kind < 2 && e.size > 0 && e.size <= 65535,
                11,
                "invalid observed event",
            )?;
            previous = e.time_us;
        }
        Ok(())
    }
}
fn hamming(n: u8) -> u8 {
    let mut b = [0u8; 9];
    for (i, p) in [3, 5, 6, 7].into_iter().enumerate() {
        b[p] = (n >> (3 - i)) & 1;
    }
    for p in [1, 2, 4] {
        b[p] = (1..8)
            .filter(|i| i & p != 0 && *i != p)
            .fold(0, |a, i| a ^ b[i]);
    }
    b[8] = b[1..8].iter().fold(0, |a, v| a ^ v);
    (1..=8).fold(0, |a, i| (a << 1) | b[i])
}
pub fn frame_symbols(plan: &Plan, payload: &[u8]) -> Result<Vec<u8>> {
    plan.validate()?;
    require(
        payload.len() == plan.payload_bytes,
        4,
        "payload differs from declared frame length",
    )?;
    let mut frame = Vec::with_capacity(payload.len() + 28);
    frame.extend(b"DLX1");
    frame.extend([0, 1, plan.codec.mode as u8, plan.codec.fec as u8]);
    frame.extend(plan.frame_id.to_be_bytes());
    frame.extend((payload.len() as u32).to_be_bytes());
    frame.extend(plan.codec.fingerprint().to_be_bytes());
    frame.extend(payload);
    frame.extend(crc32c(&frame).to_be_bytes());
    let mut bits = Vec::with_capacity(plan.coded_bits());
    for byte in frame {
        match plan.codec.fec {
            Fec::None => {
                for i in (0..8).rev() {
                    bits.push((byte >> i) & 1);
                }
            }
            Fec::Repeat3 => {
                for i in (0..8).rev() {
                    bits.extend([(byte >> i) & 1; 3]);
                }
            }
            Fec::Hamming84 => {
                for word in [hamming(byte >> 4), hamming(byte & 15)] {
                    for i in (0..8).rev() {
                        bits.push((word >> i) & 1);
                    }
                }
            }
        }
    }
    bits.resize(plan.coded_bits(), 0);
    let block = plan.codec.interleave * 8;
    let mut ordered = vec![0; bits.len()];
    for offset in (0..bits.len()).step_by(block) {
        for row in 0..plan.codec.interleave {
            for column in 0..8 {
                ordered[offset + column * plan.codec.interleave + row] =
                    bits[offset + row * 8 + column];
            }
        }
    }
    let alphabet = plan.codec.mode.alphabet();
    let mut symbols = (0..TRAINING)
        .map(|i| ((i * 5 + i / 8) % alphabet) as u8)
        .collect::<Vec<_>>();
    for chunk in ordered.chunks(plan.codec.mode.bits()) {
        let mut n = 0;
        for i in 0..plan.codec.mode.bits() {
            n = (n << 1) | chunk.get(i).copied().unwrap_or(0);
        }
        symbols.push(n);
    }
    Ok(symbols)
}
pub fn encode(plan: Plan, payload: &[u8], cancel: &Cancellation) -> Result<Trace> {
    let symbols = frame_symbols(&plan, payload)?;
    let mut events = Vec::with_capacity(symbols.len() * plan.codec.mode.max_events());
    let c = &plan.codec;
    for (i, &value) in symbols.iter().enumerate() {
        if i % 1024 == 0 {
            cancel.check()?;
        }
        let base = i as u64 * c.slot_us;
        let mut add = |phase: u64, kind: u8, size: u32| {
            events.push(Event {
                time_us: base + phase,
                size,
                kind,
            })
        };
        let small = c.small_size;
        match c.mode {
            Mode::Absolute => add(c.slot_us * (if value == 0 { 2 } else { 6 }) / 8, 0, small),
            Mode::Interval => {
                add(c.slot_us / 8, 0, small);
                add(c.slot_us * (if value == 0 { 3 } else { 5 }) / 8, 0, small);
            }
            Mode::Presence => {
                add(c.slot_us / 8, 0, small);
                if value == 1 {
                    add(c.slot_us * 5 / 8, 0, small);
                }
            }
            Mode::Count => {
                let n = if value == 0 { 2 } else { 4 };
                for k in 1..=n {
                    add(c.slot_us * k / (n + 1), 0, small);
                }
            }
            Mode::Protocol => add(c.slot_us / 2, value, small),
            Mode::Order => {
                add(c.slot_us / 4, value, small);
                add(c.slot_us * 3 / 4, 1 - value, small);
            }
            Mode::Size => add(
                c.slot_us / 2,
                0,
                if value == 0 { small } else { c.large_size },
            ),
            Mode::Hybrid => add(
                c.slot_us * (if value & 4 == 0 { 2 } else { 6 }) / 8,
                value & 1,
                if value & 2 == 0 { small } else { c.large_size },
            ),
        }
    }
    Ok(Trace { plan, events })
}
fn binary_timing(value: u64, a: u64, b: u64, tolerance: u64) -> Option<u8> {
    if value.abs_diff(a) <= tolerance {
        Some(0)
    } else if value.abs_diff(b) <= tolerance {
        Some(1)
    } else {
        None
    }
}
fn symbol(c: &Codec, events: &[Event], slot: usize, phase: i64) -> Option<u8> {
    let start = slot as i128 * c.slot_us as i128 + phase as i128;
    let times = |e: &Event| u64::try_from(e.time_us as i128 - start).ok();
    match c.mode {
        Mode::Absolute if events.len() == 1 => binary_timing(
            times(&events[0])?,
            c.slot_us / 4,
            c.slot_us * 3 / 4,
            c.tolerance_us,
        ),
        Mode::Interval if events.len() == 2 => binary_timing(
            events[1].time_us - events[0].time_us,
            c.slot_us / 4,
            c.slot_us / 2,
            c.tolerance_us,
        ),
        Mode::Presence if events.len() == 1 || events.len() == 2 => {
            if times(&events[0])?.abs_diff(c.slot_us / 8) > c.tolerance_us {
                return None;
            }
            if events.len() == 1 {
                Some(0)
            } else if times(&events[1])?.abs_diff(c.slot_us * 5 / 8) <= c.tolerance_us {
                Some(1)
            } else {
                None
            }
        }
        Mode::Count => match events.len() {
            2 => Some(0),
            4 => Some(1),
            _ => None,
        },
        Mode::Protocol if events.len() == 1 => Some(events[0].kind),
        Mode::Order
            if events.len() == 2
                && events[0].kind != events[1].kind
                && events[0].time_us != events[1].time_us =>
        {
            Some(events[0].kind)
        }
        Mode::Size if events.len() == 1 => {
            if events[0].size == c.small_size {
                Some(0)
            } else if events[0].size == c.large_size {
                Some(1)
            } else {
                None
            }
        }
        Mode::Hybrid if events.len() == 1 => {
            let t = binary_timing(
                times(&events[0])?,
                c.slot_us / 4,
                c.slot_us * 3 / 4,
                c.tolerance_us,
            )?;
            let size = if events[0].size == c.small_size {
                0
            } else if events[0].size == c.large_size {
                2
            } else {
                return None;
            };
            Some((t << 2) | size | events[0].kind)
        }
        _ => None,
    }
}
fn slot_events(events: &[Event], slot: usize, period: u64, phase: i64) -> &[Event] {
    let a = slot as i128 * period as i128 + phase as i128;
    let b = a + period as i128;
    let first = events.partition_point(|e| (e.time_us as i128) < a);
    let last = events.partition_point(|e| (e.time_us as i128) < b);
    &events[first..last]
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Delivery {
    Valid,
    Erased,
    SyncLost,
    IntegrityFailed,
}
impl Delivery {
    pub fn name(self) -> &'static str {
        match self {
            Self::Valid => "FRAME_VALID",
            Self::Erased => "ERASED",
            Self::SyncLost => "SYNC_LOST",
            Self::IntegrityFailed => "INTEGRITY_FAILED",
        }
    }
}
#[derive(Clone, Debug)]
pub struct Decoded {
    pub delivery: Delivery,
    pub payload: Option<Vec<u8>>,
    pub symbols: Vec<Option<u8>>,
    pub phase_us: i64,
    pub corrected_words: u64,
    pub training_matches: usize,
}
pub fn observe(trace: &Trace, cancel: &Cancellation) -> Result<(Vec<Option<u8>>, i64, usize)> {
    trace.validate()?;
    let c = &trace.plan.codec;
    let alphabet = c.mode.alphabet();
    let width = c.sync_search_us as i64;
    let step = ((2 * width + 63) / 64).max(1);
    let mut candidates = vec![0, -width, width];
    let mut x = -width;
    while x <= width {
        candidates.push(x);
        x += step;
    }
    candidates.sort_unstable();
    candidates.dedup();
    let mut best = (0usize, 0usize, 0i64);
    for phase in candidates {
        cancel.check()?;
        let mut matches = 0;
        let mut valid = 0;
        for i in 0..TRAINING {
            if let Some(value) =
                symbol(c, slot_events(&trace.events, i, c.slot_us, phase), i, phase)
            {
                valid += 1;
                if value as usize == (i * 5 + i / 8) % alphabet {
                    matches += 1;
                }
            }
        }
        if (matches, valid, std::cmp::Reverse(phase.unsigned_abs()))
            > (best.0, best.1, std::cmp::Reverse(best.2.unsigned_abs()))
        {
            best = (matches, valid, phase);
        }
    }
    let sync = best.1 >= TRAINING / 4 && best.0 * 4 >= best.1 * 3;
    let phase = best.2;
    let mut values = Vec::with_capacity(trace.plan.symbols());
    for i in 0..trace.plan.symbols() {
        if i % 1024 == 0 {
            cancel.check()?;
        }
        values.push(symbol(
            c,
            slot_events(&trace.events, i, c.slot_us, phase),
            i,
            phase,
        ));
    }
    Ok((values, phase, if sync { best.0 } else { 0 }))
}
fn recover_word(input: &[Option<u8>], code: Fec) -> Option<(u8, bool)> {
    let erasures = input.iter().filter(|v| v.is_none()).count();
    let (count, distance) = match code {
        Fec::Hamming84 => (16, 4),
        Fec::Repeat3 => (2, 3),
        Fec::None => (2, 1),
    };
    let mut answer = None;
    for n in 0..count {
        let word = if code == Fec::Hamming84 {
            hamming(n as u8)
        } else {
            if n == 0 {
                0
            } else {
                (1 << input.len()) - 1
            }
        };
        let errors = input
            .iter()
            .enumerate()
            .filter(|(i, v)| v.is_some_and(|v| v != ((word >> (input.len() - 1 - i)) & 1)))
            .count();
        if 2 * errors + erasures < distance {
            if answer.is_some() {
                return None;
            }
            answer = Some((n as u8, errors > 0 || erasures > 0));
        }
    }
    answer
}
pub fn decode(trace: &Trace, cancel: &Cancellation) -> Result<Decoded> {
    let (values, phase, matches) = observe(trace, cancel)?;
    let mut result = Decoded {
        delivery: Delivery::SyncLost,
        payload: None,
        symbols: values,
        phase_us: phase,
        corrected_words: 0,
        training_matches: matches,
    };
    if matches == 0 {
        return Ok(result);
    }
    let plan = &trace.plan;
    let mut received = Vec::with_capacity(plan.coded_bits());
    for symbol in &result.symbols[TRAINING..] {
        for bit in (0..plan.codec.mode.bits()).rev() {
            received.push(symbol.map(|v| (v >> bit) & 1));
        }
    }
    received.truncate(plan.coded_bits());
    let block = plan.codec.interleave * 8;
    let mut bits = vec![None; received.len()];
    for offset in (0..bits.len()).step_by(block) {
        for row in 0..plan.codec.interleave {
            for column in 0..8 {
                bits[offset + row * 8 + column] =
                    received[offset + column * plan.codec.interleave + row];
            }
        }
    }
    bits.truncate((plan.payload_bytes + 28) * 8 * plan.codec.fec.scale());
    let word = match plan.codec.fec {
        Fec::None => 1,
        Fec::Repeat3 => 3,
        Fec::Hamming84 => 8,
    };
    let mut decoded = Vec::with_capacity((plan.payload_bytes + 28) * 8);
    for b in bits.chunks_exact(word) {
        if let Some((v, corrected)) = recover_word(b, plan.codec.fec) {
            result.corrected_words += corrected as u64;
            let width = if plan.codec.fec == Fec::Hamming84 {
                4
            } else {
                1
            };
            for i in (0..width).rev() {
                decoded.push((v >> i) & 1);
            }
        } else {
            result.delivery = Delivery::Erased;
            return Ok(result);
        }
    }
    let frame: Vec<u8> = decoded
        .chunks_exact(8)
        .map(|b| b.iter().fold(0, |n, v| (n << 1) | v))
        .collect();
    let valid = frame.len() == plan.payload_bytes + 28
        && &frame[..4] == b"DLX1"
        && frame[4..8] == [0, 1, plan.codec.mode as u8, plan.codec.fec as u8]
        && frame[8..16] == plan.frame_id.to_be_bytes()
        && frame[16..20] == (plan.payload_bytes as u32).to_be_bytes()
        && frame[20..24] == plan.codec.fingerprint().to_be_bytes()
        && crc32c(&frame[..frame.len() - 4]).to_be_bytes() == frame[frame.len() - 4..];
    if valid {
        result.delivery = Delivery::Valid;
        result.payload = Some(frame[24..frame.len() - 4].to_vec());
    } else {
        result.delivery = Delivery::IntegrityFailed;
    }
    Ok(result)
}
#[cfg(test)]
mod code_tests {
    use super::*;
    #[test]
    fn secded_exhaustively_corrects_one_error_and_three_erasures_and_rejects_double_errors() {
        for n in 0..16 {
            let word = hamming(n);
            let original: Vec<Option<u8>> = (0..8).rev().map(|i| Some((word >> i) & 1)).collect();
            assert_eq!(recover_word(&original, Fec::Hamming84), Some((n, false)));
            for a in 0..8 {
                let mut bits = original.clone();
                bits[a] = bits[a].map(|v| v ^ 1);
                assert_eq!(recover_word(&bits, Fec::Hamming84), Some((n, true)));
                for b in a + 1..8 {
                    let mut two = bits.clone();
                    two[b] = two[b].map(|v| v ^ 1);
                    assert!(recover_word(&two, Fec::Hamming84).is_none());
                    for c in b + 1..8 {
                        let mut erased = original.clone();
                        for i in [a, b, c] {
                            erased[i] = None;
                        }
                        assert_eq!(recover_word(&erased, Fec::Hamming84), Some((n, true)));
                    }
                }
            }
        }
    }
    #[test]
    fn repeat3_corrects_single_bit_and_two_erasures() {
        for n in 0..2 {
            for a in 0..3 {
                let mut bits = vec![Some(n); 3];
                bits[a] = Some(n ^ 1);
                assert_eq!(recover_word(&bits, Fec::Repeat3), Some((n, true)));
                bits.fill(None);
                bits[a] = Some(n);
                assert_eq!(recover_word(&bits, Fec::Repeat3), Some((n, true)));
            }
        }
    }
}
