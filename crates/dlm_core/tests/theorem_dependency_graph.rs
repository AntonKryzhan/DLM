use dlm_core::{
    audit_conservative_extension, audit_metatheory_dependencies, axiom_decl, axiom_registry,
    closed_closure_obligation, conservative_extension_audit_passport, dependency_entry_from_axiom,
    dependency_entry_from_passport, export_global_metatheory_inventory, global_metatheory_inventory,
    global_metatheory_inventory_passport, metatheory_closure_report, metatheory_closure_report_passport,
    open_closure_obligation, preserved_theorem, require_verified_global_metatheory_inventory,
    statement_passport, theorem_dependency_edge, theorem_dependency_node_from_passport,
    theorem_from_static_proof, AxiomKind, ClosureObligationKind, ConservativeExtensionStatus,
    DependencyAuditStatus, DependencyUseKind, MetatheoryClosureReport, MetatheoryClosureStatus,
    MetatheoryInventoryStatus, Passport, TheoremDependencyNodeKind, TrustLevel, TypeKind,
    ValidationState,
};

fn dependency_audit_passport(subject: &str) -> Passport {
    let term = Passport::proof_term("Meta", format!("{}_intro", subject.replace('.', "_")), None);
    let proof = Passport::static_proof("Meta", format!("{subject}:P"), &term);
    let entry = dependency_entry_from_passport(
        format!("proof:{subject}"),
        DependencyUseKind::Theorem,
        &proof,
        1,
    )
    .unwrap();
    let audit = audit_metatheory_dependencies(subject, vec![entry], None, 2);
    assert_eq!(audit.status, DependencyAuditStatus::Verified);
    dlm_core::metatheory_dependency_audit_passport("Meta", &audit)
}

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
    let closure = metatheory_closure_report(subject, &audit, &[], vec![obligation], 4);
    assert_eq!(closure.status, MetatheoryClosureStatus::Open);
    closure
}

fn theorem(name: &str, proposition: &str) -> Passport {
    let statement = statement_passport("Meta", proposition);
    let term = Passport::proof_term("Meta", format!("{name}_intro"), None);
    let proof = Passport::static_proof("Meta", proposition, &term);
    theorem_from_static_proof("Meta", name, &statement, &proof, 1).unwrap()
}

#[test]
fn global_inventory_verifies_theorem_closure_and_dependency_edges() {
    let theorem = theorem("soundness_boundary", "Provable(P) != Truth(P)");
    let theorem_node = theorem_dependency_node_from_passport(
        "theorem:soundness_boundary",
        TheoremDependencyNodeKind::Theorem,
        &theorem,
        10,
    )
    .unwrap();

    let closure = closed_closure("Meta.base");
    let closure_passport = metatheory_closure_report_passport("Meta", &closure);
    let closure_node = theorem_dependency_node_from_passport(
        "closure:Meta.base",
        TheoremDependencyNodeKind::ClosureReport,
        &closure_passport,
        11,
    )
    .unwrap();

    let audit_passport = dependency_audit_passport("Meta.base.deps");
    let audit_node = theorem_dependency_node_from_passport(
        "deps:Meta.base",
        TheoremDependencyNodeKind::DependencyAudit,
        &audit_passport,
        12,
    )
    .unwrap();

    let edge_theorem_to_closure = theorem_dependency_edge(
        "theorem:soundness_boundary",
        "closure:Meta.base",
        "closed_by",
        13,
    )
    .unwrap();
    let edge_closure_to_deps = theorem_dependency_edge(
        "closure:Meta.base",
        "deps:Meta.base",
        "depends_on",
        14,
    )
    .unwrap();

    let report = global_metatheory_inventory(
        "Meta.global",
        vec![theorem_node, closure_node, audit_node],
        vec![edge_theorem_to_closure, edge_closure_to_deps],
        &[],
        15,
    );

    assert_eq!(report.status, MetatheoryInventoryStatus::Verified);
    assert!(report.diagnostics.is_empty());
    assert!(require_verified_global_metatheory_inventory(&report, 16).is_ok());

    let passport = global_metatheory_inventory_passport("Meta", &report);
    assert!(matches!(passport.ty, TypeKind::GlobalMetatheoryInventory { .. }));
    assert_eq!(passport.validation, ValidationState::StaticChecked);
    assert!(passport.history.contains_event("metatheory:global_inventory"));
}

#[test]
fn inventory_rejects_mislabeled_nodes_unknown_edges_and_duplicates() {
    let statement = statement_passport("Meta", "P");
    let err = theorem_dependency_node_from_passport(
        "not-a-theorem",
        TheoremDependencyNodeKind::Theorem,
        &statement,
        10,
    )
    .unwrap_err();
    assert!(err.message.contains("does not match passport type"));

    let theorem = theorem("id", "P -> P");
    let node = theorem_dependency_node_from_passport("theorem:id", TheoremDependencyNodeKind::Theorem, &theorem, 11).unwrap();
    let unknown_edge = theorem_dependency_edge("theorem:id", "missing", "depends_on", 12).unwrap();
    let duplicate = node.clone();
    let report = global_metatheory_inventory("Meta.bad", vec![node, duplicate], vec![unknown_edge], &[], 13);

    assert_eq!(report.status, MetatheoryInventoryStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("duplicate theorem dependency node")));
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("is not a node")));
    assert!(require_verified_global_metatheory_inventory(&report, 14).is_err());
}

#[test]
fn open_closure_node_keeps_inventory_open_not_rejected() {
    let closure = open_closure("Meta.open");
    let passport = metatheory_closure_report_passport("Meta", &closure);
    assert_eq!(passport.validation, ValidationState::ConstraintChecked);
    let node = theorem_dependency_node_from_passport(
        "closure:Meta.open",
        TheoremDependencyNodeKind::ClosureReport,
        &passport,
        10,
    )
    .unwrap();

    let report = global_metatheory_inventory("Meta.open_inventory", vec![node], vec![], &[], 11);
    assert_eq!(report.status, MetatheoryInventoryStatus::Open);
    assert!(report.diagnostics.is_empty());
    assert!(require_verified_global_metatheory_inventory(&report, 12).is_err());

    let inventory_passport = global_metatheory_inventory_passport("Meta", &report);
    assert_eq!(inventory_passport.validation, ValidationState::ConstraintChecked);
}

#[test]
fn conservative_extension_audits_are_global_inventory_evidence() {
    let base = closed_closure("Meta.base");
    let extension = closed_closure("Meta.extension");
    let base_thm = theorem("plus_zero", "forall n:Nat. n + 0 = n");
    let ext_thm = theorem("plus_zero", "forall n:Nat. n + 0 = n");
    let preserved = preserved_theorem("plus_zero", &base_thm, &ext_thm, 10).unwrap();
    let conservative = audit_conservative_extension(&base, &extension, vec![preserved], vec![], 11);
    assert_eq!(conservative.status, ConservativeExtensionStatus::Verified);
    let conservative_passport = conservative_extension_audit_passport("Meta", &conservative);
    let node = theorem_dependency_node_from_passport(
        "conservative:Meta.base->Meta.extension",
        TheoremDependencyNodeKind::ConservativeExtensionAudit,
        &conservative_passport,
        12,
    )
    .unwrap();

    let report = global_metatheory_inventory("Meta.conservative", vec![node], vec![], &[conservative], 13);
    assert_eq!(report.status, MetatheoryInventoryStatus::Verified);
    assert_eq!(report.conservative_extension_fingerprints.len(), 1);
    assert!(report.inventory_fingerprint.contains("dlm-global-metatheory-inventory"));
}

#[test]
fn rejected_conservative_extension_rejects_inventory() {
    let base = open_closure("Meta.open_base");
    let extension = closed_closure("Meta.extension");
    let conservative = audit_conservative_extension(&base, &extension, vec![], vec![], 10);
    assert_eq!(conservative.status, ConservativeExtensionStatus::Rejected);

    let report = global_metatheory_inventory("Meta.bad_conservative", vec![], vec![], &[conservative], 11);
    assert_eq!(report.status, MetatheoryInventoryStatus::Rejected);
    assert!(report.diagnostics.iter().any(|diagnostic| diagnostic.message.contains("conservative extension audit")));
}

#[test]
fn inventory_preserves_axiom_oracle_unsafe_taint() {
    let axiom = axiom_decl(
        "Meta",
        "unsafe_external_oracle",
        "Oracle(P)",
        AxiomKind::UnsafeExternal,
        "external unsafe oracle imported explicitly",
        1,
    )
    .unwrap();
    let registry = axiom_registry("Meta", vec![axiom.clone()], 2).unwrap();
    let entry = dependency_entry_from_axiom(&axiom);
    let audit = audit_metatheory_dependencies("Meta.unsafe", vec![entry], Some(&registry), 3);
    assert_eq!(audit.status, DependencyAuditStatus::Verified);
    let passport = dlm_core::metatheory_dependency_audit_passport("Meta", &audit);
    let node = theorem_dependency_node_from_passport(
        "deps:unsafe",
        TheoremDependencyNodeKind::DependencyAudit,
        &passport,
        4,
    )
    .unwrap();
    let report = global_metatheory_inventory("Meta.unsafe_inventory", vec![node], vec![], &[], 5);

    assert_eq!(report.status, MetatheoryInventoryStatus::Verified);
    assert!(report.has_axiom_taint);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert!(report.max_trust >= TrustLevel::Unsafe);

    let inventory_passport = global_metatheory_inventory_passport("Meta", &report);
    assert_eq!(inventory_passport.provenance, dlm_core::Provenance::UnsafeExternal);
}

#[test]
fn inventory_export_is_stable_and_order_sensitive() {
    let first_theorem = theorem("first", "P");
    let second_theorem = theorem("second", "Q");
    let first_node = theorem_dependency_node_from_passport("theorem:first", TheoremDependencyNodeKind::Theorem, &first_theorem, 10).unwrap();
    let second_node = theorem_dependency_node_from_passport("theorem:second", TheoremDependencyNodeKind::Theorem, &second_theorem, 11).unwrap();
    let edge = theorem_dependency_edge("theorem:first", "theorem:second", "uses", 12).unwrap();

    let first = global_metatheory_inventory(
        "Meta.order",
        vec![first_node.clone(), second_node.clone()],
        vec![edge.clone()],
        &[],
        13,
    );
    let second = global_metatheory_inventory(
        "Meta.order",
        vec![second_node, first_node],
        vec![edge],
        &[],
        14,
    );
    assert_ne!(first.inventory_fingerprint, second.inventory_fingerprint);

    let exported = export_global_metatheory_inventory(&first);
    assert!(exported.contains("DLM Global Metatheory Inventory v1"));
    assert!(exported.contains("subject: Meta.order"));
    assert!(exported.contains("status: verified"));
    assert!(exported.contains("nodes: 2"));
    assert!(exported.contains("edges: 1"));
    assert!(exported.contains("inventory_fingerprint:"));
}
