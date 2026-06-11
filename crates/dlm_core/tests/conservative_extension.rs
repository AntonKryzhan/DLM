use dlm_core::{
    audit_conservative_extension, audit_metatheory_dependencies, axiom_decl, axiom_registry,
    closed_closure_obligation, conservative_extension_audit_passport, dependency_entry_from_axiom,
    dependency_entry_from_passport, export_conservative_extension_audit_report, metatheory_closure_report,
    open_closure_obligation, preserved_theorem, require_verified_conservative_extension_audit,
    statement_passport, theorem_from_static_proof, AxiomKind, ClosureObligationKind,
    ConservativeExtensionStatus, DependencyAuditStatus, DependencyUseKind, MetatheoryClosureReport,
    MetatheoryClosureStatus, Passport, TrustLevel, TypeKind, ValidationState,
};

fn closed_closure(subject: &str) -> MetatheoryClosureReport {
    let term = Passport::proof_term("Meta", format!("{}_intro", subject.replace('.', "_")), None);
    let proof = Passport::static_proof("Meta", format!("{subject}:P"), &term);
    let entry = dependency_entry_from_passport(
        format!("proof:{subject}"),
        DependencyUseKind::Theorem,
        &proof,
        1,
    )
    .unwrap();
    let audit = audit_metatheory_dependencies(format!("{subject}.deps"), vec![entry], None, 2);
    assert_eq!(audit.status, DependencyAuditStatus::Verified);
    let obligation = closed_closure_obligation(
        format!("{subject}.soundness"),
        ClosureObligationKind::SoundnessBoundary,
        "truth/provability boundary reviewed",
        audit.audit_fingerprint.clone(),
        3,
    )
    .unwrap();
    let closure = metatheory_closure_report(subject, &audit, &[], vec![obligation], 4);
    assert_eq!(closure.status, MetatheoryClosureStatus::Closed);
    closure
}

fn open_closure(subject: &str) -> MetatheoryClosureReport {
    let base = closed_closure(subject);
    let term = Passport::proof_term("Meta", "open_intro", None);
    let proof = Passport::static_proof("Meta", format!("{subject}:open"), &term);
    let entry = dependency_entry_from_passport("proof:open", DependencyUseKind::Theorem, &proof, 1).unwrap();
    let audit = audit_metatheory_dependencies(format!("{subject}.open_deps"), vec![entry], None, 2);
    let obligation = open_closure_obligation(
        format!("{subject}.future_conservativity"),
        ClosureObligationKind::ConservativeExtension,
        "future conservative extension evidence required",
        3,
    )
    .unwrap();
    let open = metatheory_closure_report(
        subject,
        &audit,
        &[],
        vec![obligation],
        4,
    );
    assert_eq!(open.status, MetatheoryClosureStatus::Open);
    assert_ne!(open.closure_fingerprint, base.closure_fingerprint);
    open
}

fn theorem(name: &str, proposition: &str) -> Passport {
    let statement = statement_passport("Meta", proposition);
    let term = Passport::proof_term("Meta", format!("{name}_intro"), None);
    let proof = Passport::static_proof("Meta", proposition, &term);
    theorem_from_static_proof("Meta", name, &statement, &proof, 1).unwrap()
}

#[test]
fn conservative_extension_verifies_closed_extension_with_preserved_theorem() {
    let base = closed_closure("Meta.base");
    let extension = closed_closure("Meta.extension");
    let base_thm = theorem("plus_zero", "forall n:Nat. n + 0 = n");
    let ext_thm = theorem("plus_zero", "forall n:Nat. n + 0 = n");
    let preserved = preserved_theorem("plus_zero", &base_thm, &ext_thm, 10).unwrap();

    let report = audit_conservative_extension(&base, &extension, vec![preserved], vec![], 11);
    assert_eq!(report.status, ConservativeExtensionStatus::Verified);
    assert!(report.diagnostics.is_empty());
    assert!(require_verified_conservative_extension_audit(&report, 12).is_ok());

    let passport = conservative_extension_audit_passport("Meta", &report);
    assert!(matches!(passport.ty, TypeKind::ConservativeExtensionAudit { .. }));
    assert_eq!(passport.validation, ValidationState::StaticChecked);
    assert!(passport.history.contains_event("metatheory:conservative_extension"));
}

#[test]
fn theorem_preservation_rejects_name_or_proposition_mutation() {
    let base_thm = theorem("old_name", "P");
    let renamed = theorem("new_name", "P");
    let changed = theorem("old_name", "Q");

    let renamed_err = preserved_theorem("old_name", &base_thm, &renamed, 10).unwrap_err();
    assert!(renamed_err.message.contains("not preserved"));

    let changed_err = preserved_theorem("old_name", &base_thm, &changed, 11).unwrap_err();
    assert!(changed_err.message.contains("proposition changed"));
}

#[test]
fn conservative_extension_open_extension_remains_open_not_verified() {
    let base = closed_closure("Meta.base");
    let extension = open_closure("Meta.extension_open");
    let base_thm = theorem("comm", "forall a b:Nat. a + b = b + a");
    let ext_thm = theorem("comm", "forall a b:Nat. a + b = b + a");
    let preserved = preserved_theorem("comm", &base_thm, &ext_thm, 10).unwrap();

    let report = audit_conservative_extension(&base, &extension, vec![preserved], vec![], 11);
    assert_eq!(report.status, ConservativeExtensionStatus::Open);
    assert!(report.diagnostics.is_empty());
    assert!(require_verified_conservative_extension_audit(&report, 12).is_err());

    let passport = conservative_extension_audit_passport("Meta", &report);
    assert_eq!(passport.validation, ValidationState::ConstraintChecked);
}

#[test]
fn rejected_base_or_empty_preservation_rejects_conservativity() {
    let base = open_closure("Meta.open_base");
    let extension = closed_closure("Meta.extension");
    let report = audit_conservative_extension(&base, &extension, vec![], vec![], 10);

    assert_eq!(report.status, ConservativeExtensionStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("base metatheory closure")));
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("no preserved theorem evidence")));
}

#[test]
fn new_assumptions_are_visible_and_preserve_taint() {
    let base = closed_closure("Meta.base");
    let extension = closed_closure("Meta.extension");
    let base_thm = theorem("soundness_boundary", "Provable(P) != Truth(P)");
    let ext_thm = theorem("soundness_boundary", "Provable(P) != Truth(P)");
    let preserved = preserved_theorem("soundness_boundary", &base_thm, &ext_thm, 10).unwrap();

    let unsafe_axiom = axiom_decl(
        "Meta.extension",
        "external_oracle_axiom",
        "Oracle(P)",
        AxiomKind::UnsafeExternal,
        "external oracle imported explicitly",
        11,
    )
    .unwrap();
    let _registry = axiom_registry("Meta.extension", vec![unsafe_axiom.clone()], 12).unwrap();
    let assumption = dependency_entry_from_axiom(&unsafe_axiom);
    let report = audit_conservative_extension(&base, &extension, vec![preserved], vec![assumption], 13);

    assert_eq!(report.status, ConservativeExtensionStatus::Verified);
    assert!(report.has_axiom_taint);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert!(report.max_trust >= TrustLevel::Unsafe);

    let passport = conservative_extension_audit_passport("Meta", &report);
    assert_eq!(passport.provenance, dlm_core::Provenance::UnsafeExternal);
}

#[test]
fn duplicate_preservation_evidence_is_rejected() {
    let base = closed_closure("Meta.base");
    let extension = closed_closure("Meta.extension");
    let base_thm = theorem("idem", "P -> P");
    let ext_thm = theorem("idem", "P -> P");
    let first = preserved_theorem("idem", &base_thm, &ext_thm, 10).unwrap();
    let second = first.clone();

    let report = audit_conservative_extension(&base, &extension, vec![first, second], vec![], 11);
    assert_eq!(report.status, ConservativeExtensionStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("duplicate preserved theorem")));
}

#[test]
fn conservative_extension_export_is_stable_and_order_sensitive() {
    let base = closed_closure("Meta.base");
    let extension = closed_closure("Meta.extension");
    let one = preserved_theorem("one", &theorem("one", "P"), &theorem("one", "P"), 10).unwrap();
    let two = preserved_theorem("two", &theorem("two", "Q"), &theorem("two", "Q"), 11).unwrap();

    let first = audit_conservative_extension(&base, &extension, vec![one.clone(), two.clone()], vec![], 12);
    let second = audit_conservative_extension(&base, &extension, vec![two, one], vec![], 13);
    assert_ne!(first.audit_fingerprint, second.audit_fingerprint);

    let rendered = export_conservative_extension_audit_report(&first);
    assert!(rendered.contains("DLM Conservative Extension Audit v1"));
    assert!(rendered.contains("base: Meta.base"));
    assert!(rendered.contains("extension: Meta.extension"));
    assert!(rendered.contains("status: verified"));
    assert!(rendered.contains("preserved_theorems: 2"));
    assert!(rendered.contains("audit_fingerprint:"));
}
