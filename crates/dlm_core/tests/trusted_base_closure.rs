use dlm_core::*;

fn registry() -> AxiomRegistry {
    AxiomRegistry {
        theory: "Meta".to_string(),
        axioms: Vec::new(),
        fingerprint: "registry-fp".to_string(),
    }
}

fn dependency_audit(status: DependencyAuditStatus, trust: TrustLevel) -> MetatheoryDependencyAuditReport {
    MetatheoryDependencyAuditReport {
        subject: "Meta".to_string(),
        entries: Vec::new(),
        diagnostics: Vec::new(),
        status,
        max_trust: trust,
        has_axiom_taint: trust >= TrustLevel::Axiom,
        has_oracle_taint: trust >= TrustLevel::Oracle,
        has_unsafe_taint: trust >= TrustLevel::Unsafe,
        registry_fingerprint: Some("registry-fp".to_string()),
        audit_fingerprint: format!("dep-{status:?}-{trust:?}"),
    }
}

fn closure(status: MetatheoryClosureStatus, trust: TrustLevel) -> MetatheoryClosureReport {
    MetatheoryClosureReport {
        subject: "Meta".to_string(),
        status,
        primary_audit_fingerprint: "dep-closed".to_string(),
        supporting_audit_fingerprints: Vec::new(),
        obligations: Vec::new(),
        diagnostics: Vec::new(),
        max_trust: trust,
        has_axiom_taint: trust >= TrustLevel::Axiom,
        has_oracle_taint: trust >= TrustLevel::Oracle,
        has_unsafe_taint: trust >= TrustLevel::Unsafe,
        closure_fingerprint: format!("closure-{status:?}-{trust:?}"),
    }
}

fn inventory(status: MetatheoryInventoryStatus, trust: TrustLevel) -> GlobalMetatheoryInventoryReport {
    GlobalMetatheoryInventoryReport {
        subject: "Meta".to_string(),
        status,
        nodes: Vec::new(),
        edges: Vec::new(),
        conservative_extension_fingerprints: Vec::new(),
        diagnostics: Vec::new(),
        max_trust: trust,
        has_axiom_taint: trust >= TrustLevel::Axiom,
        has_oracle_taint: trust >= TrustLevel::Oracle,
        has_unsafe_taint: trust >= TrustLevel::Unsafe,
        inventory_fingerprint: format!("inventory-{status:?}-{trust:?}"),
    }
}

fn ledger(status: SoundnessBoundaryStatus, trust: TrustLevel) -> SoundnessBoundaryLedgerReport {
    SoundnessBoundaryLedgerReport {
        subject: "Meta".to_string(),
        status,
        assumptions: Vec::new(),
        global_inventory_fingerprint: Some("inventory-fp".to_string()),
        diagnostics: Vec::new(),
        max_trust: trust,
        has_axiom_taint: trust >= TrustLevel::Axiom,
        has_oracle_taint: trust >= TrustLevel::Oracle,
        has_unsafe_taint: trust >= TrustLevel::Unsafe,
        ledger_fingerprint: format!("ledger-{status:?}-{trust:?}"),
    }
}

fn closed_evidence() -> Vec<TrustedBaseEvidence> {
    vec![
        trusted_base_evidence_from_axiom_registry("registry", &registry(), 1).unwrap(),
        trusted_base_evidence_from_dependency_audit("deps", &dependency_audit(DependencyAuditStatus::Verified, TrustLevel::Axiom), 1).unwrap(),
        trusted_base_evidence_from_metatheory_closure("closure", &closure(MetatheoryClosureStatus::Closed, TrustLevel::Axiom), 1).unwrap(),
        trusted_base_evidence_from_global_inventory("inventory", &inventory(MetatheoryInventoryStatus::Verified, TrustLevel::Axiom), 1).unwrap(),
        trusted_base_evidence_from_soundness_boundary_ledger("ledger", &ledger(SoundnessBoundaryStatus::Verified, TrustLevel::Axiom), 1).unwrap(),
    ]
}

#[test]
fn trusted_base_closure_closes_complete_verified_metatheory_gate() {
    let report = trusted_base_closure("Meta", closed_evidence(), 1);
    assert_eq!(report.status, TrustedBaseClosureStatus::Closed);
    assert!(report.diagnostics.is_empty());
    assert!(report.has_axiom_taint);
    assert_eq!(report.max_trust, TrustLevel::Axiom);
    require_closed_trusted_base_closure(&report, 1).unwrap();

    let passport = trusted_base_closure_passport("Meta", &report);
    match passport.ty {
        TypeKind::TrustedBaseClosure { subject, status } => {
            assert_eq!(subject, "Meta");
            assert_eq!(status, "closed");
        }
        other => panic!("unexpected trusted-base passport type: {other}"),
    }
    assert_eq!(passport.trust, TrustLevel::Axiom);
    assert_eq!(passport.validation, ValidationState::StaticChecked);
}

#[test]
fn open_supporting_evidence_keeps_trusted_base_open_not_closed() {
    let mut evidence = closed_evidence();
    evidence[2] = trusted_base_evidence_from_metatheory_closure(
        "closure",
        &closure(MetatheoryClosureStatus::Open, TrustLevel::Axiom),
        1,
    )
    .unwrap();
    let report = trusted_base_closure("Meta", evidence, 1);
    assert_eq!(report.status, TrustedBaseClosureStatus::Open);
    assert!(report.diagnostics.is_empty());
    assert!(require_closed_trusted_base_closure(&report, 1).is_err());
}

#[test]
fn rejected_evidence_rejects_final_metatheory_gate() {
    let mut evidence = closed_evidence();
    evidence[1] = trusted_base_evidence_from_dependency_audit(
        "deps",
        &dependency_audit(DependencyAuditStatus::Rejected, TrustLevel::Axiom),
        1,
    )
    .unwrap();
    let report = trusted_base_closure("Meta", evidence, 1);
    assert_eq!(report.status, TrustedBaseClosureStatus::Rejected);
    assert!(report.diagnostics.iter().any(|d| d.message.contains("rejected")));
}

#[test]
fn missing_required_evidence_kind_is_rejected() {
    let mut evidence = closed_evidence();
    evidence.retain(|item| item.kind != TrustedBaseEvidenceKind::SoundnessBoundaryLedger);
    let report = trusted_base_closure("Meta", evidence, 1);
    assert_eq!(report.status, TrustedBaseClosureStatus::Rejected);
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.message.contains("soundness_boundary_ledger")));
}

#[test]
fn duplicate_evidence_id_or_fingerprint_is_rejected() {
    let mut evidence = closed_evidence();
    evidence.push(evidence[0].clone());
    let report = trusted_base_closure("Meta", evidence, 1);
    assert_eq!(report.status, TrustedBaseClosureStatus::Rejected);
    assert!(report.diagnostics.iter().any(|d| d.message.contains("duplicate")));
}

#[test]
fn trusted_base_export_is_stable_order_sensitive_and_taint_preserving() {
    let report = trusted_base_closure("Meta", closed_evidence(), 1);
    let exported = export_trusted_base_closure(&report);
    assert!(exported.contains("DLM Trusted Base Closure v1"));
    assert!(exported.contains("status: closed"));
    assert!(exported.contains("has_axiom_taint: true"));

    let mut reversed = closed_evidence();
    reversed.reverse();
    let reversed_report = trusted_base_closure("Meta", reversed, 1);
    assert_ne!(report.closure_fingerprint, reversed_report.closure_fingerprint);
}

#[test]
fn trusted_base_evidence_from_passport_accepts_only_foundation_artifacts() {
    let report = trusted_base_closure("Meta", closed_evidence(), 1);
    let passport = trusted_base_closure_passport("Meta", &report);
    let err = trusted_base_evidence_from_passport("final_gate", &passport, 1).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TrustedBaseError);

    let raw_nat = Passport::literal_nat("Core");
    let err = trusted_base_evidence_from_passport("not_foundation", &raw_nat, 1).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TrustedBaseError);
}
