use crate::{
    codec::{self, Decoded, Plan, Trace},
    config::Config,
    model::{self, Distortion},
    statistics, require, Cancellation, Result,
};
pub struct Measurement {
    pub capture: Trace,
    pub decoded: Decoded,
    pub report: String,
}
fn finish(
    config: &Config,
    payload: &[u8],
    capture: Trace,
    distortion: Distortion,
    scope: &str,
    elapsed_us: u64,
    cancel: &Cancellation,
) -> Result<Measurement> {
    let decoded = codec::decode(&capture, cancel)?;
    let expected = codec::frame_symbols(&capture.plan, payload)?;
    let statistics = statistics::evaluate(
        &expected,
        &decoded.symbols,
        capture.plan.codec.mode.alphabet(),
        config.noise.has_memory(),
        config.bootstrap_samples,
        config.noise.seed,
        cancel,
    )?;
    let valid = decoded.payload.as_deref() == Some(payload);
    require(
        decoded.payload.is_none() || valid,
        12,
        "decoded frame differs from transmitted binary payload",
    )?;
    let decoded_bytes = decoded.payload.as_ref().map_or(0, Vec::len);
    let body_bytes = capture.events.iter().map(|e| e.size as u64).sum::<u64>();
    let json=format!("{{\"format\":\"DLM-INFORMATION-CHANNEL-LAB-v1\",\"version\":\"{}\",\"status\":\"MEASURED\",\"scope\":\"{}\",\"mode\":\"{}\",\"fec\":\"{}\",\"frame_id\":{},\"seed\":{},\"direction\":\"{}\",\"delivery\":\"{}\",\"payload_bytes\":{},\"decoded_bytes\":{},\"payload_exact_match\":{},\"corrected_words\":{},\"clock_phase_us\":{},\"training_matches\":{},\"symbol_period_us\":{},\"observation_window_us\":{},\"useful_bits_per_second\":{},\"observed_body_bytes\":{},\"sent_events\":{},\"received_events\":{},\"lost_events\":{},\"duplicates\":{},\"reordered_events\":{},\"bad_state_events\":{},\"outside_window_events\":{},\"normalizer_added_events\":{},\"normalizer_removed_events\":{},\"noise_model\":{},\"interleave_depth\":{},\"small_size\":{},\"large_size\":{},\"timing_tolerance_us\":{},\"clock_search_us\":{},\"bootstrap_samples\":{},\"statistics\":{},\"framing_integrity\":\"CRC32C; not authentication\",\"production_carrier\":false,\"public_internet_tested\":false}}",env!("CARGO_PKG_VERSION"),scope,config.codec.mode.name(),config.codec.fec.name(),config.frame_id,config.noise.seed,if config.noise.reverse{"reverse"}else{"forward"},decoded.delivery.name(),payload.len(),decoded_bytes,valid,decoded.corrected_words,decoded.phase_us,decoded.training_matches,config.codec.slot_us,elapsed_us,if valid{payload.len()as f64*8.0*1_000_000.0/elapsed_us.max(1)as f64}else{0.0},body_bytes,distortion.sent_events,distortion.received_events,distortion.lost_events,distortion.duplicates,distortion.reordered,distortion.bad_state_events,distortion.outside_window,distortion.normalized_added,distortion.normalized_removed,config.noise.json(),config.codec.interleave,config.codec.small_size,config.codec.large_size,config.codec.tolerance_us,config.codec.sync_search_us,config.bootstrap_samples,statistics.json(config.codec.slot_us));
    Ok(Measurement {
        capture,
        decoded,
        report: json,
    })
}
pub fn simulate(config: &Config, payload: &[u8], cancel: &Cancellation) -> Result<Measurement> {
    config.validate()?;
    let planned = codec::encode(
        Plan {
            codec: config.codec.clone(),
            frame_id: config.frame_id,
            payload_bytes: payload.len(),
        },
        payload,
        cancel,
    )?;
    let (capture, stats) = model::distort(&planned, &config.noise, cancel)?;
    let elapsed = capture
        .events
        .last()
        .map_or(0, |e| e.time_us + 1)
        .max(planned.plan.duration_us());
    finish(
        config,
        payload,
        capture,
        stats,
        "deterministic_local_event_model",
        elapsed,
        cancel,
    )
}
pub fn loopback(config: &Config, payload: &[u8], cancel: &Cancellation) -> Result<Measurement> {
    let planned = codec::encode(
        Plan {
            codec: config.codec.clone(),
            frame_id: config.frame_id,
            payload_bytes: payload.len(),
        },
        payload,
        cancel,
    )?;
    let observation = crate::live::run(&planned, config, cancel)?;
    let stats = Distortion {
        sent_events: planned.events.len() as u64,
        received_events: observation.trace.events.len() as u64,
        ..Default::default()
    };
    finish(
        config,
        payload,
        observation.trace,
        stats,
        "physical_loopback_TCP_UDP_application_events",
        observation.elapsed_us,
        cancel,
    )
}
pub fn matrix(config: &Config, payload: &[u8], cancel: &Cancellation) -> Result<String> {
    config.validate()?;
    let mut values = Vec::new();
    for mode in codec::Mode::ALL {
        for (name, noise) in model::scenarios(&config.noise) {
            cancel.check()?;
            let mut c = config.clone();
            c.codec.mode = mode;
            c.noise = noise;
            let m = simulate(&c, payload, cancel)?;
            values.push(format!(
                "{{\"case\":\"{}\",\"measurement\":{}}}",
                name, m.report
            ));
        }
    }
    Ok(format!("{{\"format\":\"DLM-INFORMATION-CHANNEL-MATRIX-v1\",\"status\":\"MEASURED\",\"cases\":[{}],\"production_carrier\":false}}",values.join(",")))
}
pub fn decoded_report(decoded: &Decoded) -> String {
    format!("{{\"format\":\"DLM-CHANNEL-DECODE-v1\",\"delivery\":\"{}\",\"decoded_bytes\":{},\"corrected_words\":{},\"clock_phase_us\":{},\"training_matches\":{},\"original_payload_available_to_decoder\":false,\"framing_integrity\":\"CRC32C; not authentication\"}}",decoded.delivery.name(),decoded.payload.as_ref().map_or(0,Vec::len),decoded.corrected_words,decoded.phase_us,decoded.training_matches)
}
