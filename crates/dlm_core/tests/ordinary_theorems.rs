//! v0.76 — statements, theorems and proofs in `.dlm` syntax.

use dlm_core::{
    audit_certificate_against_theorem, parse_module, theorem_from_static_proof, CheckPolicy, CheckReport, Checker,
    DiagnosticKind, Passport, ProofCertificateStatus, Runtime, TrustLevel, TypeKind, ValidationState, MAX_PROOF_CASES,
};

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing passport {name}"))
}

fn theory(body: &str) -> String {
    format!("module demo\ntheory T {{\n    fn double(n: Nat) -> Nat = n + n\n{body}\n}}\n")
}

fn rejects_with(source: &str, kind: DiagnosticKind) {
    let report = check(source);
    assert!(report.diagnostics.iter().any(|d| d.kind == kind), "expected {kind:?} for\n{source}\ngot {:?}", report.diagnostics);
}

const EXAMPLE: &str = include_str!("../../../examples/valid/ordinary_theorems.dlm");

#[test]
fn example_theorems_are_proved_and_certified() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);

    for (name, proposition) in [
        ("fact_5", "nat_eq(fact(5), 120)"),
        ("add_comm_below_50", "forall n: Nat < 50, m: Nat < 50. nat_eq((n + m), (m + n))"),
        ("gauss_below_200", "forall n: Nat < 200. nat_eq(mul(2, sum_to(n)), mul(n, (n + 1)))"),
        ("excluded_middle", "forall b: Bool. or(b, not(b))"),
    ] {
        let theorem = passport(&report, &format!("Arithmetic.{name}"));
        assert_eq!(theorem.ty, TypeKind::Theorem { name: name.into(), proposition: proposition.into() });
        assert_eq!(theorem.validation, ValidationState::StaticChecked);
        assert!(theorem.trust < TrustLevel::Axiom, "{name} must not be axiom-tainted");
    }

    let axiom = passport(&report, "Arithmetic.add_comm");
    assert!(matches!(&axiom.ty, TypeKind::Theorem { name, .. } if name == "add_comm"));
    assert_eq!(axiom.trust, TrustLevel::Axiom);
    assert_eq!(axiom.validation, ValidationState::Assumed);

    let conjecture = passport(&report, "Arithmetic.double_monus");
    assert!(matches!(conjecture.ty, TypeKind::Goal { .. }), "a tested conjecture stays an open goal");
    assert!(conjecture.to_string().contains("conjecture:double_monus:tested:cases=1000:below=1000:no_counterexample"));

    assert_eq!(report.certificates.len(), 6);
    for certificate in &report.certificates {
        let theorem = passport(&report, &format!("Arithmetic.{}", certificate.theorem_name));
        let audit = audit_certificate_against_theorem(certificate, theorem, 0);
        assert!(audit.verified(), "{}: {audit:?}", certificate.theorem_name);
        let expected = if certificate.theorem_name == "add_comm" {
            ProofCertificateStatus::AxiomAdmitted
        } else {
            ProofCertificateStatus::Checked
        };
        assert_eq!(certificate.status, expected, "{}", certificate.theorem_name);
    }

    let module = parse_module(EXAMPLE).unwrap();
    let run = Runtime::new().run_module(&module).expect("runtime");
    assert_eq!(
        run.output,
        vec![
            "StaticProof<nat_eq(fact(5), 120)>",
            "StaticProof<forall n: Nat, m: Nat. nat_eq((n + m), (m + n))>",
            "Prop<forall n: Nat. nat_eq(monus(double(n), n), n)>",
        ]
    );
}

#[test]
fn proof_method_is_recorded_in_the_theorem_history() {
    let report = check(EXAMPLE);
    let history = |name: &str| passport(&report, &format!("Arithmetic.{name}")).to_string();
    assert!(history("fact_5").contains("proof:computation:cases=1:theorem=fact_5"));
    assert!(history("add_comm_below_50").contains("proof:exhaustion:below=50:cases=2500"));
    assert!(history("excluded_middle").contains("proof:exhaustion:finite:cases=2"));
    assert!(history("add_comm").contains("theorem:axiom:add_comm"));
}

#[test]
fn false_statements_are_refuted_with_counterexamples() {
    let closed = check(include_str!("../../../examples/invalid/theorem_false_closed.dlm"));
    assert!(closed.diagnostics.iter().any(|d| d.message.contains("evaluates to false")), "{:?}", closed.diagnostics);
    let bounded = check(include_str!("../../../examples/invalid/theorem_false_counterexample.dlm"));
    assert!(bounded.diagnostics.iter().any(|d| d.message.contains("counterexample n = 0")), "{:?}", bounded.diagnostics);
    let conjecture = check(include_str!("../../../examples/invalid/conjecture_refuted.dlm"));
    assert!(conjecture.diagnostics.iter().any(|d| d.message.contains("refuted")), "{:?}", conjecture.diagnostics);
    // The first counterexample in enumeration order is reported.
    let report = check(&theory("    theorem t(a: Nat, b: Nat) below 5: not(and(nat_eq(a, 2), nat_eq(b, 3)))"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("counterexample a = 2, b = 3")), "{:?}", report.diagnostics);
}

#[test]
fn unbounded_universal_statements_are_never_proved_by_testing() {
    rejects_with(include_str!("../../../examples/invalid/theorem_unbounded_requires_bound.dlm"), DiagnosticKind::StatementTheoremError);
    // A conjecture is a Goal; the core theorem constructor rejects it.
    let report = check(EXAMPLE);
    let goal = passport(&report, "Arithmetic.double_monus");
    let proof = passport(&report, "Arithmetic.fact_5");
    assert!(theorem_from_static_proof("Arithmetic", "smuggled", goal, proof, 1).is_err());
}

#[test]
fn statements_are_pure_closed_and_boolean() {
    rejects_with(include_str!("../../../examples/invalid/statement_must_be_pure.dlm"), DiagnosticKind::StatementTheoremError);
    rejects_with(include_str!("../../../examples/invalid/statement_depends_on_theory_value.dlm"), DiagnosticKind::StatementTheoremError);
    rejects_with(&theory("    theorem t: double(2)"), DiagnosticKind::StatementTheoremError);
    rejects_with(&theory("    theorem t(p: Option<Nat>) below 3: true"), DiagnosticKind::StatementTheoremError);
    rejects_with(&theory("    theorem t(b: Bool) below 4: b"), DiagnosticKind::StatementTheoremError);
    rejects_with(&theory("    theorem list: true"), DiagnosticKind::NameError);
    rejects_with(&theory("    theorem t: true\n    theorem t: true"), DiagnosticKind::NameError);
    // Nullary fns are the way to share data with statements.
    let report = check(&theory("    fn xs() -> List<Nat> = list(1, 2, 3)\n    theorem t: nat_eq(length(xs()), 3)"));
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn theorems_are_proof_objects_not_ordinary_values() {
    rejects_with(include_str!("../../../examples/invalid/theorem_is_not_an_ordinary_value.dlm"), DiagnosticKind::PartialityTypeError);
    rejects_with(&theory("    theorem t: true\n    let n = t + 1"), DiagnosticKind::AccessError);
}

#[test]
fn case_limit_is_enforced_before_evaluation() {
    let report = check(include_str!("../../../examples/invalid/proof_case_limit_exceeded.dlm"));
    assert!(
        report.diagnostics.iter().any(|d| d.message.contains(&format!("the limit is {MAX_PROOF_CASES}"))),
        "{:?}",
        report.diagnostics
    );
}

#[test]
fn axioms_are_visible_to_policy() {
    let source = include_str!("../../../examples/invalid/axiom_theorem_rejected_by_trusted_only.dlm");
    assert!(check(source).ok());
    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&parse_module(source).unwrap());
    assert!(trusted.diagnostics.iter().any(|d| d.kind == DiagnosticKind::TrustTaintError));
    // Axioms may quantify over any ordinary type, including records.
    let report = check(&theory("    record P(x: Nat)\n    axiom p_refl(p: P): nat_eq(field(p, x), field(p, x))"));
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn statement_syntax() {
    for bad in [
        "    theorem : true",
        "    theorem t true",
        "    theorem t() : true",
        "    theorem t(n) below 3: true",
        "    theorem t(n: Nat) below 0: true",
        "    theorem t(n: Nat) tested 3: true",
        "    axiom a(n: Nat) below 3: true",
        "    conjecture c(n: Nat): true",
        "    conjecture c: true",
        "    theorem t(n: Nat, n: Nat) below 3: true",
    ] {
        let diagnostics = parse_module(&theory(bad)).expect_err(bad);
        assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::ParseError), "{bad}: {diagnostics:?}");
    }
}

#[test]
fn runtime_statement_evaluation_api() {
    let module = parse_module(&theory("    theorem t(n: Nat, b: Bool) below 3: or(b, n > 1)")).expect("parse");
    let dlm_core::ModuleItem::Theory(theory_decl) = &module.items[0] else { panic!("theory") };
    let dlm_core::TheoryItem::Statement(item) = &theory_decl.items[1] else { panic!("statement") };
    let outcome = Runtime::new()
        .evaluate_statement("T", &[], &[], &item.params, 3, &item.body)
        .expect("evaluate");
    // n = 0, b = false is the first assignment and already refutes it.
    assert_eq!(outcome.cases, 1);
    assert_eq!(outcome.counterexample, Some(vec![("n".to_string(), "0".to_string()), ("b".to_string(), "false".to_string())]));
}
