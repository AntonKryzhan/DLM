//! v0.77 — `by normalize` / `by induction` / `using` lemmas in `.dlm`.

use dlm_core::{
    audit_certificate_against_theorem, parse_module, CheckPolicy, CheckReport, Checker, DiagnosticKind, Passport,
    ProofCertificateStatus, TrustLevel, TypeKind,
};

const EXAMPLE: &str = include_str!("../../../examples/valid/symbolic_proofs.dlm");

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing passport {name}"))
}

fn rejects_with(source: &str, kind: DiagnosticKind, needle: &str) {
    let report = check(source);
    assert!(
        report.diagnostics.iter().any(|d| d.kind == kind && d.message.contains(needle)),
        "expected {kind:?} containing `{needle}`, got {:?}",
        report.diagnostics
    );
}

#[test]
fn example_proves_unbounded_theorems() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for (name, proposition) in [
        ("add_zero", "forall n: Nat. nat_eq((n + 0), n)"),
        ("gauss", "forall n: Nat. nat_eq(mul(2, sum_to(n)), mul(n, (n + 1)))"),
        ("add_spec", "forall a: Nat, b: Nat. nat_eq(add(a, b), (a + b))"),
        ("twice_inc", "forall n: Nat. nat_eq(twice(fun(x: Nat, (x + 1)), n), (n + 2))"),
    ] {
        let theorem = passport(&report, &format!("Symbolic.{name}"));
        assert_eq!(theorem.ty, TypeKind::Theorem { name: name.into(), proposition: proposition.into() });
        assert!(theorem.trust < TrustLevel::Axiom);
    }
    assert_eq!(report.certificates.len(), 10);
    for certificate in &report.certificates {
        assert_eq!(certificate.status, ProofCertificateStatus::Checked, "{}", certificate.theorem_name);
        let theorem = passport(&report, &format!("Symbolic.{}", certificate.theorem_name));
        assert!(audit_certificate_against_theorem(certificate, theorem, 0).verified());
    }
}

#[test]
fn induction_goes_through_the_core_induction_objects() {
    let report = check(EXAMPLE);
    let gauss = passport(&report, "Symbolic.gauss").to_string();
    for event in [
        "induction:nat:scheme:P_gauss",
        "induction:nat:base:P_gauss(0)",
        "induction:nat:step:forall n:Nat. P_gauss(n) -> P_gauss(succ(n))",
        "induction:nat:proof:forall n:Nat. P_gauss(n)",
        "induction:instantiate:P_gauss(n) := nat_eq(mul(2, sum_to(n)), mul(n, (n + 1)))",
        "proof:symbolic:step:normalize+hypothesis_linear_combination",
    ] {
        assert!(gauss.contains(event), "missing `{event}` in {gauss}");
    }
    let odd = passport(&report, "Symbolic.odd_squares").to_string();
    assert!(odd.contains("proof:symbolic:step:normalize:"), "IH used as a ground rewrite: {odd}");
}

#[test]
fn lemmas_are_recorded_and_their_taint_flows() {
    let report = check(EXAMPLE);
    let spec = passport(&report, "Symbolic.twice_add_spec").to_string();
    assert!(spec.contains("lemma:use:add_spec"), "{spec}");

    let source = include_str!("../../../examples/invalid/axiom_lemma_rejected_by_trusted_only.dlm");
    let research = check(source);
    assert!(research.ok(), "{:?}", research.diagnostics);
    assert_eq!(passport(&research, "T.uses_axiom").trust, TrustLevel::Axiom);
    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&parse_module(source).unwrap());
    assert!(trusted.diagnostics.iter().any(|d| d.kind == DiagnosticKind::TrustTaintError));
}

#[test]
fn false_or_unprovable_statements_are_rejected_with_residuals() {
    rejects_with(
        include_str!("../../../examples/invalid/symbolic_proof_fails.dlm"),
        DiagnosticKind::StatementTheoremError,
        "step case n = n_k + 1 does not normalize to true",
    );
    rejects_with(
        include_str!("../../../examples/invalid/normalize_does_not_assume_positive.dlm"),
        DiagnosticKind::StatementTheoremError,
        "residual: 1 + pred(n) = n",
    );
    rejects_with(
        "module m\ntheory T {\n    theorem all_zero(n: Nat): nat_eq(n, 0) by induction n\n}\n",
        DiagnosticKind::StatementTheoremError,
        "step case",
    );
    rejects_with(
        "module m\ntheory T {\n    theorem one(n: Nat): nat_eq(n, 1) by induction n\n}\n",
        DiagnosticKind::StatementTheoremError,
        "base case n = 0",
    );
}

#[test]
fn only_unconditional_theorems_are_lemmas() {
    rejects_with(
        include_str!("../../../examples/invalid/lemma_must_be_unconditional.dlm"),
        DiagnosticKind::StatementTheoremError,
        "is a bounded theorem",
    );
    rejects_with(
        "module m\ntheory T {\n    conjecture c(n: Nat) tested 5: nat_eq(n, n)\n    theorem t(n: Nat): nat_eq(n, n) by normalize using c\n}\n",
        DiagnosticKind::StatementTheoremError,
        "is a conjecture",
    );
    rejects_with(
        "module m\ntheory T {\n    theorem t(n: Nat): nat_eq(n, n) by normalize using later\n    theorem later(n: Nat): nat_eq(n, n) by normalize\n}\n",
        DiagnosticKind::StatementTheoremError,
        "is not declared before this theorem",
    );
    rejects_with(
        "module m\ntheory T {\n    theorem pos(n: Nat): n + 1 > 0 by normalize\n    theorem t(n: Nat): nat_eq(n, n) by normalize using pos\n}\n",
        DiagnosticKind::StatementTheoremError,
        "not an equation",
    );
    // A closed theorem is a ground lemma.
    let report = check(
        "module m\ntheory T {\n    fn f(n: Nat) -> Nat = mul(n, 0) + 5\n    fn g() -> Nat = 5\n    theorem g_value: nat_eq(g(), 5)\n    theorem t(n: Nat): nat_eq(f(n), g()) by normalize using g_value\n}\n",
    );
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn proof_clause_syntax() {
    rejects_with(
        include_str!("../../../examples/invalid/symbolic_proof_requires_nat_variables.dlm"),
        DiagnosticKind::StatementTheoremError,
        "symbolic proofs support Nat, Int, Bool, Text and List<Nat> variables",
    );
    for bad in [
        "    theorem t(n: Nat): nat_eq(n, n) by magic",
        "    theorem t(n: Nat): nat_eq(n, n) by induction",
        "    theorem t(n: Nat): nat_eq(n, n) by induction m",
        "    theorem t(n: Nat) below 3: nat_eq(n, n) by normalize",
        "    axiom a(n: Nat): nat_eq(n, n) by normalize",
        "    conjecture c(n: Nat) tested 3: nat_eq(n, n) by normalize",
        "    theorem t(n: Nat): nat_eq(n, n) by normalize using 1bad",
    ] {
        let source = format!("module m\ntheory T {{\n{bad}\n}}\n");
        let diagnostics = parse_module(&source).expect_err(bad);
        assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::ParseError), "{bad}: {diagnostics:?}");
    }
    // `by` inside parentheses is not a proof clause.
    assert!(parse_module("module m\ntheory T {\n    theorem t: nat_eq(1, 1)\n}\n").is_ok());
}
