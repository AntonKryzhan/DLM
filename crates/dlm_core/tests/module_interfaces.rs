use dlm_core::{
    audit_module_import, build_import_graph, export_module_interface_text, import_decl,
    module_import_audit_passport, module_interface, module_interface_passport, module_manifest,
    private_export, public_export, public_interface_symbols, render_module_import_audit_report,
    require_verified_module_import_audit, Capability, ExportVisibility, ModuleImportAuditStatus,
    Passport, TypeKind,
};

fn proof_of_p() -> Passport {
    let proof_term = Passport::proof_term("Meta", "p_intro", None);
    Passport::static_proof("Meta", "P", &proof_term)
}

fn nat_source() -> Passport {
    Passport::literal_nat("Core")
}

#[test]
fn module_interface_is_a_contract_not_a_proof_or_theorem() {
    let interface = module_interface(
        "Math.Core",
        vec![
            (public_export("zero"), nat_source()),
            (private_export("p_proof"), proof_of_p()),
        ],
        1,
    )
    .expect("interface should validate");

    assert_eq!(public_interface_symbols(&interface), vec!["zero".to_string()]);
    assert_eq!(interface.symbols.len(), 2);
    assert!(!interface.fingerprint.is_empty());

    let passport = module_interface_passport("Meta", &interface);
    assert!(matches!(
        &passport.ty,
        TypeKind::ModuleInterface { ref module } if module == "Math.Core"
    ));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
    assert!(passport.capabilities.contains(Capability::CanInspectAst));
}

#[test]
fn module_interface_rejects_duplicate_or_empty_symbols() {
    let duplicate = module_interface(
        "Math.Core",
        vec![(public_export("x"), nat_source()), (private_export("x"), proof_of_p())],
        7,
    )
    .unwrap_err();
    assert_eq!(duplicate.kind, dlm_core::DiagnosticKind::ModuleInterfaceError);

    let empty = module_interface("Math.Core", vec![(public_export(""), nat_source())], 8).unwrap_err();
    assert_eq!(empty.kind, dlm_core::DiagnosticKind::ModuleInterfaceError);
}

#[test]
fn verified_import_audit_requires_explicit_edge_public_manifest_and_interface_entry() {
    let provider = module_manifest(
        "Math.Nat",
        vec![],
        vec![public_export("zero"), private_export("secret")],
        1,
    )
    .unwrap();
    let importer = module_manifest(
        "Math.Core",
        vec![import_decl("Math.Nat", Some("Nat"))],
        vec![public_export("main")],
        1,
    )
    .unwrap();
    let graph = build_import_graph("Math.Core", vec![importer, provider], 1).unwrap();
    let interface = module_interface(
        "Math.Nat",
        vec![(public_export("zero"), nat_source()), (private_export("secret"), proof_of_p())],
        1,
    )
    .unwrap();

    let report = audit_module_import(&graph, "Math.Core", "Math.Nat", vec!["zero".into()], &interface, 1);
    assert_eq!(report.status, ModuleImportAuditStatus::Verified);
    assert_eq!(report.resolved_symbols.len(), 1);
    require_verified_module_import_audit(&report, 1).unwrap();

    let passport = module_import_audit_passport("Meta", &report);
    assert!(matches!(
        &passport.ty,
        TypeKind::ModuleImportAudit { ref importer, ref provider, ref status }
            if importer == "Math.Core" && provider == "Math.Nat" && status == "verified"
    ));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
}

#[test]
fn import_audit_rejects_private_symbols_stale_interfaces_and_missing_edges() {
    let provider = module_manifest(
        "Math.Nat",
        vec![],
        vec![public_export("zero"), private_export("secret")],
        1,
    )
    .unwrap();
    let importer = module_manifest("Math.Core", vec![], vec![public_export("main")], 1).unwrap();
    let graph = build_import_graph("Math.Core", vec![importer, provider], 1).unwrap();
    let interface = module_interface("Math.Nat", vec![(public_export("zero"), nat_source())], 1).unwrap();

    let no_edge = audit_module_import(&graph, "Math.Core", "Math.Nat", vec!["zero".into()], &interface, 1);
    assert_eq!(no_edge.status, ModuleImportAuditStatus::Rejected);
    assert!(no_edge.diagnostics.iter().any(|diag| diag.message.contains("does not import")));

    let private = audit_module_import(&graph, "Math.Core", "Math.Nat", vec!["secret".into()], &interface, 1);
    assert_eq!(private.status, ModuleImportAuditStatus::Rejected);

    let stale = audit_module_import(&graph, "Math.Core", "Math.Nat", vec!["zero".into()], &module_interface("Other", vec![(public_export("zero"), nat_source())], 1).unwrap(), 1);
    assert_eq!(stale.status, ModuleImportAuditStatus::Rejected);
    assert!(stale.diagnostics.iter().any(|diag| diag.message.contains("does not match")));
}

#[test]
fn interface_fingerprint_is_stable_across_input_order_but_changes_with_visibility() {
    let forward = module_interface(
        "Math.Core",
        vec![(public_export("a"), nat_source()), (private_export("b"), proof_of_p())],
        1,
    )
    .unwrap();
    let reversed = module_interface(
        "Math.Core",
        vec![(private_export("b"), proof_of_p()), (public_export("a"), nat_source())],
        1,
    )
    .unwrap();
    let changed_visibility = module_interface(
        "Math.Core",
        vec![(private_export("a"), nat_source()), (private_export("b"), proof_of_p())],
        1,
    )
    .unwrap();

    assert_eq!(forward.fingerprint, reversed.fingerprint);
    assert_ne!(forward.fingerprint, changed_visibility.fingerprint);
    assert_eq!(forward.symbols[0].symbol, "a");
    assert_eq!(forward.symbols[0].visibility, ExportVisibility::Public);
}

#[test]
fn interface_export_and_audit_report_are_stable_textual_audit_artifacts() {
    let provider = module_manifest("P", vec![], vec![public_export("x")], 1).unwrap();
    let importer = module_manifest("I", vec![import_decl("P", None::<String>)], vec![], 1).unwrap();
    let graph = build_import_graph("I", vec![importer, provider], 1).unwrap();
    let interface = module_interface("P", vec![(public_export("x"), nat_source())], 1).unwrap();
    let report = audit_module_import(&graph, "I", "P", vec!["x".into()], &interface, 1);

    let interface_text = export_module_interface_text(&interface);
    assert!(interface_text.contains("DLM module interface"));
    assert!(interface_text.contains("module: P"));
    assert!(interface_text.contains("fingerprint:"));
    assert!(interface_text.contains("x [public]"));

    let audit_text = render_module_import_audit_report(&report);
    assert!(audit_text.contains("DLM module import audit"));
    assert!(audit_text.contains("status: verified"));
    assert!(audit_text.contains("interface_fingerprint:"));
    assert!(audit_text.contains("audit_fingerprint:"));
}
