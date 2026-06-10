use dlm_core::{
    bridge_law, passport_rules, policy, BridgeDecl, BridgeKind, BridgeProfile, CheckPolicy,
    Passport, TrustLevel,
};

fn trust_levels() -> [TrustLevel; 5] {
    [
        TrustLevel::Checked,
        TrustLevel::Builtin,
        TrustLevel::Axiom,
        TrustLevel::Oracle,
        TrustLevel::Unsafe,
    ]
}

fn bridge_kinds() -> Vec<BridgeKind> {
    vec![
        BridgeKind::Definitional,
        BridgeKind::Conservative,
        BridgeKind::Quote,
        BridgeKind::Transport,
        BridgeKind::Soundness,
        BridgeKind::Reflection,
        BridgeKind::Migration,
        BridgeKind::Materialize,
        BridgeKind::Unsafe,
        BridgeKind::Unknown("experimental".to_string()),
    ]
}

fn bridge(kind: BridgeKind) -> BridgeDecl {
    let kind_name = kind.as_str().to_string();
    BridgeDecl {
        name: format!("{kind_name}_bridge"),
        source: "Source".to_string(),
        target: "Target".to_string(),
        kind,
        line: 1,
    }
}

fn with_trust(mut passport: Passport, trust: TrustLevel) -> Passport {
    passport.trust = trust;
    passport
}

#[test]
fn generated_trust_join_obeys_semilattice_laws() {
    let levels = trust_levels();

    for a in levels {
        assert_eq!(policy::join_trust(a, a), a, "join must be idempotent for {a:?}");
        assert_eq!(policy::join_trust(TrustLevel::Checked, a), a);
        assert_eq!(policy::join_trust(a, TrustLevel::Checked), a);

        for b in levels {
            let ab = policy::join_trust(a, b);
            let ba = policy::join_trust(b, a);
            assert_eq!(ab, ba, "join must be commutative for {a:?}, {b:?}");
            assert!(ab >= a, "join must not lower lhs taint: {a:?}, {b:?}");
            assert!(ab >= b, "join must not lower rhs taint: {a:?}, {b:?}");

            for c in levels {
                let left = policy::join_trust(policy::join_trust(a, b), c);
                let right = policy::join_trust(a, policy::join_trust(b, c));
                assert_eq!(left, right, "join must be associative for {a:?}, {b:?}, {c:?}");
                assert_eq!(left, policy::join_many_trust([a, b, c]));
            }
        }
    }
}

#[test]
fn generated_policy_thresholds_are_prefix_closed() {
    let policies = [
        CheckPolicy::trusted_only(),
        CheckPolicy::research(),
        CheckPolicy::allow_unsafe(),
    ];

    for policy in policies {
        for high in trust_levels() {
            for low in trust_levels() {
                if low <= high && policy::is_allowed_by_policy(policy, high) {
                    assert!(
                        policy::is_allowed_by_policy(policy, low),
                        "policy {policy:?} accepts {high:?} but rejects lower trust {low:?}"
                    );
                }
            }
        }
    }

    assert!(!policy::is_allowed_by_policy(CheckPolicy::trusted_only(), TrustLevel::Axiom));
    assert!(policy::is_allowed_by_policy(CheckPolicy::research(), TrustLevel::Axiom));
    assert!(!policy::is_allowed_by_policy(CheckPolicy::research(), TrustLevel::Unsafe));
    assert!(policy::is_allowed_by_policy(CheckPolicy::allow_unsafe(), TrustLevel::Unsafe));
}

#[test]
fn generated_bridge_profiles_match_central_laws() {
    for kind in bridge_kinds() {
        let law = bridge_law(&kind);
        let profile = BridgeProfile::from_decl(&bridge(kind.clone()));

        assert_eq!(profile.preserves_syntax, law.preserves_syntax);
        assert_eq!(profile.preserves_value, law.preserves_value);
        assert_eq!(profile.preserves_proof, law.preserves_proof);
        assert_eq!(profile.preserves_truth, law.preserves_truth);
        assert_eq!(profile.requires_axiom, law.requires_axiom);
        assert_eq!(profile.is_conservative, law.is_conservative);
        assert_eq!(profile.is_reflective, law.is_reflective);
        assert_eq!(profile.is_reversible, law.is_reversible);
        assert_eq!(profile.taint, law.taint);

        if profile.preserves_truth {
            assert!(
                profile.preserves_proof,
                "truth-preserving bridges must also preserve proof evidence: {kind:?}"
            );
        }

        if profile.requires_axiom {
            assert!(
                profile.taint >= TrustLevel::Axiom,
                "axiom-requiring bridges must be at least Axiom-tainted: {kind:?}"
            );
        }

        if profile.is_reflective {
            assert!(profile.requires_axiom);
            assert!(!profile.preserves_truth);
        }
    }
}

#[test]
fn generated_bridge_kind_specific_boundaries_remain_stable() {
    let quote = BridgeProfile::from_decl(&bridge(BridgeKind::Quote));
    assert!(quote.preserves_syntax);
    assert!(!quote.preserves_value);
    assert!(!quote.preserves_proof);
    assert!(!quote.preserves_truth);

    for kind in [BridgeKind::Transport, BridgeKind::Migration, BridgeKind::Materialize] {
        let profile = BridgeProfile::from_decl(&bridge(kind.clone()));
        assert!(profile.preserves_value, "{kind:?} must preserve value role");
        assert!(!profile.preserves_proof, "{kind:?} must not preserve proof by default");
        assert!(!profile.preserves_truth, "{kind:?} must not preserve truth by default");
    }

    let soundness = BridgeProfile::from_decl(&bridge(BridgeKind::Soundness));
    assert!(soundness.preserves_proof);
    assert!(soundness.preserves_truth);
    assert!(soundness.requires_axiom);
    assert_eq!(soundness.taint, TrustLevel::Axiom);

    let unsafe_bridge = BridgeProfile::from_decl(&bridge(BridgeKind::Unsafe));
    assert!(unsafe_bridge.requires_axiom);
    assert_eq!(unsafe_bridge.taint, TrustLevel::Unsafe);

    let unknown = BridgeProfile::from_decl(&bridge(BridgeKind::Unknown("future".to_string())));
    assert!(!unknown.preserves_syntax);
    assert!(!unknown.preserves_value);
    assert!(!unknown.preserves_proof);
    assert!(!unknown.preserves_truth);
    assert_eq!(unknown.taint, TrustLevel::Unsafe);
}

#[test]
fn generated_binary_passport_derivations_never_lower_trust() {
    for lhs_trust in trust_levels() {
        for rhs_trust in trust_levels() {
            let lhs = with_trust(Passport::literal_nat("Meta"), lhs_trust);
            let rhs = with_trust(Passport::compressed_nat("Meta"), rhs_trust);
            let sum = Passport::add_result(&lhs, &rhs, "Meta");

            assert_eq!(sum.trust, lhs_trust.max(rhs_trust));
            assert!(sum.trust >= lhs_trust);
            assert!(sum.trust >= rhs_trust);

            assert!(passport_rules::history_contains_ordered_subsequence(
                &sum,
                &["created:literal_nat", "created:compressed_nat", "derived:add"],
            ));
        }
    }
}

#[test]
fn generated_source_based_passport_derivations_preserve_or_raise_trust() {
    for trust in trust_levels() {
        let universe = with_trust(Passport::universe("Meta", 0), trust);
        let set = Passport::set_of_universe(&universe, "Meta");
        let class = Passport::class_of_universe(&universe, "Meta");
        let class_infinity = Passport::class_infinity("Meta", &class);
        let universe_infinity = Passport::universe_infinity("Meta", &universe);
        let soundness = Passport::soundness_proof("Meta", "phi", &universe, "soundness_bridge");

        for derived in [&set, &class, &class_infinity, &universe_infinity] {
            assert!(
                derived.trust >= trust,
                "source-derived passport lowered trust from {trust:?} to {:?}",
                derived.trust,
            );
        }

        assert!(soundness.trust >= trust);
        assert!(soundness.trust >= TrustLevel::Axiom);
        assert!(soundness.history.contains_event("axiom:soundness_assumption"));
    }
}

#[test]
fn generated_history_subsequence_checks_preserve_order_and_multiplicity() {
    let mut first = Passport::literal_nat("Meta");
    first.history.push("repeat:resource");
    let mut second = Passport::literal_nat("Meta");
    second.history.push("repeat:resource");

    let joined = Passport::add_result(&first, &second, "Meta");
    let repeated_count = joined
        .history
        .events()
        .iter()
        .filter(|event| event.as_str() == "repeat:resource")
        .count();

    assert_eq!(repeated_count, 2, "history is a chain, not a set");
    assert!(passport_rules::history_contains_ordered_subsequence(
        &joined,
        &["repeat:resource", "repeat:resource", "derived:add"],
    ));
    assert!(!passport_rules::history_contains_ordered_subsequence(
        &joined,
        &["derived:add", "repeat:resource"],
    ));
}
