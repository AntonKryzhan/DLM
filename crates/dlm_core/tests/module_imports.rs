use dlm_core::{
    build_import_graph, import_decl, import_graph_passport, imported_public_symbols,
    module_export_passport, module_manifest, module_manifest_passport, private_export, public_export,
    require_public_export, Capability, ExportVisibility, Passport, TypeKind,
};

#[test]
fn module_manifest_tracks_imports_and_exports_without_becoming_theorem() {
    let manifest = module_manifest(
        "Math.Core",
        vec![import_decl("Math.Nat", Some("Nat"))],
        vec![public_export("zero"), private_export("internal_succ")],
        1,
    )
    .expect("manifest should validate");

    let passport = module_manifest_passport("Meta", &manifest);
    assert!(matches!(
        &passport.ty,
        TypeKind::ModuleManifest { ref module } if module == "Math.Core"
    ));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
    assert!(passport.capabilities.contains(Capability::CanInspectAst));
}

#[test]
fn manifest_rejects_duplicate_imports_aliases_and_exports() {
    let duplicate_import = module_manifest(
        "A",
        vec![import_decl("B", None::<String>), import_decl("B", None::<String>)],
        vec![public_export("x")],
        7,
    )
    .unwrap_err();
    assert_eq!(duplicate_import.kind, dlm_core::DiagnosticKind::ModuleImportError);

    let duplicate_alias = module_manifest(
        "A",
        vec![import_decl("B", Some("M")), import_decl("C", Some("M"))],
        vec![public_export("x")],
        8,
    )
    .unwrap_err();
    assert_eq!(duplicate_alias.kind, dlm_core::DiagnosticKind::ModuleImportError);

    let duplicate_export = module_manifest(
        "A",
        vec![],
        vec![public_export("x"), private_export("x")],
        9,
    )
    .unwrap_err();
    assert_eq!(duplicate_export.kind, dlm_core::DiagnosticKind::ModuleImportError);
}

#[test]
fn import_graph_resolves_dependencies_and_public_exports_only() {
    let nat = module_manifest(
        "Math.Nat",
        vec![],
        vec![public_export("zero"), private_export("secret")],
        1,
    )
    .unwrap();
    let core = module_manifest(
        "Math.Core",
        vec![import_decl("Math.Nat", Some("Nat"))],
        vec![public_export("main")],
        1,
    )
    .unwrap();

    let graph = build_import_graph("Math.Core", vec![core, nat], 1).expect("graph should resolve");
    assert_eq!(graph.edges.len(), 1);

    let zero = require_public_export(&graph, "Math.Nat", "zero", 1).unwrap();
    assert_eq!(zero.visibility, ExportVisibility::Public);

    let secret = require_public_export(&graph, "Math.Nat", "secret", 1).unwrap_err();
    assert_eq!(secret.kind, dlm_core::DiagnosticKind::ModuleImportError);

    let imported = imported_public_symbols(&graph, "Math.Core", 1).unwrap();
    assert_eq!(imported, vec![("Math.Nat".to_string(), "zero".to_string())]);

    let graph_passport = import_graph_passport("Meta", &graph);
    assert!(matches!(&graph_passport.ty, TypeKind::ImportGraph { ref root } if root == "Math.Core"));
}

#[test]
fn import_graph_rejects_missing_modules_and_cycles() {
    let imports_missing = module_manifest(
        "A",
        vec![import_decl("Missing", None::<String>)],
        vec![public_export("x")],
        1,
    )
    .unwrap();
    let missing = build_import_graph("A", vec![imports_missing], 1).unwrap_err();
    assert!(missing.iter().any(|diag| diag.message.contains("missing module")));

    let a = module_manifest(
        "A",
        vec![import_decl("B", None::<String>)],
        vec![public_export("a")],
        1,
    )
    .unwrap();
    let b = module_manifest(
        "B",
        vec![import_decl("A", None::<String>)],
        vec![public_export("b")],
        1,
    )
    .unwrap();
    let cycle = build_import_graph("A", vec![a, b], 1).unwrap_err();
    assert!(cycle.iter().any(|diag| diag.message.contains("cyclic import graph")));
}

#[test]
fn exported_passport_preserves_source_trust_and_capabilities() {
    let source = Passport::static_proof("P");
    let export = public_export("p_proof");
    let exported = module_export_passport("Meta", "Math.Proofs", &export, &source);

    assert!(matches!(
        &exported.ty,
        TypeKind::ModuleExport { ref module, ref symbol, ref visibility }
            if module == "Math.Proofs" && symbol == "p_proof" && visibility == "public"
    ));
    assert_eq!(exported.trust, source.trust);
    assert_eq!(exported.capabilities, source.capabilities);
    assert!(exported.history.contains_event("module:export:Math.Proofs:p_proof:public"));
}
