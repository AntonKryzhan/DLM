use dlm_core::*;

fn soundness_profile() -> BridgeProfile {
    BridgeProfile {
        name: "Meta_soundness".to_string(),
        source: "Meta".to_string(),
        target: "Object".to_string(),
        kind: "soundness".to_string(),
        preserves_syntax: false,
        preserves_value: false,
        preserves_proof: true,
        preserves_truth: true,
        requires_axiom: true,
        is_conservative: false,
        is_reflective: false,
        is_reversible: false,
        taint: TrustLevel::Axiom,
        role: "axiom-tainted truth bridge for tests",
    }
}

fn quote_profile() -> BridgeProfile {
    BridgeProfile {
        name: "Meta_quote".to_string(),
        source: "Object".to_string(),
        target: "Meta".to_string(),
        kind: "quote".to_string(),
        preserves_syntax: true,
        preserves_value: false,
        preserves_proof: false,
        preserves_truth: false,
        requires_axiom: false,
        is_conservative: false,
        is_reflective: false,
        is_reversible: false,
        taint: TrustLevel::Builtin,
        role: "syntax-only bridge for tests",
    }
}

#[test]
fn soundness_bridge_assumption_is_recorded_and_axiom_tainted() {
    let entry = boundary_assumption_from_bridge_profile("b.soundness", &soundness_profile(), 10)
        .expect("soundness bridge must become boundary ledger entry");
    assert_eq!(entry.kind, BoundaryAssumptionKind::SoundnessBridge);
    assert_eq!(entry.trust, TrustLevel::Axiom);
    assert!(entry.requires_axiom);
    assert!(entry.preserves_proof);
    assert!(entry.preserves_truth);

    let report = soundness_boundary_ledger("Meta", vec![entry], None, 10);
    assert_eq!(report.status, SoundnessBoundaryStatus::Verified);
    assert!(report.has_axiom_taint);
    assert!(require_verified_soundness_boundary_ledger(&report, 10).is_ok());

    let passport = soundness_boundary_ledger_passport("Meta", &report);
    match passport.ty {
        TypeKind::SoundnessBoundaryLedger { ref subject, ref status } => {
            assert_eq!(subject, "Meta");
            assert_eq!(status, "verified");
        }
        other => panic!("unexpected passport type: {other:?}"),
    }
    assert_eq!(passport.trust, TrustLevel::Axiom);
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
}

#[test]
fn safe_quote_bridge_is_not_a_boundary_assumption() {
    let err = boundary_assumption_from_bridge_profile("b.quote", &quote_profile(), 20)
        .expect_err("quote bridge must not be recorded as a soundness assumption");
    assert_eq!(err.kind, DiagnosticKind::SoundnessBoundaryError);
}

#[test]
fn reflection_consistency_and_truth_lifts_are_explicit_passport_boundaries() {
    let term = Passport::proof_term("Meta", "true_intro", None);
    let checked = Passport::kernel_checked_proof("Meta", "true_intro", &term);
    let truth = Passport::axiom_truth_from_provable("Meta", "true_intro", &checked);
    let reflection = Passport::axiom_reflection_proof("Meta", "Meta", "true_intro", &checked);
    let consistency_claim = Passport::consistency_claim("Meta", "Meta", None);
    let consistency = Passport::axiom_consistency_proof("Meta", "Meta", &consistency_claim);

    let truth_entry = boundary_assumption_from_passport(
        "truth.lift",
        BoundaryAssumptionKind::TruthLift,
        &truth,
        30,
    )
    .expect("truth lift must be accepted");
    let reflection_entry = boundary_assumption_from_passport(
        "reflection.axiom",
        BoundaryAssumptionKind::ReflectionBridge,
        &reflection,
        30,
    )
    .expect("reflection axiom must be accepted");
    let consistency_entry = boundary_assumption_from_passport(
        "consistency.axiom",
        BoundaryAssumptionKind::ConsistencyAssumption,
        &consistency,
        30,
    )
    .expect("consistency axiom must be accepted");

    let report = soundness_boundary_ledger(
        "Meta",
        vec![truth_entry, reflection_entry, consistency_entry],
        None,
        30,
    );
    assert_eq!(report.status, SoundnessBoundaryStatus::Verified);
    assert_eq!(report.assumptions.len(), 3);
    assert_eq!(report.max_trust, TrustLevel::Axiom);
    assert!(report.has_axiom_taint);
}

#[test]
fn checked_static_proof_cannot_be_smuggled_as_boundary_assumption() {
    let term = Passport::proof_term("Meta", "true_intro", None);
    let checked = Passport::kernel_checked_proof("Meta", "true_intro", &term);
    let err = boundary_assumption_from_passport(
        "bad.checked",
        BoundaryAssumptionKind::AxiomDependency,
        &checked,
        40,
    )
    .expect_err("checked proof below Axiom cannot be used as boundary assumption");
    assert_eq!(err.kind, DiagnosticKind::SoundnessBoundaryError);
}

#[test]
fn duplicate_boundary_assumptions_reject_the_ledger() {
    let entry = boundary_assumption_from_bridge_profile("b.soundness", &soundness_profile(), 50)
        .expect("soundness bridge entry");
    let report = soundness_boundary_ledger("Meta", vec![entry.clone(), entry], None, 50);
    assert_eq!(report.status, SoundnessBoundaryStatus::Rejected);
    assert!(report.diagnostics.iter().any(|d| d.message.contains("duplicate boundary assumption id")));
    assert!(require_verified_soundness_boundary_ledger(&report, 50).is_err());
}

#[test]
fn open_or_rejected_global_inventory_affects_boundary_ledger_status() {
    let entry = boundary_assumption_from_bridge_profile("b.soundness", &soundness_profile(), 60)
        .expect("soundness bridge entry");

    let unknown = theorem_dependency_node_from_passport(
        "unknown.axiom",
        TheoremDependencyNodeKind::Unknown,
        &Passport::axiom_truth_from_provable(
            "Meta",
            "P",
            &Passport::kernel_checked_proof(
                "Meta",
                "P",
                &Passport::proof_term("Meta", "p_intro", None),
            ),
        ),
        60,
    )
    .expect("unknown node is allowed but leaves inventory open");
    let open_inventory = global_metatheory_inventory("Meta", vec![unknown], vec![], &[], 60);
    assert_eq!(open_inventory.status, MetatheoryInventoryStatus::Verified);

    let open_report = soundness_boundary_ledger("Meta", vec![entry.clone()], Some(&open_inventory), 60);
    assert_eq!(open_report.status, SoundnessBoundaryStatus::Verified);
    assert!(open_report.global_inventory_fingerprint.is_some());

    let rejected_inventory = global_metatheory_inventory("", vec![], vec![], &[], 60);
    assert_eq!(rejected_inventory.status, MetatheoryInventoryStatus::Rejected);
    let rejected_report = soundness_boundary_ledger("Meta", vec![entry], Some(&rejected_inventory), 60);
    assert_eq!(rejected_report.status, SoundnessBoundaryStatus::Rejected);
}

#[test]
fn ledger_export_is_stable_and_order_sensitive() {
    let soundness = boundary_assumption_from_bridge_profile("b.soundness", &soundness_profile(), 70)
        .expect("soundness bridge entry");
    let truth = Passport::axiom_truth_from_provable(
        "Meta",
        "P",
        &Passport::kernel_checked_proof("Meta", "P", &Passport::proof_term("Meta", "p_intro", None)),
    );
    let truth_entry = boundary_assumption_from_passport(
        "truth.P",
        BoundaryAssumptionKind::TruthLift,
        &truth,
        70,
    )
    .expect("truth boundary entry");

    let a = soundness_boundary_ledger("Meta", vec![soundness.clone(), truth_entry.clone()], None, 70);
    let b = soundness_boundary_ledger("Meta", vec![soundness, truth_entry], None, 70);
    assert_eq!(a.ledger_fingerprint, b.ledger_fingerprint);
    let exported = export_soundness_boundary_ledger(&a);
    assert!(exported.contains("DLM Soundness Boundary Ledger v1"));
    assert!(exported.contains("status: verified"));
    assert!(exported.contains("b.soundness"));
    assert!(exported.contains("truth.P"));
}
