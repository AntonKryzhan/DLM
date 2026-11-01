//! v0.78 — inequalities with hypotheses, `List<Nat>` variables, structural list
//! induction through the core, and the runtime cross-check of symbolic proofs.

use dlm_core::{
    audit_certificate_against_theorem, list_base_case_proposition, list_induction_base_case, list_induction_proof,
    list_induction_scheme, list_induction_step_case, list_step_case_proposition, nat_induction_scheme, parse_module,
    CheckReport, Checker, DiagnosticKind, Passport, ProofCertificateStatus, TrustLevel, TypeKind,
};

const EXAMPLE: &str = include_str!("../../../examples/valid/symbolic_lists_inequalities.dlm");

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing passport {name}"))
}

fn rejects_with(source: &str, needle: &str) {
    let report = check(source);
    assert!(
        report.diagnostics.iter().any(|d| d.kind == DiagnosticKind::StatementTheoremError && d.message.contains(needle)),
        "expected E0909 containing `{needle}`, got {:?}",
        report.diagnostics
    );
}

#[test]
fn example_proves_inequalities_and_list_theorems() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for (name, proposition) in [
        ("fact_positive", "forall n: Nat. (fact(n) > 0)"),
        ("sum_to_bound", "forall n: Nat. not((sum_to(n) > mul(n, n)))"),
        ("len_is_length", "forall xs: List<Nat>. nat_eq(len(xs), length(xs))"),
        ("map_length", "forall xs: List<Nat>. nat_eq(length(inc_all(xs)), length(xs))"),
        ("cons_length", "forall xs: List<Nat>, x: Nat. nat_eq(length(cons(x, xs)), (length(xs) + 1))"),
    ] {
        let theorem = passport(&report, &format!("Lists.{name}"));
        assert_eq!(theorem.ty, TypeKind::Theorem { name: name.into(), proposition: proposition.into() });
        assert!(theorem.trust < TrustLevel::Axiom);
    }
    assert_eq!(report.certificates.len(), 6);
    for certificate in &report.certificates {
        assert_eq!(certificate.status, ProofCertificateStatus::Checked, "{}", certificate.theorem_name);
        let theorem = passport(&report, &format!("Lists.{}", certificate.theorem_name));
        assert!(audit_certificate_against_theorem(certificate, theorem, 0).verified());
    }
}

#[test]
fn inequality_steps_use_nonnegative_combinations() {
    let report = check(EXAMPLE);
    for name in ["fact_positive", "sum_to_bound"] {
        let history = passport(&report, &format!("Lists.{name}")).to_string();
        assert!(history.contains("proof:symbolic:step:normalize+hypothesis_nonnegative_combination"), "{history}");
    }
}

#[test]
fn list_induction_goes_through_the_core_list_objects() {
    let report = check(EXAMPLE);
    let history = passport(&report, "Lists.map_length").to_string();
    for event in [
        "induction:list:scheme:List<Nat>:P_map_length",
        "induction:list:base:P_map_length([])",
        "induction:list:step:forall h:Nat, t:List<Nat>. P_map_length(t) -> P_map_length(cons(h, t))",
        "induction:list:proof:forall xs:List<Nat>. P_map_length(xs)",
        "induction:instantiate:P_map_length(xs) := nat_eq(length(inc_all(xs)), length(xs))",
    ] {
        assert!(history.contains(event), "missing `{event}` in {history}");
    }
}

#[test]
fn every_symbolic_theorem_is_cross_checked_by_the_runtime() {
    let report = check(EXAMPLE);
    for (name, cases) in [("fact_positive", 6), ("len_is_length", 40), ("cons_length", 240)] {
        let history = passport(&report, &format!("Lists.{name}")).to_string();
        let event = format!("crosscheck:runtime:agree:cases={cases}:theorem={name}");
        assert!(history.contains(&event), "missing `{event}` in {history}");
    }
    // v0.77 theorems are cross-checked too.
    let v077 = check(include_str!("../../../examples/valid/symbolic_proofs.dlm"));
    assert!(v077.ok(), "{:?}", v077.diagnostics);
    let gauss = passport(&v077, "Symbolic.gauss").to_string();
    assert!(gauss.contains("crosscheck:runtime:agree:cases=6:theorem=gauss"), "{gauss}");
}

#[test]
fn a_false_axiom_used_as_a_lemma_is_caught_by_the_cross_check() {
    rejects_with(
        include_str!("../../../examples/invalid/false_axiom_caught_by_crosscheck.dlm"),
        "kernel/runtime disagreement: the symbolic kernel proved theorem 'half_zero', but the runtime evaluates it to false at n = 1",
    );
}

#[test]
fn large_grids_are_skipped_and_recorded() {
    let source = "module m\ntheory T {\n    theorem comm(a: Nat, b: Nat, c: Nat, d: Nat, e: Nat, f: Nat): nat_eq(a + b + c + d + e + f, f + e + d + c + b + a) by normalize\n}\n";
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let history = passport(&report, "T.comm").to_string();
    assert!(history.contains("crosscheck:runtime:skipped:cases=46656:theorem=comm"), "{history}");
}

#[test]
fn lists_need_a_proof_clause_and_wrong_inequalities_fail() {
    rejects_with(
        include_str!("../../../examples/invalid/list_theorem_requires_proof.dlm"),
        "only Nat and Bool variables can be enumerated",
    );
    rejects_with(
        include_str!("../../../examples/invalid/inequality_induction_fails.dlm"),
        "base case n = 0 does not normalize to true; residual: -1 >= 0",
    );
    // A true inequality outside the coefficient criterion is not accepted by
    // `by normalize`: n^2 - n has a negative coefficient.
    rejects_with(
        "module m\ntheory T {\n    theorem sq(n: Nat): not(n > mul(n, n)) by normalize\n}\n",
        "residual: -n + n^2 >= 0",
    );
    // ... but induction proves it.
    assert!(check("module m\ntheory T {\n    theorem sq(n: Nat): not(n > mul(n, n)) by induction n\n}\n").ok());
    // Lists of other element types are not supported by symbolic proofs.
    rejects_with(
        "module m\ntheory T {\n    theorem t(xs: List<Bool>): nat_eq(length(xs), length(xs)) by normalize\n}\n",
        "has type List<Bool>; symbolic proofs support Nat, Int, Bool, Text and List<Nat> variables",
    );
    // Wrong list statement: the kernel refuses, nothing reaches the runtime.
    rejects_with(
        "module m\ntheory T {\n    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))\n    theorem t(xs: List<Nat>): nat_eq(length(inc_all(xs)), length(xs) + 1) by induction xs\n}\n",
        "base case xs = []",
    );
}

#[test]
fn list_induction_core_objects_check_their_cases() {
    let scheme = list_induction_scheme("T", "Nat", "P", 1).unwrap();
    assert_eq!(scheme.ty.to_string(), "InductionScheme<List<Nat>,P>");
    let base = Passport::static_proof("T", list_base_case_proposition("P"), &scheme);
    let step = Passport::static_proof("T", list_step_case_proposition("Nat", "P"), &scheme);
    let base_case = list_induction_base_case("T", &scheme, &base, 1).unwrap();
    let step_case = list_induction_step_case("T", &scheme, &step, 1).unwrap();
    let proof = list_induction_proof("T", &scheme, &base_case, &step_case, 1).unwrap();
    assert_eq!(proof.ty, TypeKind::InductionProof { proposition: "forall xs:List<Nat>. P(xs)".into() });

    // Cases are not interchangeable and must match the family.
    assert!(list_induction_base_case("T", &scheme, &step, 1).is_err());
    assert!(list_induction_step_case("T", &scheme, &base, 1).is_err());
    assert!(list_induction_proof("T", &scheme, &step_case, &base_case, 1).is_err());
    let other = list_induction_scheme("T", "Nat", "Q", 1).unwrap();
    assert!(list_induction_proof("T", &other, &base_case, &step_case, 1).is_err());
    // A Nat scheme is not a list scheme.
    let nat = nat_induction_scheme("T", "P", 1).unwrap();
    assert!(list_induction_base_case("T", &nat, &base, 1).is_err());
}
