use crate::{
    codec::{Trace},
    require, Cancellation, Random, Result, MAX_EVENTS,
};
#[derive(Clone, Debug, PartialEq)]
pub struct Noise {
    pub seed: u64,
    pub delay_us: u64,
    pub jitter_us: u64,
    pub loss_ppm: u32,
    pub duplicate_ppm: u32,
    pub reorder_ppm: u32,
    pub reorder_delay_us: u64,
    pub good_to_bad_ppm: u32,
    pub bad_to_good_ppm: u32,
    pub bad_loss_ppm: u32,
    pub kind_flip_ppm: u32,
    pub tcp_delay_us: u64,
    pub reverse_delay_us: u64,
    pub reverse: bool,
    pub clock_offset_us: i64,
    pub clock_drift_ppm: i64,
    pub rate_bytes_per_second: u64,
    pub time_quantum_us: u64,
    pub normalize_time: bool,
    pub normalize_size: Option<u32>,
    pub normalize_kind: Option<u8>,
    pub normalize_count: Option<u8>,
    pub burst_period_us: u64,
    pub burst_width_us: u64,
    pub blocked: bool,
}
impl Default for Noise {
    fn default() -> Self {
        Self {
            seed: 1,
            delay_us: 0,
            jitter_us: 0,
            loss_ppm: 0,
            duplicate_ppm: 0,
            reorder_ppm: 0,
            reorder_delay_us: 0,
            good_to_bad_ppm: 0,
            bad_to_good_ppm: 0,
            bad_loss_ppm: 1_000_000,
            kind_flip_ppm: 0,
            tcp_delay_us: 0,
            reverse_delay_us: 0,
            reverse: false,
            clock_offset_us: 0,
            clock_drift_ppm: 0,
            rate_bytes_per_second: 0,
            time_quantum_us: 0,
            normalize_time: false,
            normalize_size: None,
            normalize_kind: None,
            normalize_count: None,
            burst_period_us: 0,
            burst_width_us: 0,
            blocked: false,
        }
    }
}
impl Noise {
    pub fn validate(&self) -> Result<()> {
        for n in [
            self.loss_ppm,
            self.duplicate_ppm,
            self.reorder_ppm,
            self.good_to_bad_ppm,
            self.bad_to_good_ppm,
            self.bad_loss_ppm,
            self.kind_flip_ppm,
        ] {
            require(n <= 1_000_000, 4, "probabilities must be 0..1000000 ppm")?;
        }
        require(
            self.delay_us <= 2_000_000
                && self.jitter_us <= 1_000_000
                && self.reorder_delay_us <= 2_000_000
                && self.tcp_delay_us <= 2_000_000
                && self.reverse_delay_us <= 2_000_000,
            4,
            "delay model exceeds bounded observation window",
        )?;
        require(
            self.clock_offset_us.unsigned_abs() <= 2_000_000
                && self.clock_drift_ppm.unsigned_abs() <= 100_000,
            4,
            "receiver clock model out of range",
        )?;
        require(
            self.rate_bytes_per_second <= 1_000_000_000 && self.time_quantum_us <= 1_000_000,
            4,
            "rate shaper or time quantization out of range",
        )?;
        require(
            self.normalize_size.is_none_or(|v| v > 0 && v <= 65535)
                && self.normalize_kind.is_none_or(|v| v < 2)
                && self.normalize_count.is_none_or(|v| (1..=4).contains(&v)),
            4,
            "invalid normalization output",
        )?;
        require(
            (self.burst_period_us == 0 && self.burst_width_us == 0)
                || (self.burst_period_us > 0 && self.burst_width_us <= self.burst_period_us),
            4,
            "invalid deterministic burst window",
        )
    }
    pub fn json(&self) -> String {
        format!("{{\"seed\":{},\"delay_us\":{},\"jitter_us\":{},\"loss_ppm\":{},\"duplicate_ppm\":{},\"reorder_ppm\":{},\"reorder_delay_us\":{},\"good_to_bad_ppm\":{},\"bad_to_good_ppm\":{},\"bad_loss_ppm\":{},\"kind_flip_ppm\":{},\"tcp_delay_us\":{},\"reverse_delay_us\":{},\"reverse\":{},\"clock_offset_us\":{},\"clock_drift_ppm\":{},\"rate_bytes_per_second\":{},\"time_quantum_us\":{},\"normalize_time\":{},\"normalize_size\":{},\"normalize_kind\":{},\"normalize_count\":{},\"burst_period_us\":{},\"burst_width_us\":{},\"blocked\":{}}}",self.seed,self.delay_us,self.jitter_us,self.loss_ppm,self.duplicate_ppm,self.reorder_ppm,self.reorder_delay_us,self.good_to_bad_ppm,self.bad_to_good_ppm,self.bad_loss_ppm,self.kind_flip_ppm,self.tcp_delay_us,self.reverse_delay_us,self.reverse,self.clock_offset_us,self.clock_drift_ppm,self.rate_bytes_per_second,self.time_quantum_us,self.normalize_time,self.normalize_size.map_or("null".into(),|n|n.to_string()),self.normalize_kind.map_or("null".into(),|n|n.to_string()),self.normalize_count.map_or("null".into(),|n|n.to_string()),self.burst_period_us,self.burst_width_us,self.blocked)
    }
    pub fn has_memory(&self) -> bool {
        self.good_to_bad_ppm > 0
            || self.burst_width_us > 0
            || self.rate_bytes_per_second > 0
            || self.reorder_ppm > 0
            || self.clock_drift_ppm != 0
            || self.time_quantum_us > 0
    }
}
#[derive(Clone, Debug, Default)]
pub struct Distortion {
    pub sent_events: u64,
    pub received_events: u64,
    pub lost_events: u64,
    pub duplicates: u64,
    pub reordered: u64,
    pub bad_state_events: u64,
    pub outside_window: u64,
    pub normalized_added: u64,
    pub normalized_removed: u64,
}
pub fn distort(trace: &Trace, noise: &Noise, cancel: &Cancellation) -> Result<(Trace, Distortion)> {
    trace.validate()?;
    noise.validate()?;
    let mut rng = Random::new(noise.seed);
    let mut out = Vec::with_capacity(trace.events.len());
    let mut bad = false;
    let mut stats = Distortion {
        sent_events: trace.events.len() as u64,
        ..Default::default()
    };
    let end = trace.plan.duration_us() + 10_000_000;
    for (i, event) in trace.events.iter().enumerate() {
        if i % 1024 == 0 {
            cancel.check()?;
        }
        let burst = noise.burst_period_us > 0
            && event.time_us % noise.burst_period_us < noise.burst_width_us;
        stats.bad_state_events += bad as u64;
        let lost = noise.blocked
            || burst
            || rng.chance(if bad {
                noise.bad_loss_ppm
            } else {
                noise.loss_ppm
            });
        if rng.chance(if bad {
            noise.bad_to_good_ppm
        } else {
            noise.good_to_bad_ppm
        }) {
            bad = !bad;
        }
        if lost {
            stats.lost_events += 1;
            continue;
        }
        let mut observed = *event;
        let jitter = (rng.normal() * noise.jitter_us as f64).round() as i64;
        let mut time = event.time_us as i128 + noise.delay_us as i128 + jitter as i128;
        if event.kind == 1 {
            time += noise.tcp_delay_us as i128;
        }
        if noise.reverse {
            time += noise.reverse_delay_us as i128;
        }
        if rng.chance(noise.reorder_ppm) {
            time += noise.reorder_delay_us as i128;
            stats.reordered += 1;
        }
        if rng.chance(noise.kind_flip_ppm) {
            observed.kind ^= 1;
        }
        if let Some(n) = noise.normalize_size {
            observed.size = n;
        }
        if let Some(n) = noise.normalize_kind {
            observed.kind = n;
        }
        if time < 0 || time > end as i128 {
            stats.outside_window += 1;
            continue;
        }
        observed.time_us = time as u64;
        require(
            out.len() < MAX_EVENTS,
            7,
            "distortion event storage quota exceeded",
        )?;
        out.push(observed);
        if rng.chance(noise.duplicate_ppm) {
            require(
                out.len() < MAX_EVENTS,
                7,
                "duplicate event storage quota exceeded",
            )?;
            let mut copy = observed;
            copy.time_us = copy.time_us.saturating_add(1);
            out.push(copy);
            stats.duplicates += 1;
        }
    }
    out.sort_by_key(|e| e.time_us);
    let mut free = 0u64;
    let period = trace.plan.codec.slot_us;
    for event in &mut out {
        if noise.rate_bytes_per_second > 0 {
            let duration =
                ((event.size as u64 + 2) * 1_000_000).div_ceil(noise.rate_bytes_per_second);
            event.time_us = event.time_us.max(free).saturating_add(duration);
            free = event.time_us;
        }
        if noise.normalize_time {
            event.time_us = event.time_us / period * period + period / 2;
        }
        if noise.time_quantum_us > 0 {
            event.time_us = event.time_us / noise.time_quantum_us * noise.time_quantum_us;
        }
        let value = event.time_us as i128
            + noise.clock_offset_us as i128
            + event.time_us as i128 * noise.clock_drift_ppm as i128 / 1_000_000;
        event.time_us = if value < 0 {
            u64::MAX
        } else {
            u64::try_from(value).unwrap_or(u64::MAX)
        };
    }
    if let Some(count) = noise.normalize_count {
        out.sort_by_key(|e| e.time_us);
        let mut normalized = Vec::with_capacity(out.len());
        let mut at = 0;
        while at < out.len() {
            cancel.check()?;
            let slot = out[at].time_us / period;
            let start = at;
            while at < out.len() && out[at].time_us / period == slot {
                at += 1;
            }
            let before = at - start;
            stats.normalized_added += (count as usize).saturating_sub(before) as u64;
            stats.normalized_removed += before.saturating_sub(count as usize) as u64;
            require(
                normalized.len() + count as usize <= MAX_EVENTS,
                7,
                "normalizer event quota",
            )?;
            for i in 0..count {
                let mut event = out[start + (i as usize).min(before - 1)];
                event.time_us = slot * period + period * (i as u64 + 1) / (count as u64 + 1);
                normalized.push(event);
            }
        }
        out = normalized;
    }
    out.retain(|e| {
        let keep = e.time_us <= end;
        if !keep {
            stats.outside_window += 1;
        }
        keep
    });
    out.sort_by_key(|e| e.time_us);
    stats.received_events = out.len() as u64;
    let result = Trace {
        plan: trace.plan.clone(),
        events: out,
    };
    result.validate()?;
    Ok((result, stats))
}
pub fn scenarios(base: &Noise) -> Vec<(&'static str, Noise)> {
    let mut cases = Vec::new();
    let mut clean = Noise::default();
    clean.seed = base.seed;
    cases.push(("clean", clean.clone()));
    cases.push(("configured", base.clone()));
    let mut n = clean.clone();
    n.jitter_us = 500;
    cases.push(("gaussian_jitter", n));
    let mut n = clean.clone();
    n.loss_ppm = 50_000;
    cases.push(("independent_loss_5pct", n));
    let mut n = clean.clone();
    n.good_to_bad_ppm = 30_000;
    n.bad_to_good_ppm = 150_000;
    cases.push(("gilbert_elliott", n));
    let mut n = clean.clone();
    n.reorder_ppm = 50_000;
    n.reorder_delay_us = 15_000;
    cases.push(("reordering", n));
    let mut n = clean.clone();
    n.duplicate_ppm = 50_000;
    cases.push(("duplicates", n));
    let mut n = clean.clone();
    n.rate_bytes_per_second = 20_000;
    cases.push(("fifo_rate_shaping", n));
    let mut n = clean.clone();
    n.normalize_time = true;
    n.normalize_size = Some(64);
    n.normalize_kind = Some(0);
    n.normalize_count = Some(2);
    cases.push(("observable_normalization", n));
    let mut n = clean.clone();
    n.blocked = true;
    cases.push(("closed_channel", n));
    let mut n = clean.clone();
    n.burst_period_us = 300_000;
    n.burst_width_us = 20_000;
    cases.push(("deterministic_bursts", n));
    let mut n = clean;
    n.reverse = true;
    n.reverse_delay_us = 3_000;
    n.tcp_delay_us = 2_000;
    cases.push(("asymmetric_reverse", n));
    cases
}
