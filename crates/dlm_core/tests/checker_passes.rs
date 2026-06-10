use dlm_core::{
    parse_module, run_frontend_passes, Checker, DiagnosticKind, PassId, PassStatus,
};

#[test]
fn frontend_pipeline_reports_raw_ast_and_resolution_passes() {
    let source = r#"
module examples.pass_pipeline

theory Core {
    let x = 1
}
"#;

    let module = parse_module(source).expect("module parses");
    let frontend = run_frontend_passes(&module).expect("frontend passes");

    assert!(frontend.report.ok());
    assert_eq!(frontend.report.len(), 2);
    assert_eq!(
        frontend.report.find(PassId::RawAstAccepted).unwrap().status,
        PassStatus::Passed
    );
    assert_eq!(
        frontend.report.find(PassId::NameResolution).unwrap().status,
        PassStatus::Passed
    );
    assert!(frontend.resolved.symbols.theory_id("Core").is_some());
}

#[test]
fn checker_report_includes_legacy_checker_pass_after_frontend() {
    let source = r#"
module examples.checker_pipeline

theory Core {
    let a = 1
    let b = a + 2
}
"#;

    let module = parse_module(source).expect("module parses");
    let report = Checker::new().check_module(&module);

    assert!(report.ok());
    assert_eq!(report.value_count, 2);
    assert_eq!(
        report.passes.find(PassId::RawAstAccepted).unwrap().status,
        PassStatus::Passed
    );
    assert_eq!(
        report.passes.find(PassId::NameResolution).unwrap().status,
        PassStatus::Passed
    );
    assert_eq!(
        report.passes.find(PassId::LegacyChecker).unwrap().status,
        PassStatus::Passed
    );
}

#[test]
fn checker_stops_before_legacy_checker_when_resolution_fails() {
    let source = r#"
module examples.duplicate_value

theory Core {
    let x = 1
    let x = 2
}
"#;

    let module = parse_module(source).expect("module parses");
    let report = Checker::new().check_module(&module);

    assert!(!report.ok());
    assert_eq!(report.value_count, 0);
    assert!(report
        .diagnostics
        .iter()
        .any(|diag| diag.kind == DiagnosticKind::NameError));
    assert_eq!(
        report.passes.find(PassId::NameResolution).unwrap().status,
        PassStatus::Failed
    );
    assert_eq!(
        report.passes.find(PassId::LegacyChecker).unwrap().status,
        PassStatus::Skipped
    );
}
