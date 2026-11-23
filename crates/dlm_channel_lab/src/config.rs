use crate::{
    codec::{Codec, Fec, Mode},
    model::Noise,
    require, Error, Result,
};
use std::collections::BTreeMap;
#[derive(Clone, Debug)]
pub struct Config {
    pub codec: Codec,
    pub noise: Noise,
    pub frame_id: u64,
    pub live_max_ms: u64,
    pub bootstrap_samples: usize,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            codec: Codec::default(),
            noise: Noise::default(),
            frame_id: 1,
            live_max_ms: 10_000,
            bootstrap_samples: 128,
        }
    }
}
impl Config {
    pub fn validate(&self) -> Result<()> {
        self.codec.validate()?;
        self.noise.validate()?;
        require(
            (100..=30_000).contains(&self.live_max_ms),
            4,
            "loopback deadline must be 100..30000 milliseconds",
        )?;
        require(
            self.bootstrap_samples >= 32 && self.bootstrap_samples <= 512,
            4,
            "bootstrap samples must be 32..512",
        )
    }
    pub fn parse(text: &str) -> Result<Self> {
        require(
            text.len() <= 65536 && !text.contains('\0'),
            4,
            "configuration text length or NUL invalid",
        )?;
        let mut v = BTreeMap::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let (k, val) = line
                .split_once('=')
                .ok_or(Error::new(4, "configuration requires key=value"))?;
            let (k, val) = (k.trim(), val.trim());
            require(
                !k.is_empty() && !val.is_empty() && v.insert(k, val).is_none(),
                4,
                "empty or duplicate configuration field",
            )?;
        }
        let mut c = Self::default();
        if let Some(s) = v.remove("mode") {
            c.codec.mode = Mode::parse(s)?;
        }
        if let Some(s) = v.remove("fec") {
            c.codec.fec = Fec::parse(s)?;
        }
        macro_rules! number {
            ($key:literal,$field:expr) => {
                if let Some(s) = v.remove($key) {
                    $field = s
                        .parse()
                        .map_err(|_| Error::new(4, concat!("invalid ", $key)))?;
                }
            };
        }
        macro_rules! boolean {
            ($key:literal,$field:expr) => {
                if let Some(s) = v.remove($key) {
                    $field = match s {
                        "true" => true,
                        "false" => false,
                        _ => return Err(Error::new(4, concat!("invalid ", $key))),
                    };
                }
            };
        }
        number!("slot_us", c.codec.slot_us);
        c.codec.tolerance_us = (c.codec.slot_us / 10).max(1);
        c.codec.sync_search_us = c.codec.slot_us;
        number!("tolerance_us", c.codec.tolerance_us);
        number!("sync_search_us", c.codec.sync_search_us);
        number!("interleave", c.codec.interleave);
        number!("small_size", c.codec.small_size);
        number!("large_size", c.codec.large_size);
        number!("frame_id", c.frame_id);
        number!("live_max_ms", c.live_max_ms);
        number!("bootstrap_samples", c.bootstrap_samples);
        number!("seed", c.noise.seed);
        number!("delay_us", c.noise.delay_us);
        number!("jitter_us", c.noise.jitter_us);
        number!("loss_ppm", c.noise.loss_ppm);
        number!("duplicate_ppm", c.noise.duplicate_ppm);
        number!("reorder_ppm", c.noise.reorder_ppm);
        number!("reorder_delay_us", c.noise.reorder_delay_us);
        number!("good_to_bad_ppm", c.noise.good_to_bad_ppm);
        number!("bad_to_good_ppm", c.noise.bad_to_good_ppm);
        number!("bad_loss_ppm", c.noise.bad_loss_ppm);
        number!("kind_flip_ppm", c.noise.kind_flip_ppm);
        number!("tcp_delay_us", c.noise.tcp_delay_us);
        number!("reverse_delay_us", c.noise.reverse_delay_us);
        number!("clock_offset_us", c.noise.clock_offset_us);
        number!("clock_drift_ppm", c.noise.clock_drift_ppm);
        number!("rate_bytes_per_second", c.noise.rate_bytes_per_second);
        number!("time_quantum_us", c.noise.time_quantum_us);
        number!("burst_period_us", c.noise.burst_period_us);
        number!("burst_width_us", c.noise.burst_width_us);
        boolean!("normalize_time", c.noise.normalize_time);
        boolean!("blocked", c.noise.blocked);
        boolean!("reverse", c.noise.reverse);
        if let Some(s) = v.remove("normalize_size") {
            c.noise.normalize_size = Some(
                s.parse()
                    .map_err(|_| Error::new(4, "invalid normalized size"))?,
            );
        }
        if let Some(s) = v.remove("normalize_kind") {
            c.noise.normalize_kind = Some(
                s.parse()
                    .map_err(|_| Error::new(4, "invalid normalized kind"))?,
            );
        }
        if let Some(s) = v.remove("normalize_count") {
            c.noise.normalize_count = Some(
                s.parse()
                    .map_err(|_| Error::new(4, "invalid normalized event count"))?,
            );
        }
        require(v.is_empty(), 4, "unknown laboratory configuration field")?;
        c.validate()?;
        Ok(c)
    }
}
