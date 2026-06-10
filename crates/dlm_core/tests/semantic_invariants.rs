use dlm_core::{
    bridge_law, CheckPolicy, Passport, TrustLevel, BridgeDecl, BridgeKind, BridgeProfile, Capability,
};
use dlm_core::passport_rules;
use dlm_core::policy;

fn bridge(kind: BridgeKind) -> BridgeDecl {
    BridgeDecl {
        name: format!("{}_bridge", kind.as_str()),
        source: "Source".to_string(),
        target: "Target".to_string(),
        kind,
        line: 1,
    }
}

#[test]
fn trust_join_is_monotonic_and_taint_preserving() {
    assert_eq!(policy::join_trust(TrustLevel::Checked, TrustLevel::Builtin), TrustLevel::Builtin);
    assert_eq!(policy::join_trust(TrustLevel::Checked, TrustLevel::Axiom), TrustLevel::Axiom);
    assert_eq!(policy::join_trust(TrustLevel::Axiom, TrustLevel::Builtin), TrustLevel::Axiom);
    assert_eq!(policy::join_trust(TrustLevel::Checked, TrustLevel::Unsafe), TrustLevel::Unsafe);
    assert_eq!(policy::join_trust(TrustLevel::Oracle, TrustLevel::Axiom), TrustLevel::Oracle);
}

#[test]
fn check_policy_keeps_trusted_only_below_axiom() {
    assert!(policy::is_allowed_by_policy(CheckPolicy::research(), TrustLevel::Axiom));
    assert!(!policy::is_allowed_by_policy(CheckPolicy::trusted_only(), TrustLevel::Axiom));
    assert!(policy::is_allowed_by_policy(CheckPolicy::allow_unsafe(), TrustLevel::Unsafe));
}

#[test]
fn bridge_profiles_are_centralized_and_preserve_only_their_lawful_roles() {
    let quote = BridgeProfile::from_decl(&bridge(BridgeKind::Quote));
    assert!(quote.preserves_syntax);
    assert!(!quote.preserves_value);
    assert!(!quote.preserves_proof);
    assert!(!quote.preserves_truth);
    assert_eq!(quote.taint, TrustLevel::Builtin);

    let transport = BridgeProfile::from_decl(&bridge(BridgeKind::Transport));
    assert!(!transport.preserves_syntax);
    assert!(transport.preserves_value);
    assert!(!transport.preserves_proof);
    assert!(!transport.preserves_truth);

    let soundness = BridgeProfile::from_decl(&bridge(BridgeKind::Soundness));
    assert!(soundness.preserves_proof);
    assert!(soundness.preserves_truth);
    assert!(soundness.requires_axiom);
    assert_eq!(soundness.taint, TrustLevel::Axiom);

    let reflection = BridgeProfile::from_decl(&bridge(BridgeKind::Reflection));
    assert!(reflection.preserves_syntax);
    assert!(reflection.preserves_proof);
    assert!(!reflection.preserves_truth);
    assert!(reflection.requires_axiom);
    assert!(reflection.is_reflective);
    assert_eq!(reflection.taint, TrustLevel::Axiom);

    let unsafe_bridge = BridgeProfile::from_decl(&bridge(BridgeKind::Unsafe));
    assert!(unsafe_bridge.requires_axiom);
    assert_eq!(unsafe_bridge.taint, TrustLevel::Unsafe);
}

#[test]
fn bridge_law_matches_decl_profile_for_soundness_sensitive_kinds() {
    for kind in [
        BridgeKind::Quote,
        BridgeKind::Transport,
        BridgeKind::Soundness,
        BridgeKind::Reflection,
        BridgeKind::Unsafe,
    ] {
        let law = bridge_law(&kind);
        let profile = BridgeProfile::from_decl(&bridge(kind));
        assert_eq!(profile.preserves_syntax, law.preserves_syntax);
        assert_eq!(profile.preserves_value, law.preserves_value);
        assert_eq!(profile.preserves_proof, law.preserves_proof);
        assert_eq!(profile.preserves_truth, law.preserves_truth);
        assert_eq!(profile.requires_axiom, law.requires_axiom);
        assert_eq!(profile.taint, law.taint);
    }
}

#[test]
fn capability_requirement_reports_missing_capability_without_mutating_passport() {
    let value = Passport::compressed_nat("Meta");
    assert!(passport_rules::require_capability(
        &value,
        Capability::CanSymbolicPrint,
        1,
        "compressed nat should be symbolically printable",
    ).is_ok());

    let err = passport_rules::require_capability(
        &value,
        Capability::CanPrintDecimal,
        1,
        "compressed nat is intentionally not decimal-printable",
    ).expect_err("compressed Nat must not have can_print_decimal");
    assert!(err.message.contains("compressed nat is intentionally not decimal-printable"));
}

#[test]
fn history_order_is_semantic_not_a_set() {
    let lhs = Passport::literal_nat("Meta");
    let rhs = Passport::compressed_nat("Meta");
    let joined = Passport::add_result(&lhs, &rhs, "Meta");

    assert!(passport_rules::history_contains_ordered_subsequence(
        &joined,
        &["created:literal_nat", "created:compressed_nat", "derived:add"],
    ));
    assert!(!passport_rules::history_contains_ordered_subsequence(
        &joined,
        &["created:compressed_nat", "created:literal_nat", "derived:add"],
    ));
}
