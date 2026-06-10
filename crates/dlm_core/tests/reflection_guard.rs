use dlm_core::{parse_module, Checker};

#[test]
fn reflection_claim_requires_explicit_bridge() {
    let source = r#"
module demo

theory Core {
  let proof = prove(7 > 0)
  let p = provable_of(proof)
  let reflected = reflection_claim(p)
}
"#;

    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);

    assert!(!report.ok(), "reflection without explicit bridge must fail");
    assert!(
        report
            .diagnostics
            .iter()
            .any(|diag| format!("{:?}", diag.kind).contains("ReflectionBoundaryError")),
        "expected ReflectionBoundaryError, got: {:?}",
        report.diagnostics
    );
}

#[test]
fn reflection_claim_with_bridge_passes_and_axiom_taints_lift() {
    let source = r#"
module demo

bridge Core_reflection : Core -> Core {
  kind = reflection
}

theory Core {
  let proof = prove(7 > 0)
  let p = provable_of(proof)
  let reflected = reflection_claim(p)
  let accepted = reflection_axiom(reflected)
}
"#;

    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);

    assert!(report.ok(), "expected OK, got: {:?}", report.diagnostics);
    assert!(
        report.inferred.iter().any(|(name, passport)| {
            name.ends_with(".accepted") && format!("{}", passport).contains("trust=Axiom")
        }),
        "reflection_axiom must be visible as Axiom-tainted; inferred: {:?}",
        report.inferred
    );
}

#[test]
fn dangerous_self_truth_forms_are_reflection_boundary_errors() {
    for builtin in [
        "truth_of_self",
        "says_unprovable_self",
        "liar_sentence",
        "truth_of_own_truth",
    ] {
        let source = format!(
            r#"
module demo

theory Core {{
  let bad = {builtin}()
}}
"#
        );
        let module = parse_module(&source).expect("parse");
        let report = Checker::new().check_module(&module);

        assert!(!report.ok(), "{builtin} must fail");
        assert!(
            report
                .diagnostics
                .iter()
                .any(|diag| format!("{:?}", diag.kind).contains("ReflectionBoundaryError")),
            "expected ReflectionBoundaryError for {builtin}, got: {:?}",
            report.diagnostics
        );
    }
}
