use dlm_core::{
    audit_metatheory_dependencies, axiom_decl, axiom_registry, axiom_registry_passport,
    dependency_entry_from_axiom, dependency_entry_from_passport, metatheory_dependency_audit_passport,
    render_metatheory_dependency_audit_report, require_declared_axiom,
    require_verified_metatheory_dependency_audit, AxiomKind, DependencyAuditStatus,
    DependencyUseKind, Passport, TrustLevel, TypeKind,
};

#[test]
fn axiom_registry_rejects_duplicate_or_cross_theory_axioms() {
    let ax = axiom_decl("Meta", "sound", "Provable(P)->Truth(P)", AxiomKind::Soundness, "explicit soundness assumption", 1).unwrap();
    let duplicate = ax.clone();
    let err = axiom_registry("Meta", vec![ax.clone(), duplicate], 2).unwrap_err();
    assert_eq!(err.kind, dlm_core::DiagnosticKind::MetatheoryDependencyError);

    let foreign = axiom_decl("Other", "foreign", "Q", AxiomKind::Domain, "foreign domain axiom", 3).unwrap();
    let err = axiom_registry("Meta", vec![ax, foreign], 4).unwrap_err();
    assert_eq!(err.kind, dlm_core::DiagnosticKind::MetatheoryDependencyError);
}

#[test]
fn axiom_registry_is_audit_contract_not_theorem_or_proof() {
    let ax = axiom_decl("Meta", "reflect", "Reflect(P)", AxiomKind::Reflection, "explicit reflection", 1).unwrap();
    let registry = axiom_registry("Meta", vec![ax], 2).unwrap();
    let passport = axiom_registry_passport("Meta", &registry);

    assert!(matches!(passport.ty, TypeKind::AxiomRegistry { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(passport.ty, TypeKind::StaticProof(_)));
    assert!(passport.trust >= TrustLevel::Axiom);
    assert!(passport.history.contains_event("metatheory:axiom_registry"));
}

#[test]
fn declared_axioms_are_required_before_axiom_dependencies_verify() {
    let ax = axiom_decl("Meta", "truth_lift", "Provable(P)->Truth(P)", AxiomKind::Soundness, "explicit truth lift", 1).unwrap();
    let registry = axiom_registry("Meta", vec![ax.clone()], 2).unwrap();
    assert_eq!(require_declared_axiom(&registry, "truth_lift", 3).unwrap().fingerprint, ax.fingerprint);
    assert!(require_declared_axiom(&registry, "missing", 4).is_err());

    let entry = dependency_entry_from_axiom(&ax);
    let verified = audit_metatheory_dependencies("Meta.soundness_boundary", vec![entry], Some(&registry), 5);
    assert_eq!(verified.status, DependencyAuditStatus::Verified);
    assert!(verified.has_axiom_taint);
    assert!(require_verified_metatheory_dependency_audit(&verified, 6).is_ok());

    let rejected = audit_metatheory_dependencies("Meta.soundness_boundary", verified.entries.clone(), None, 7);
    assert_eq!(rejected.status, DependencyAuditStatus::Rejected);
    assert!(require_verified_metatheory_dependency_audit(&rejected, 8).is_err());
}

#[test]
fn dependency_entries_preserve_passport_trust_history_and_type_identity() {
    let proof_term = Passport::proof_term("Meta", "p_intro", None);
    let proof = Passport::static_proof("Meta", "P", &proof_term);
    let entry = dependency_entry_from_passport("proof:P", DependencyUseKind::Theorem, &proof, 1).unwrap();

    assert_eq!(entry.kind, DependencyUseKind::Theorem);
    assert_eq!(entry.trust, proof.trust);
    assert!(entry.ty.contains("StaticProof<P>"));
    assert!(entry.history.iter().any(|event| event.contains("proof_kernel")));

    let report = audit_metatheory_dependencies("Meta.clean_dependency", vec![entry], None, 2);
    assert_eq!(report.status, DependencyAuditStatus::Verified);
    assert!(!report.has_axiom_taint);
    assert!(!report.has_unsafe_taint);
}

#[test]
fn dependency_audit_passport_preserves_max_taint_and_registry_fingerprint() {
    let safe = Passport::literal_nat("Core");
    let safe_entry = dependency_entry_from_passport("literal", DependencyUseKind::Unknown, &safe, 1).unwrap();
    let unsafe_ax = axiom_decl("Meta", "external_oracle", "Oracle(P)", AxiomKind::UnsafeExternal, "external unsafe oracle", 2).unwrap();
    let registry = axiom_registry("Meta", vec![unsafe_ax.clone()], 3).unwrap();
    let unsafe_entry = dependency_entry_from_axiom(&unsafe_ax);

    let report = audit_metatheory_dependencies(
        "Meta.external_boundary",
        vec![safe_entry, unsafe_entry],
        Some(&registry),
        4,
    );
    assert_eq!(report.status, DependencyAuditStatus::Verified);
    assert_eq!(report.max_trust, TrustLevel::Unsafe);
    assert!(report.has_axiom_taint);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert_eq!(report.registry_fingerprint.as_deref(), Some(registry.fingerprint.as_str()));

    let passport = metatheory_dependency_audit_passport("Meta", &report);
    assert!(matches!(passport.ty, TypeKind::MetatheoryDependencyAudit { .. }));
    assert_eq!(passport.trust, TrustLevel::Unsafe);
    assert!(passport.history.contains_event("metatheory:dependency_audit"));
    assert!(passport.history.contains_event("metatheory:registry:fingerprint"));
}

#[test]
fn dependency_audit_fingerprint_and_rendering_are_stable_and_order_sensitive() {
    let first = Passport::literal_nat("Core");
    let second = Passport::compressed_nat("Core");
    let first_entry = dependency_entry_from_passport("first", DependencyUseKind::Unknown, &first, 1).unwrap();
    let second_entry = dependency_entry_from_passport("second", DependencyUseKind::Unknown, &second, 2).unwrap();

    let report_a = audit_metatheory_dependencies(
        "Core.order",
        vec![first_entry.clone(), second_entry.clone()],
        None,
        3,
    );
    let report_b = audit_metatheory_dependencies("Core.order", vec![second_entry, first_entry], None, 4);
    assert_ne!(report_a.audit_fingerprint, report_b.audit_fingerprint);

    let rendered = render_metatheory_dependency_audit_report(&report_a);
    assert!(rendered.contains("DLM Metatheory Dependency Audit v1"));
    assert!(rendered.contains("subject: Core.order"));
    assert!(rendered.contains("entries: 2"));
}
