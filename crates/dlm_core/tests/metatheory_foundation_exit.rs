use dlm_core::*;

fn criterion(kind: MetatheoryExitCriterionKind) -> MetatheoryExitCriterion {
    metatheory_exit_criterion(
        kind.to_string(),
        kind,
        "YARD.Meta",
        MetatheoryExitCriterionStatus::Satisfied,
        format!("evidence:{kind}"),
        TrustLevel::Checked,
        Provenance::InternalDerived,
        ValidationState::StaticChecked,
        vec![format!("criterion:{kind}:checked")],
        1,
    )
    .unwrap()
}

fn all_required_criteria() -> Vec<MetatheoryExitCriterion> {
    required_metatheory_exit_criteria()
        .into_iter()
        .map(criterion)
        .collect()
}

#[test]
fn complete_metatheory_foundation_exit_is_ready_not_truth_or_theorem() {
    let report = metatheory_foundation_exit_report("YARD.Meta", all_required_criteria(), 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Ready);
    require_metatheory_foundation_ready(&report, 1).unwrap();

    let passport = metatheory_foundation_exit_passport("Meta", &report);
    assert!(matches!(
        passport.ty,
        TypeKind::MetatheoryFoundationExit { ref subject, ref status }
            if subject == "YARD.Meta" && status == "ready"
    ));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(passport.ty, TypeKind::TruthClaim { .. }));
    assert_eq!(passport.validation, ValidationState::StaticChecked);
}

#[test]
fn missing_required_criterion_keeps_exit_incomplete() {
    let mut criteria = all_required_criteria();
    criteria.retain(|item| item.kind != MetatheoryExitCriterionKind::ModuleBoundary);
    let report = metatheory_foundation_exit_report("YARD.Meta", criteria, 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Incomplete);
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.message.contains("module_boundary")));
    assert!(require_metatheory_foundation_ready(&report, 1).is_err());
}

#[test]
fn failed_criterion_rejects_exit_report() {
    let mut criteria = all_required_criteria();
    let failed = metatheory_exit_criterion(
        "failed_truth_boundary",
        MetatheoryExitCriterionKind::TruthProvabilityBoundary,
        "YARD.Meta",
        MetatheoryExitCriterionStatus::Failed,
        "truth/provability regression failed",
        TrustLevel::Axiom,
        Provenance::BuiltinKnown,
        ValidationState::Raw,
        vec!["criterion:truth_boundary:failed".to_string()],
        1,
    )
    .unwrap();
    criteria.retain(|item| item.kind != MetatheoryExitCriterionKind::TruthProvabilityBoundary);
    criteria.push(failed);
    let report = metatheory_foundation_exit_report("YARD.Meta", criteria, 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Rejected);
    assert!(report.has_axiom_taint);
}

#[test]
fn open_trusted_base_keeps_foundation_incomplete() {
    let mut criteria = all_required_criteria();
    let open = metatheory_exit_criterion(
        "trusted_base_open",
        MetatheoryExitCriterionKind::TrustedBaseClosure,
        "YARD.Meta",
        MetatheoryExitCriterionStatus::Open,
        "trusted base closure still open",
        TrustLevel::Checked,
        Provenance::InternalDerived,
        ValidationState::ConstraintChecked,
        vec!["trusted_base:open".to_string()],
        1,
    )
    .unwrap();
    criteria.retain(|item| item.kind != MetatheoryExitCriterionKind::TrustedBaseClosure);
    criteria.push(open);
    let report = metatheory_foundation_exit_report("YARD.Meta", criteria, 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Incomplete);
}

#[test]
fn duplicate_ids_or_fingerprints_reject_exit_report() {
    let mut criteria = all_required_criteria();
    let duplicate = criteria[0].clone();
    criteria.push(duplicate);
    let report = metatheory_foundation_exit_report("YARD.Meta", criteria, 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Rejected);
    assert!(report.diagnostics.iter().any(|d| d.message.contains("duplicate")));
}

#[test]
fn foundation_exit_preserves_oracle_and_unsafe_taint() {
    let mut criteria = all_required_criteria();
    let oracle = metatheory_exit_criterion(
        "soundness_oracle",
        MetatheoryExitCriterionKind::SoundnessBoundaryLedger,
        "YARD.Meta",
        MetatheoryExitCriterionStatus::Satisfied,
        "oracle boundary accounted",
        TrustLevel::Oracle,
        Provenance::OracleInput,
        ValidationState::StaticChecked,
        vec!["soundness_boundary:oracle".to_string()],
        1,
    )
    .unwrap();
    let unsafe_item = metatheory_exit_criterion(
        "regression_unsafe",
        MetatheoryExitCriterionKind::RegressionCoverage,
        "YARD.Meta",
        MetatheoryExitCriterionStatus::Satisfied,
        "unsafe regression boundary accounted",
        TrustLevel::Unsafe,
        Provenance::UnsafeExternal,
        ValidationState::StaticChecked,
        vec!["regression:unsafe".to_string()],
        1,
    )
    .unwrap();
    criteria.retain(|item| item.kind != MetatheoryExitCriterionKind::SoundnessBoundaryLedger);
    criteria.retain(|item| item.kind != MetatheoryExitCriterionKind::RegressionCoverage);
    criteria.push(oracle);
    criteria.push(unsafe_item);

    let report = metatheory_foundation_exit_report("YARD.Meta", criteria, 1);
    assert_eq!(report.status, MetatheoryFoundationStatus::Ready);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert_eq!(report.max_trust, TrustLevel::Unsafe);
    let passport = metatheory_foundation_exit_passport("Meta", &report);
    assert_eq!(passport.trust, TrustLevel::Unsafe);
    assert_eq!(passport.provenance, Provenance::UnsafeExternal);
}

#[test]
fn trusted_base_report_becomes_exit_criterion() {
    let report = TrustedBaseClosureReport {
        subject: "YARD.Meta".to_string(),
        status: TrustedBaseClosureStatus::Closed,
        evidence: vec![],
        diagnostics: vec![],
        max_trust: TrustLevel::Axiom,
        has_axiom_taint: true,
        has_oracle_taint: false,
        has_unsafe_taint: false,
        closure_fingerprint: "tb-fingerprint".to_string(),
    };
    let criterion = metatheory_exit_criterion_from_trusted_base_report("tb", &report, 1).unwrap();
    assert_eq!(criterion.kind, MetatheoryExitCriterionKind::TrustedBaseClosure);
    assert_eq!(criterion.status, MetatheoryExitCriterionStatus::Satisfied);
    assert!(criterion.has_axiom_taint);
}

#[test]
fn foundation_exit_export_is_stable_and_order_sensitive() {
    let report_a = metatheory_foundation_exit_report("YARD.Meta", all_required_criteria(), 1);
    let mut reversed = all_required_criteria();
    reversed.reverse();
    let report_b = metatheory_foundation_exit_report("YARD.Meta", reversed, 1);

    let text = export_metatheory_foundation_exit_report(&report_a);
    assert!(text.contains("DLM Metatheory Foundation Exit Report v1"));
    assert!(text.contains("status: ready"));
    assert!(text.contains("exit_fingerprint:"));
    assert_ne!(report_a.exit_fingerprint, report_b.exit_fingerprint);
}
