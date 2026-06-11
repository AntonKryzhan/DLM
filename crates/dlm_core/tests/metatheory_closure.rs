use dlm_core::{
    audit_metatheory_dependencies, axiom_decl, axiom_registry, closed_closure_obligation,
    dependency_entry_from_axiom, dependency_entry_from_passport, export_metatheory_closure_report,
    metatheory_closure_report, metatheory_closure_report_passport, open_closure_obligation,
    require_closed_metatheory_closure, AxiomKind, ClosureObligationKind, DependencyAuditStatus,
    DependencyUseKind, MetatheoryClosureStatus, Passport, TrustLevel, TypeKind,
};

fn verified_clean_audit() -> dlm_core::MetatheoryDependencyAuditReport {
    let term = Passport::proof_term("Meta", "p_intro", None);
    let proof = Passport::static_proof("Meta", "P", &term);
    let entry = dependency_entry_from_passport("proof:P", DependencyUseKind::Theorem, &proof, 1).unwrap();
    let report = audit_metatheory_dependencies("Meta.clean", vec![entry], None, 2);
    assert_eq!(report.status, DependencyAuditStatus::Verified);
    report
}

#[test]
fn closure_report_closes_verified_dependency_audit_with_closed_obligations() {
    let audit = verified_clean_audit();
    let obligation = closed_closure_obligation(
        "truth-boundary-reviewed",
        ClosureObligationKind::SoundnessBoundary,
        "Provable(P) is not used as Truth(P)",
        audit.audit_fingerprint.clone(),
        3,
    )
    .unwrap();

    let report = metatheory_closure_report("Meta.clean_closure", &audit, &[], vec![obligation], 4);
    assert_eq!(report.status, MetatheoryClosureStatus::Closed);
    assert!(!report.has_axiom_taint);
    assert!(require_closed_metatheory_closure(&report, 5).is_ok());

    let passport = metatheory_closure_report_passport("Meta", &report);
    assert!(matches!(passport.ty, TypeKind::MetatheoryClosureReport { .. }));
    assert_eq!(passport.validation, dlm_core::ValidationState::StaticChecked);
    assert!(passport.history.contains_event("metatheory:closure_report"));
}

#[test]
fn open_obligation_keeps_closure_open_without_rejecting_the_evidence() {
    let audit = verified_clean_audit();
    let obligation = open_closure_obligation(
        "conservative-extension-not-reviewed",
        ClosureObligationKind::ConservativeExtension,
        "future extension must show no old theorem is invalidated",
        3,
    )
    .unwrap();

    let report = metatheory_closure_report("Meta.open", &audit, &[], vec![obligation], 4);
    assert_eq!(report.status, MetatheoryClosureStatus::Open);
    assert!(report.diagnostics.is_empty());
    assert!(require_closed_metatheory_closure(&report, 5).is_err());

    let passport = metatheory_closure_report_passport("Meta", &report);
    assert_eq!(passport.validation, dlm_core::ValidationState::ConstraintChecked);
}

#[test]
fn rejected_dependency_audit_rejects_the_closure_report() {
    let ax = axiom_decl("Meta", "sound", "Provable(P)->Truth(P)", AxiomKind::Soundness, "explicit soundness", 1).unwrap();
    let entry = dependency_entry_from_axiom(&ax);
    let rejected_audit = audit_metatheory_dependencies("Meta.rejected", vec![entry], None, 2);
    assert_eq!(rejected_audit.status, DependencyAuditStatus::Rejected);

    let report = metatheory_closure_report("Meta.bad", &rejected_audit, &[], vec![], 3);
    assert_eq!(report.status, MetatheoryClosureStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("primary dependency audit")));
    assert!(require_closed_metatheory_closure(&report, 4).is_err());
}

#[test]
fn supporting_audits_preserve_order_and_taint() {
    let clean = verified_clean_audit();
    let ax = axiom_decl("Meta", "reflect", "Reflect(P)", AxiomKind::Reflection, "explicit reflection", 1).unwrap();
    let registry = axiom_registry("Meta", vec![ax.clone()], 2).unwrap();
    let axiom_entry = dependency_entry_from_axiom(&ax);
    let axiom_audit = audit_metatheory_dependencies("Meta.reflective", vec![axiom_entry], Some(&registry), 3);
    assert_eq!(axiom_audit.status, DependencyAuditStatus::Verified);

    let first = metatheory_closure_report("Meta.support", &clean, &[axiom_audit.clone()], vec![], 4);
    let second = metatheory_closure_report("Meta.support", &axiom_audit, &[clean.clone()], vec![], 5);

    assert_eq!(first.status, MetatheoryClosureStatus::Closed);
    assert!(first.has_axiom_taint);
    assert!(first.max_trust >= TrustLevel::Axiom);
    assert_ne!(first.closure_fingerprint, second.closure_fingerprint);
}

#[test]
fn duplicate_audit_fingerprint_is_rejected() {
    let audit = verified_clean_audit();
    let report = metatheory_closure_report("Meta.dup", &audit, &[audit.clone()], vec![], 2);
    assert_eq!(report.status, MetatheoryClosureStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("duplicate audit fingerprint")));
}

#[test]
fn closure_export_is_stable_and_contains_required_fields() {
    let audit = verified_clean_audit();
    let report = metatheory_closure_report("Meta.export", &audit, &[], vec![], 2);
    let exported = export_metatheory_closure_report(&report);

    assert!(exported.contains("DLM Metatheory Closure Report v1"));
    assert!(exported.contains("subject: Meta.export"));
    assert!(exported.contains("status: closed"));
    assert!(exported.contains("primary_audit_fingerprint:"));
    assert!(exported.contains("closure_fingerprint:"));
}
