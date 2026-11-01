//! v0.81 — trusted-base audits (`dlm audit`) and project checking
//! (`dlm.project`).

use std::path::{Path, PathBuf};

use dlm_core::{
    audit_check_report, check_project, link_program, load_program_with_paths, load_project, parse_module,
    render_program_audit, AuditStatus, Checker, ProgramAudit, StatementAuditKind, TrustedComponent,
};

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn audit_file(relative: &str) -> ProgramAudit {
    let program = load_program_with_paths(&repo().join(relative), &[repo().join("lib")]).expect("load");
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    audit_check_report(&report, program.dependencies().iter().map(|m| m.name.clone()).collect())
}

fn audit_sources(root: &str, library: &[&str]) -> ProgramAudit {
    let program = link_program(
        parse_module(root).unwrap(),
        library.iter().map(|s| parse_module(s).unwrap()).collect(),
    )
    .unwrap();
    let report = Checker::new().check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    audit_check_report(&report, program.dependencies().iter().map(|m| m.name.clone()).collect())
}

#[test]
fn library_usage_is_closed_and_rests_on_the_kernel_only() {
    let audit = audit_file("examples/valid/std_library_usage.dlm");
    assert_eq!(audit.status, AuditStatus::Closed);
    assert_eq!(audit.imports, vec!["std.nat".to_string(), "std.list".to_string()]);
    assert_eq!(audit.components.iter().copied().collect::<Vec<_>>(), vec![TrustedComponent::SymbolicKernel]);
    assert_eq!((audit.proved(), audit.crosscheck_agree, audit.crosscheck_skipped), (3, 3, 0));
    let fast = audit.statements.iter().find(|s| s.name == "App.fast_add_twice").unwrap();
    // Lemma use is transitive and crosses the module boundary (add_acc_spec is private to std.nat).
    assert_eq!(fast.lemmas, vec!["add_acc_spec".to_string(), "add_fast_spec".to_string()]);
    assert_eq!(fast.method, "symbolic:normalize");
    assert_eq!(fast.crosscheck.as_deref(), Some("agree:cases=6"));
    let text = render_program_audit(&audit);
    assert!(text.starts_with("DLM-AUDIT v1\nmodule: examples.std_library_usage\n"), "{text}");
    assert!(text.contains("trusted components: symbolic-kernel (3)"), "{text}");
}

#[test]
fn methods_axioms_and_open_goals_are_classified() {
    let audit = audit_file("examples/valid/ordinary_theorems.dlm");
    assert_eq!(audit.status, AuditStatus::AxiomDependent);
    let method = |name: &str| audit.statements.iter().find(|s| s.name == format!("Arithmetic.{name}")).unwrap().method.clone();
    assert_eq!(method("fact_5"), "computation");
    assert_eq!(method("gauss_below_200"), "exhaustion:below=200");
    assert_eq!(method("excluded_middle"), "exhaustion:finite");
    assert_eq!(method("add_comm"), "axiom");
    assert_eq!(method("double_monus"), "conjecture:tested:cases=1000");
    assert_eq!(audit.open_goals(), 1);
    assert!(audit.components.contains(&TrustedComponent::RuntimeEvaluator));
    assert_eq!(audit.axioms.len(), 1);
    assert!(audit.axioms[0].used_by.is_empty(), "declared but unused");

    let symbolic = audit_file("examples/valid/symbolic_advanced_induction.dlm");
    assert_eq!(symbolic.status, AuditStatus::Closed);
    let half = symbolic.statements.iter().find(|s| s.name == "Advanced.half_bound").unwrap();
    assert_eq!(half.method, "symbolic:induction");
}

#[test]
fn imported_axioms_are_attributed_to_the_theorems_that_use_them() {
    let base = "module lib.base
theory Base {
    fn f(n: Nat) -> Nat = n
    axiom f_zero(n: Nat): nat_eq(f(n) + 0, f(n))
    theorem clean(n: Nat): nat_eq(f(n), n) by normalize
}
";
    let audit = audit_sources(
        "module app
import lib.base
theory App extends Base {
    theorem uses(n: Nat): nat_eq(f(n) + 0 + 1, f(n) + 1) by normalize using f_zero
    theorem indirect(n: Nat): nat_eq(f(n) + 2, n + 2) by normalize using clean
}
",
        &[base],
    );
    assert_eq!(audit.status, AuditStatus::AxiomDependent);
    let uses = audit.statements.iter().find(|s| s.name == "App.uses").unwrap();
    assert_eq!(uses.axioms, vec!["f_zero: forall n: Nat. nat_eq((f(n) + 0), f(n))".to_string()]);
    assert!(uses.components.contains(&TrustedComponent::Axioms));
    let indirect = audit.statements.iter().find(|s| s.name == "App.indirect").unwrap();
    assert!(indirect.axioms.is_empty());
    assert!(!indirect.unattributed_taint);
    assert_eq!(audit.axioms.len(), 1);
    assert_eq!(audit.axioms[0].used_by, vec!["App.uses".to_string()]);
}

#[test]
fn bridges_and_tainted_values_are_reported() {
    let unsafe_bridge = audit_file("examples/valid/unsafe_bridge_profile.dlm");
    assert_eq!(unsafe_bridge.status, AuditStatus::Unsafe);
    assert_eq!(unsafe_bridge.bridges, vec!["PA_bad: PA -> Meta (unsafe)".to_string()]);
    let assumed = audit_file("examples/valid/axiom_research.dlm");
    assert_eq!(assumed.status, AuditStatus::AxiomDependent);
    assert!(assumed.tainted_values.iter().any(|v| v == "Core.assumption (Axiom)"), "{:?}", assumed.tainted_values);
    assert_eq!(assumed.statements.iter().filter(|s| s.kind == StatementAuditKind::Theorem).count(), 0);
}

#[test]
fn fingerprints_are_stable_and_content_sensitive() {
    let source = "module m\ntheory T {\n    fn d(n: Nat) -> Nat = n + n\n    theorem t(n: Nat): nat_eq(d(n), mul(2, n)) by normalize\n}\n";
    let a = audit_sources(source, &[]);
    let b = audit_sources(source, &[]);
    assert_eq!(a.fingerprint, b.fingerprint);
    assert!(a.fingerprint.starts_with("fnv64:"));
    let changed = audit_sources(&source.replace("mul(2, n)", "n + n"), &[]);
    assert_ne!(a.fingerprint, changed.fingerprint);
}

#[test]
fn project_checks_every_root_and_reports_failures() {
    let dir = std::env::temp_dir().join(format!("dlm_project_test_{}", std::process::id()));
    std::fs::create_dir_all(dir.join("src")).unwrap();
    std::fs::create_dir_all(dir.join("vendor/util")).unwrap();
    std::fs::write(dir.join("vendor/util/math.dlm"), "module util.math\ntheory UMath {\n    fn d(n: Nat) -> Nat = n + n\n    theorem d_spec(n: Nat): nat_eq(d(n), mul(2, n)) by normalize\n}\n").unwrap();
    std::fs::write(dir.join("src/good.dlm"), "module good\nimport util.math\ntheory Good extends UMath {\n    theorem q(n: Nat): nat_eq(d(d(n)), mul(4, n)) by normalize using d_spec\n}\n").unwrap();
    std::fs::write(dir.join("src/bad.dlm"), "module bad\ntheory Bad {\n    theorem f(n: Nat): nat_eq(n, 1) by induction n\n}\n").unwrap();
    std::fs::write(dir.join("dlm.project"), "name = sample\nlib = vendor\nroots = src\n").unwrap();

    let manifest = load_project(&dir).unwrap();
    let report = check_project(&manifest, None);
    assert_eq!(report.entries.len(), 2);
    assert_eq!(report.failed(), 1);
    assert!(!report.ok());
    let text = report.render();
    assert!(text.contains("FAILED") && text.contains("bad.dlm"), "{text}");
    assert!(text.contains("module=good status=closed proved=1"), "{text}");
    assert!(text.contains("roots: 2  failed: 1  proved: 1"), "{text}");

    std::fs::remove_file(dir.join("src/bad.dlm")).unwrap();
    let report = check_project(&load_project(&dir.join("dlm.project")).unwrap(), Some(true));
    assert!(report.ok(), "{}", report.render());
    assert_eq!(report.status(), Some(AuditStatus::Closed));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn the_repository_project_checks() {
    let manifest = load_project(&repo()).expect("repository dlm.project");
    assert_eq!(manifest.name, "DLM");
    let report = check_project(&manifest, None);
    assert!(report.ok(), "{}", report.render());
    // Unsafe bridges are declared by some examples, so the worst status is unsafe.
    assert_eq!(report.status(), Some(AuditStatus::Unsafe));
    let proved: usize = report.entries.iter().filter_map(|e| e.audit.as_ref()).map(|a| a.proved()).sum();
    assert!(proved >= 50, "library and examples prove many theorems: {proved}");
    // The standard library itself is closed.
    for entry in report.entries.iter().filter(|e| e.path.to_string_lossy().contains("lib/std")) {
        assert_eq!(entry.audit.as_ref().unwrap().status, AuditStatus::Closed, "{}", entry.path.display());
    }
}
