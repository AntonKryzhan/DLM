use dlm_channel_lab::{
    codec::{self, Codec, Delivery, Fec, Mode, Plan, TRAINING},
    config::Config,
    experiment, live,
    model::{self, Noise},
    statistics, trace, Cancellation, MAX_EVENTS, MAX_PAYLOAD,
};
fn plan(mode: Mode, fec: Fec, n: usize) -> Plan {
    Plan {
        codec: Codec {
            mode,
            fec,
            ..Codec::default()
        },
        frame_id: 0x8877665544332211,
        payload_bytes: n,
    }
}
#[test]
fn every_alphabet_and_code_round_trips_empty_nul_ff_and_binary_boundaries() {
    let token = Cancellation::default();
    for mode in Mode::ALL {
        for code in [Fec::None, Fec::Repeat3, Fec::Hamming84] {
            for data in [
                vec![],
                vec![0, 255, 65],
                (0..=255).collect(),
                vec![0; 63],
                vec![255; 65],
            ] {
                let t = codec::encode(plan(mode, code, data.len()), &data, &token).unwrap();
                let captured = trace::decode(&trace::encode(&t).unwrap()).unwrap();
                let d = codec::decode(&captured, &token).unwrap();
                assert_eq!(d.delivery, Delivery::Valid, "{mode:?}/{code:?}");
                assert_eq!(d.payload, Some(data));
            }
        }
    }
}
#[test]
fn decoder_needs_no_original_payload_and_public_metadata_cannot_replace_events() {
    let token = Cancellation::default();
    let data = b"\0secret-observed-only\xff";
    for mode in Mode::ALL {
        let t = codec::encode(plan(mode, Fec::Hamming84, data.len()), data, &token).unwrap();
        let wire = trace::encode(&t).unwrap();
        let mut t = trace::decode(&wire).unwrap();
        t.events.clear();
        let d = codec::decode(&t, &token).unwrap();
        assert_eq!(d.delivery, Delivery::SyncLost);
        assert!(d.payload.is_none());
        let n = Noise {
            blocked: true,
            ..Noise::default()
        };
        let t = trace::decode(&wire).unwrap();
        let (t, s) = model::distort(&t, &n, &token).unwrap();
        assert_eq!(s.received_events, 0);
        assert_eq!(
            codec::decode(&t, &token).unwrap().delivery,
            Delivery::SyncLost
        );
    }
}
#[test]
fn interleaving_recovers_a_burst_that_overwhelms_one_secded_word() {
    let token = Cancellation::default();
    let data = b"burst\0\xff";
    for depth in [1, 16] {
        let mut p = plan(Mode::Protocol, Fec::Hamming84, data.len());
        p.codec.interleave = depth;
        let mut t = codec::encode(p, data, &token).unwrap();
        for event in &mut t.events[TRAINING..TRAINING + 8] {
            event.kind ^= 1;
        }
        let d = codec::decode(&t, &token).unwrap();
        if depth == 16 {
            assert_eq!(d.payload.as_deref(), Some(&data[..]));
            assert!(d.corrected_words >= 8);
        } else {
            assert!(d.payload.is_none());
        }
    }
}
#[test]
fn crc_catches_a_different_valid_codeword() {
    let token = Cancellation::default();
    let data = b"frame-integrity";
    let mut t = codec::encode(plan(Mode::Size, Fec::None, data.len()), data, &token).unwrap();
    let index = TRAINING + 24 * 8;
    let c = &t.plan.codec;
    t.events[index].size = if t.events[index].size == c.small_size {
        c.large_size
    } else {
        c.small_size
    };
    let d = codec::decode(&t, &token).unwrap();
    assert_eq!(d.delivery, Delivery::IntegrityFailed);
    assert!(d.payload.is_none());
}
#[test]
fn receiver_searches_clock_offset_without_using_payload_bits() {
    let token = Cancellation::default();
    let data = b"phase drift";
    for mode in Mode::ALL {
        let t = codec::encode(plan(mode, Fec::Hamming84, data.len()), data, &token).unwrap();
        let n = Noise {
            clock_offset_us: 7_000,
            ..Noise::default()
        };
        let (t, _) = model::distort(&t, &n, &token).unwrap();
        let d = codec::decode(&t, &token).unwrap();
        assert_eq!(d.payload.as_deref(), Some(&data[..]), "{mode:?}");
    }
}
#[test]
fn gaussian_loss_burst_shaping_reorder_and_normalizers_are_seeded_and_bounded() {
    let token = Cancellation::default();
    let data = vec![93; 120];
    let t = codec::encode(
        plan(Mode::Hybrid, Fec::Hamming84, data.len()),
        &data,
        &token,
    )
    .unwrap();
    for (_, mut noise) in model::scenarios(&Noise::default()) {
        noise.seed = 0x123456;
        let (a, x) = model::distort(&t, &noise, &token).unwrap();
        let (b, y) = model::distort(&t, &noise, &token).unwrap();
        assert_eq!(a.events, b.events);
        assert_eq!(x.received_events, y.received_events);
        assert!(a.events.len() <= MAX_EVENTS);
        a.validate().unwrap();
        if noise.blocked {
            assert!(a.events.is_empty());
        }
        if noise.normalize_count.is_some() {
            assert!(x.normalized_added + x.normalized_removed > 0);
        }
    }
}
#[test]
fn hybrid_preserves_two_observable_bits_when_size_is_normalized() {
    let data = b"partial observable channel";
    let config = Config {
        codec: Codec {
            mode: Mode::Hybrid,
            ..Codec::default()
        },
        noise: Noise {
            normalize_size: Some(64),
            ..Noise::default()
        },
        ..Config::default()
    };
    let m = experiment::simulate(&config, data, &Cancellation::default()).unwrap();
    assert!(m.decoded.payload.is_none());
    let expected = codec::frame_symbols(&m.capture.plan, data).unwrap();
    let stats = statistics::evaluate(
        &expected,
        &m.decoded.symbols,
        8,
        false,
        64,
        7,
        &Cancellation::default(),
    )
    .unwrap();
    assert!((stats.capacity.lower - 2.0).abs() < 1e-10);
}
#[test]
fn normalization_and_closed_paths_never_claim_payload_delivery() {
    for mode in Mode::ALL {
        let c = Config {
            codec: Codec {
                mode,
                ..Codec::default()
            },
            noise: Noise {
                blocked: true,
                ..Noise::default()
            },
            ..Config::default()
        };
        let m = experiment::simulate(&c, &[0; 32], &Cancellation::default()).unwrap();
        assert_eq!(m.decoded.delivery, Delivery::SyncLost);
        assert!(m.report.contains("\"useful_bits_per_second\":0"));
        assert!(m
            .report
            .contains("\"dmc_fit_capacity_lower_bits_per_symbol\":0"));
    }
}
#[test]
fn hostile_capture_lengths_flags_counts_and_truncation_fail_before_allocation() {
    let token = Cancellation::default();
    let t = codec::encode(plan(Mode::Order, Fec::Hamming84, 0), &[], &token).unwrap();
    let b = trace::encode(&t).unwrap();
    for cut in 0..b.len() {
        if cut < 80 || cut % 16 == 0 {
            assert!(trace::decode(&b[..cut]).is_err());
        }
    }
    for index in [0, 8, 12, 16, 20, 24, 28, 36, 44, 60, 64, 68, 72, 89] {
        let mut bad = b.clone();
        bad[index] ^= 255;
        assert!(trace::decode(&bad).is_err(), "byte {index}");
    }
    let mut trailing = b.clone();
    trailing.push(0);
    assert!(trace::decode(&trailing).is_err());
    let mut t = t;
    t.events.reverse();
    assert!(t.validate().is_err());
}
#[test]
fn configuration_rejects_unknown_duplicates_overflow_and_live_limits() {
    for s in [
        "unknown=1",
        "mode=size\nmode=order",
        "slot_us=0",
        "slot_us=18446744073709551615",
        "fec=none\ninterleave=0",
        "normalize_kind=2",
        "normalize_count=0",
        "normalize_size=0",
        "clock_drift_ppm=-100001",
        "loss_ppm=1000001",
        "bootstrap_samples=513",
        "live_max_ms=30001",
        "burst_width_us=1",
        "large_size=64",
    ] {
        assert!(Config::parse(s).is_err(), "{s}");
    }
    let p = plan(Mode::Count, Fec::Repeat3, MAX_PAYLOAD);
    assert!(p.validate().is_err());
    let p = plan(Mode::Size, Fec::None, MAX_PAYLOAD + 1);
    assert!(p.validate().is_err());
}
#[test]
fn fitted_capacity_matches_identity_erasure_and_bsc_primary_formulas() {
    for erased in [0, 25, 50, 100] {
        let matrix = vec![vec![100 - erased, 0, erased], vec![0, 100 - erased, erased]];
        let c = statistics::capacity(&matrix).unwrap();
        assert!((c.lower - (1.0 - erased as f64 / 100.0)).abs() < 1e-10);
    }
    for error in [0, 1, 10, 25, 50] {
        let matrix = vec![vec![100 - error, error, 0], vec![error, 100 - error, 0]];
        let c = statistics::capacity(&matrix).unwrap();
        let p = error as f64 / 100.0;
        let entropy = if p == 0.0 {
            0.0
        } else {
            -p * p.log2() - (1.0 - p) * (1.0 - p).log2()
        };
        assert!((c.lower - (1.0 - entropy)).abs() < 1e-10);
        assert!(c.upper - c.lower < 1e-9);
    }
    let zero = statistics::capacity(&[vec![1, 0], vec![1, 0]]).unwrap();
    assert!(zero.lower.abs() < 1e-12 && zero.upper.abs() < 1e-12);
    assert!(
        !statistics::capacity(&[vec![0, 0], vec![1, 0]])
            .unwrap()
            .available
    );
    assert!(statistics::capacity(&[vec![1, 0], vec![1]]).is_err());
}
#[test]
fn wilson_and_block_bootstrap_handle_zero_errors_erasures_and_correlations() {
    let w = statistics::wilson(0, 100);
    assert!(w.low == 0.0 && w.high > 0.03 && w.high < 0.04);
    assert!(!statistics::wilson(1, 0).available);
    assert!(!statistics::wilson(2, 1).available);
    let expected: Vec<u8> = (0..512).map(|i| (i % 2) as u8).collect();
    let observed: Vec<Option<u8>> = expected
        .iter()
        .enumerate()
        .map(|(i, &x)| if i % 64 < 16 { None } else { Some(x) })
        .collect();
    let token = Cancellation::default();
    let a = statistics::evaluate(&expected, &observed, 2, true, 128, 42, &token).unwrap();
    let b = statistics::evaluate(&expected, &observed, 2, true, 128, 42, &token).unwrap();
    assert_eq!(a.method, "moving_block_bootstrap_95");
    assert_eq!(a.erasure95.low, b.erasure95.low);
    assert!(a.erasure95.low < 0.25 && a.erasure95.high > 0.25);
    assert_eq!(a.bit_errors, 0);
}
#[test]
fn cancellation_stops_encoding_distortion_measurement_and_physical_receiver() {
    let cancel = Cancellation::default();
    cancel.cancel();
    assert_eq!(
        codec::encode(plan(Mode::Count, Fec::None, 0), &[], &cancel)
            .unwrap_err()
            .code,
        9
    );
    let token = Cancellation::default();
    let t = codec::encode(plan(Mode::Protocol, Fec::None, 0), &[], &token).unwrap();
    assert_eq!(
        model::distort(&t, &Noise::default(), &cancel)
            .unwrap_err()
            .code,
        9
    );
    let c = Config {
        codec: Codec {
            mode: Mode::Protocol,
            slot_us: 4000,
            tolerance_us: 400,
            sync_search_us: 4000,
            ..Codec::default()
        },
        ..Config::default()
    };
    let t = codec::encode(
        Plan {
            codec: c.codec.clone(),
            ..plan(Mode::Protocol, Fec::Hamming84, 0)
        },
        &[],
        &token,
    )
    .unwrap();
    let worker_token = token.clone();
    let worker = std::thread::spawn(move || live::run(&t, &c, &worker_token));
    std::thread::sleep(std::time::Duration::from_millis(20));
    token.cancel();
    assert_eq!(worker.join().unwrap().err().unwrap().code, 9);
}
#[test]
fn actual_tcp_udp_loopback_transmits_binary_events_and_replay_decodes() {
    let config = Config {
        codec: Codec {
            mode: Mode::Protocol,
            slot_us: 4000,
            tolerance_us: 400,
            sync_search_us: 4000,
            ..Codec::default()
        },
        ..Config::default()
    };
    let data = b"A\0\xff";
    let m = experiment::loopback(&config, data, &Cancellation::default()).unwrap();
    assert_eq!(m.decoded.payload.as_deref(), Some(&data[..]));
    assert!(
        m.capture.events.iter().any(|e| e.kind == 0)
            && m.capture.events.iter().any(|e| e.kind == 1)
    );
    let replay = trace::decode(&trace::encode(&m.capture).unwrap()).unwrap();
    let d = codec::decode(&replay, &Cancellation::default()).unwrap();
    assert_eq!(d.payload.as_deref(), Some(&data[..]));
}

#[test]
fn physical_emission_bounds_fail_without_opening_sockets() {
    let token = Cancellation::default();
    let c = Config::default();
    let mut t = codec::encode(
        Plan {
            codec: c.codec.clone(),
            ..plan(Mode::Interval, Fec::Hamming84, 0)
        },
        &[],
        &token,
    )
    .unwrap();
    t.events[0].size = 65535;
    assert_eq!(live::run(&t, &c, &token).err().unwrap().code, 4);
}
