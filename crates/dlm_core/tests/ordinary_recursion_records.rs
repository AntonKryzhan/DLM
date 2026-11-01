//! v0.74 — well-founded recursion, records and sums in `.dlm` syntax.

use dlm_core::{
    parse_module, CheckPolicy, CheckReport, Checker, DiagnosticKind, Passport, Runtime, TrustLevel, TypeKind,
    DEFAULT_RECURSION_FUEL,
};

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn run(source: &str) -> Vec<String> {
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "check failed: {:?}", report.diagnostics);
    Runtime::new().run_module(&module).expect("runtime").output
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report
        .inferred
        .iter()
        .find(|(n, _)| n == name)
        .map(|(_, p)| p)
        .unwrap_or_else(|| panic!("missing passport {name}"))
}

fn theory(body: &str) -> String {
    format!("module demo\ntheory T {{\n{body}\n}}\n")
}

fn rejects_with(source: &str, kind: DiagnosticKind) {
    let report = check(source);
    assert!(report.diagnostics.iter().any(|d| d.kind == kind), "expected {kind:?} for\n{source}\ngot {:?}", report.diagnostics);
}

#[test]
fn recursion_example_is_verified_well_founded_and_runs() {
    let source = include_str!("../../../examples/valid/ordinary_recursion.dlm");
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for (name, measure) in [
        ("fact", "nat_decreasing"),
        ("sum_to", "nat_decreasing"),
        ("halve_steps", "nat_decreasing"),
        ("total", "structural_subterm"),
        ("count_down", "nat_decreasing"),
    ] {
        assert_eq!(
            passport(&report, &format!("Recursion.{name}.recursion")).ty,
            TypeKind::RecursionScheme {
                name: name.to_string(),
                measure: measure.to_string(),
                status: "verified_well_founded".to_string()
            }
        );
        assert_eq!(
            passport(&report, &format!("Recursion.{name}")).ty,
            TypeKind::FunctionContract { name: name.to_string(), status: "verified".to_string() }
        );
    }
    assert_eq!(run(source), vec!["2432902008176640000", "5050", "4", "12", "[5, 4, 3, 2, 1]", "5050"]);
}

#[test]
fn accepted_guard_and_decrease_forms() {
    for body in [
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(pred(n)))",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(0, n), 0, f(pred(n)))",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(n > 0, f(pred(n)), 0)",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(n > 3, f(monus(n, 3)), n)",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(not(nat_eq(n, 0)), f(pred(n)), 0)",
        "    fn f(n: Nat, a: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), a, f(pred(n), a + n))",
        "    fn f(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, f(tail(xs)))",
        "    fn f(xs: Sequence<Nat>) -> Nat decreasing xs = if_then_else(length(xs) > 0, f(tail(xs)), 0)",
        "    fn f(xs: List<Nat>) -> Nat decreasing xs = if_then_else(not(is_empty(xs)), f(tail(xs)), 0)",
    ] {
        let report = check(&theory(body));
        assert!(report.ok(), "{body}: {:?}", report.diagnostics);
    }
}

#[test]
fn rejected_recursion_forms() {
    for body in [
        // no measure
        "    fn f(n: Nat) -> Nat = if_then_else(nat_eq(n, 0), 0, f(pred(n)))",
        // unguarded
        "    fn f(n: Nat) -> Nat decreasing n = f(pred(n))",
        // recursive call in the zero branch
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), f(pred(n)), 0)",
        // guard on another parameter
        "    fn f(n: Nat, m: Nat) -> Nat decreasing n = if_then_else(nat_eq(m, 0), 0, f(pred(n), m))",
        // not decreasing / increasing / monus by zero
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(n))",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(n + 1))",
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(monus(n, 0)))",
        // list without tail
        "    fn f(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, f(xs))",
        // recursion through a traversal or case argument
        "    fn f(xs: List<Nat>) -> List<Nat> decreasing xs = if_then_else(is_empty(xs), xs, map(xs, f))",
        // measure of a type that cannot decrease
        "    fn f(b: Bool) -> Nat decreasing b = if_then_else(b, 0, f(b))",
    ] {
        rejects_with(&theory(body), DiagnosticKind::RecursionBoundaryError);
    }
}

#[test]
fn fuel_is_explicit_operational_bound() {
    let report = check(include_str!("../../../examples/invalid/recursion_fuel_exhausted.dlm"));
    assert!(report.ok(), "fuel exhaustion is a runtime boundary, not a type error: {:?}", report.diagnostics);
    let module = parse_module(include_str!("../../../examples/invalid/recursion_fuel_exhausted.dlm")).unwrap();
    let err = Runtime::new().run_module(&module).expect_err("fuel 3 cannot reach depth 10");
    assert!(err.message.contains("recursion fuel exhausted"), "{}", err.message);

    let explicit = check(&theory(
        "    fn f(n: Nat) -> Nat decreasing n fuel 64 = if_then_else(nat_eq(n, 0), 0, f(pred(n)))\n    let x = f(3)",
    ));
    assert!(passport(&explicit, "T.x").to_string().contains("recursion=verified_well_founded:fuel=64"));
    rejects_with(
        &theory(&format!(
            "    fn f(n: Nat) -> Nat decreasing n fuel {} = if_then_else(nat_eq(n, 0), 0, f(pred(n)))",
            dlm_core::MAX_RECURSION_FUEL + 1
        )),
        DiagnosticKind::RecursionBoundaryError,
    );
    let default = check(&theory(
        "    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(pred(n)))\n    let x = f(3)",
    ));
    assert!(passport(&default, "T.x").to_string().contains(&format!("fuel={DEFAULT_RECURSION_FUEL}")));
}

#[test]
fn deep_recursion_runs_on_the_runtime_stack_not_the_caller_stack() {
    // Test threads have a small stack; the runtime uses its own large one.
    let depth = DEFAULT_RECURSION_FUEL - 1;
    let source = theory(&format!(
        "    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))\n    print_decimal(sum_to({depth}))"
    ));
    let expected = (depth as u128) * (depth as u128 + 1) / 2;
    assert_eq!(run(&source), vec![expected.to_string()]);
}

#[test]
fn effectful_or_tainted_recursion_is_downgraded_not_hidden() {
    let report = check(&theory(
        "    fn tainted(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), axiom_nat(), tainted(pred(n)))\n\
         let x = tainted(2)",
    ));
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        passport(&report, "T.tainted.recursion").ty,
        TypeKind::RecursionScheme { name: "tainted".into(), measure: "nat_decreasing".into(), status: "downgraded".into() }
    );
    assert_eq!(passport(&report, "T.x").trust, TrustLevel::Axiom);
    rejects_with(
        &theory(
            "    fn tainted(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), axiom_nat(), tainted(pred(n)))\n\
             let xs = map(list(1), tainted)",
        ),
        DiagnosticKind::FunctionContractError,
    );
}

#[test]
fn records_and_sums_example_runs() {
    let source = include_str!("../../../examples/valid/ordinary_records_sums.dlm");
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert!(matches!(&passport(&report, "Geometry.Point").ty, TypeKind::RecordType { name, .. } if name == "Point"));
    assert!(matches!(&passport(&report, "Geometry.seg").ty, TypeKind::RecordTerm { name, .. } if name == "Segment"));
    assert_eq!(
        passport(&report, "Geometry.values").ty,
        TypeKind::ListValue { item: "SumType<Nat+Bool>".into(), len: 3 }
    );
    assert_eq!(
        run(source),
        vec![
            "Segment{from: Point{x: 0, y: 0}, to: Point{x: 3, y: 4}}",
            "7",
            "Point{x: 5, y: 5}",
            "[inl(7), inr(true), inr(false)]",
            "[7, 1, 0]",
            "true",
        ]
    );
}

#[test]
fn record_rules() {
    rejects_with(include_str!("../../../examples/invalid/record_field_type_mismatch.dlm"), DiagnosticKind::StructuralTypeError);
    rejects_with(include_str!("../../../examples/invalid/recursive_record_rejected.dlm"), DiagnosticKind::StructuralTypeError);
    rejects_with(&theory("    record P(x: Nat)\n    let p = P(1, 2)"), DiagnosticKind::StructuralTypeError);
    rejects_with(&theory("    record P(x: Nat)\n    let v = field(P(1), y)"), DiagnosticKind::StructuralTypeError);
    rejects_with(&theory("    record P(x: Q)"), DiagnosticKind::NameError);
    rejects_with(&theory("    fn f(p: Missing) -> Nat = 1"), DiagnosticKind::NameError);
    rejects_with(&theory("    let n = none(Missing)"), DiagnosticKind::NameError);
    rejects_with(&theory("    record P(x: Nat)\n    fn P(n: Nat) -> Nat = n"), DiagnosticKind::NameError);
    assert!(parse_module(&theory("    record lower(x: Nat)")).is_err());
    assert!(parse_module(&theory("    record List(x: Nat)")).is_err());
    assert!(parse_module(&theory("    record P(x: Nat, x: Nat)")).is_err());
}

#[test]
fn records_are_ordinary_values_with_canonical_types() {
    let report = check(&theory(
        "    record P(x: Nat)\n    let ps = list(P(1), P(2))\n    let o = some(P(3))\n    let s = inl(P(4), Nat)",
    ));
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.ps").ty, TypeKind::ListValue { item: "Record<P>".into(), len: 2 });
    assert_eq!(passport(&report, "T.o").ty, TypeKind::OptionValue { kind: "some".into(), item: "Record<P>".into() });
}

#[test]
fn sum_rules() {
    rejects_with(include_str!("../../../examples/invalid/case_branch_types_differ.dlm"), DiagnosticKind::StructuralEliminationError);
    rejects_with(
        &theory("    fn a(n: Nat) -> Nat = n\n    let r = case(inl(1, Bool), a, a)"),
        DiagnosticKind::StructuralEliminationError,
    );
    rejects_with(&theory("    fn a(n: Nat) -> Nat = n\n    let r = case(5, a, a)"), DiagnosticKind::StructuralEliminationError);
    rejects_with(&theory("    let p = prove(7 > 0)\n    let s = inl(p, Nat)"), DiagnosticKind::StructuralTypeError);
    rejects_with(&theory("    let x = list(inl(1, Bool), inl(true, Nat))"), DiagnosticKind::SequenceTypeError);
}

#[test]
fn taint_flows_through_records_and_sums() {
    let source = theory(
        "    record P(x: Nat)\n    let p = P(axiom_nat())\n    let s = inr(axiom_nat(), Bool)\n    fn id(n: Nat) -> Nat = n\n    fn flag(b: Bool) -> Nat = if_then_else(b, 1, 0)\n    let c = case(s, flag, id)",
    );
    let report = check(&source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.p").trust, TrustLevel::Axiom);
    assert_eq!(passport(&report, "T.s").trust, TrustLevel::Axiom);
    assert_eq!(passport(&report, "T.c").trust, TrustLevel::Axiom);
    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&parse_module(&source).unwrap());
    assert!(trusted.diagnostics.iter().any(|d| d.kind == DiagnosticKind::TrustTaintError));
}
