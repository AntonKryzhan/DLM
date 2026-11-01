//! v0.73 — Ordinary mathematics surface: `.dlm` syntax wired to the Stage 2
//! core layers (functions, contracts, Option/Result, List/Sequence, products,
//! bounded map/fold).

use dlm_core::{
    parse_module, CheckPolicy, CheckReport, Checker, DiagnosticKind, Passport, Runtime, TrustLevel, TypeKind,
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
        .unwrap_or_else(|| panic!("missing passport {name}; have {:?}", report.inferred.iter().map(|(n, _)| n).collect::<Vec<_>>()))
}

fn rejects_with(source: &str, kind: DiagnosticKind) {
    let report = check(source);
    assert!(
        report.diagnostics.iter().any(|d| d.kind == kind),
        "expected {kind:?}, got {:?}",
        report.diagnostics
    );
}

#[test]
fn functions_check_with_verified_contracts_and_execute() {
    let source = include_str!("../../../examples/valid/ordinary_functions.dlm");
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for name in ["double", "square", "is_big", "clamp", "distance", "same"] {
        let contract = passport(&report, &format!("Arith.{name}"));
        assert_eq!(
            contract.ty,
            TypeKind::FunctionContract { name: name.to_string(), status: "verified".to_string() },
            "{name}"
        );
    }
    assert_eq!(run(source), vec!["42", "144", "10", "7", "41", "true"]);
}

#[test]
fn partiality_collections_and_products_examples_execute() {
    assert_eq!(
        run(include_str!("../../../examples/valid/ordinary_partiality.dlm")),
        vec!["some(8)", "none", "8", "7", "ok(5)", "err(500)", "5", "0", "[true, true, true, true]"]
    );
    assert_eq!(
        run(include_str!("../../../examples/valid/ordinary_collections.dlm")),
        vec![
            "[2, 4, 6, 8]",
            "20",
            "[some(1), some(2), some(3), some(4)]",
            "4",
            "some(3)",
            "none",
            "none",
            "0",
            "seq[10, 20, 30]",
            "60",
            "[some(1), none]",
        ]
    );
    assert_eq!(
        run(include_str!("../../../examples/valid/ordinary_products.dlm")),
        vec!["(7, true)", "(true, 7)", "7", "3", "[(1, 2), (3, 4)]"]
    );
}

#[test]
fn option_branch_does_not_change_type_so_some_and_none_mix() {
    let report = check(
        r#"
module demo
theory T {
    let xs = list(some(1), none(Nat), some(3))
    let nested = list(list(1), list(2, 3))
}
"#,
    );
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.xs").ty, TypeKind::ListValue { item: "Option<Nat>".into(), len: 3 });
    assert_eq!(passport(&report, "T.nested").ty, TypeKind::ListValue { item: "List<Nat>".into(), len: 2 });
}

#[test]
fn static_map_keeps_length_and_records_core_traversal_report() {
    let report = check(
        r#"
module demo
theory T {
    fn double(n: Nat) -> Nat = n + n
    let ys = map(list(1, 2, 3), double)
}
"#,
    );
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ys = passport(&report, "T.ys");
    assert_eq!(ys.ty, TypeKind::ListValue { item: "Nat".into(), len: 3 });
    let history = ys.to_string();
    assert!(history.contains("traversal:map:double:verified_bounded:fingerprint="), "{history}");
}

#[test]
fn collections_of_unknown_length_are_runtime_bounded() {
    let source = r#"
module demo
theory T {
    fn add(acc: Nat, x: Nat) -> Nat = acc + x
    fn double(n: Nat) -> Nat = n + n
    fn sum(xs: List<Nat>) -> Nat = fold(xs, 0, add)
    fn doubled(xs: List<Nat>) -> List<Nat> = map(xs, double)
    let s = sum(doubled(list(1, 2, 3)))
    print_decimal(s)
}
"#;
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.sum").ty, TypeKind::FunctionContract { name: "sum".into(), status: "verified".into() });
    assert_eq!(run(source), vec!["12"]);
}

#[test]
fn recursion_and_forward_references_are_rejected() {
    rejects_with(include_str!("../../../examples/invalid/fn_recursion_rejected.dlm"), DiagnosticKind::RecursionBoundaryError);
    // A function may only call previously declared functions, so mutual
    // recursion cannot be written.
    rejects_with(
        r#"
module demo
theory T {
    fn a(n: Nat) -> Nat = b(n)
    fn b(n: Nat) -> Nat = a(n)
}
"#,
        DiagnosticKind::NameError,
    );
    // Recursion through a traversal argument is recursion too.
    rejects_with(
        r#"
module demo
theory T {
    fn f(xs: List<Nat>) -> List<Nat> = map(xs, f)
}
"#,
        DiagnosticKind::RecursionBoundaryError,
    );
}

#[test]
fn typing_errors_use_the_matching_stage2_diagnostics() {
    rejects_with(include_str!("../../../examples/invalid/fn_return_type_mismatch.dlm"), DiagnosticKind::FunctionTermError);
    rejects_with(include_str!("../../../examples/invalid/fn_argument_type_mismatch.dlm"), DiagnosticKind::FunctionTermError);
    rejects_with(include_str!("../../../examples/invalid/list_mixed_item_types.dlm"), DiagnosticKind::SequenceTypeError);
    rejects_with(include_str!("../../../examples/invalid/if_branches_must_match.dlm"), DiagnosticKind::StandardPreludeError);
    rejects_with(include_str!("../../../examples/invalid/option_rejects_static_proof.dlm"), DiagnosticKind::PartialityTypeError);
    rejects_with(include_str!("../../../examples/invalid/fold_step_signature_mismatch.dlm"), DiagnosticKind::TraversalError);
    rejects_with(include_str!("../../../examples/invalid/fn_shadows_builtin.dlm"), DiagnosticKind::NameError);
}

#[test]
fn effectful_or_tainted_functions_cannot_be_traversed() {
    rejects_with(
        include_str!("../../../examples/invalid/map_requires_verified_contract.dlm"),
        DiagnosticKind::FunctionContractError,
    );
    rejects_with(
        include_str!("../../../examples/invalid/map_rejects_axiom_tainted_function.dlm"),
        DiagnosticKind::FunctionContractError,
    );
}

#[test]
fn effectful_and_partial_functions_are_downgraded_but_callable() {
    let source = r#"
module demo
theory T {
    fn shout(n: Nat) -> Text = print_decimal(n)
    fn checked(n: Nat) -> Nat = n
    let said = shout(5)
}
"#;
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.shout").ty, TypeKind::FunctionContract { name: "shout".into(), status: "downgraded".into() });
    assert!(passport(&report, "T.shout").to_string().contains("purity=effectful"));
    assert_eq!(run(source), vec!["5"]);

    let partial = check(
        r#"
module demo
theory T {
    fn guarded(n: Nat) -> Nat = n + monus(1, 1)
    fn needs_input(n: Nat) -> Nat = n + read_nat()
}
"#,
    );
    assert!(partial.ok(), "{:?}", partial.diagnostics);
    let needs = passport(&partial, "T.needs_input").to_string();
    assert!(needs.contains("totality=partial"), "{needs}");
    assert!(needs.contains("downgraded"), "{needs}");
}

#[test]
fn taint_flows_through_functions_and_collections() {
    let source = include_str!("../../../examples/invalid/ordinary_axiom_fn_rejected_by_trusted_only.dlm");
    let report = check(source);
    assert!(report.ok(), "research policy allows visible axiom taint: {:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.x").trust, TrustLevel::Axiom);
    assert_eq!(passport(&report, "T.tainted").ty, TypeKind::FunctionContract { name: "tainted".into(), status: "downgraded".into() });

    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&parse_module(source).unwrap());
    assert!(trusted.diagnostics.iter().any(|d| d.kind == DiagnosticKind::TrustTaintError));

    let list = check(
        r#"
module demo
theory T {
    let xs = list(1, axiom_nat())
    let o = some(axiom_nat())
}
"#,
    );
    assert!(list.ok(), "{:?}", list.diagnostics);
    assert_eq!(passport(&list, "T.xs").trust, TrustLevel::Axiom);
    assert_eq!(passport(&list, "T.o").trust, TrustLevel::Axiom);
}

#[test]
fn parameters_are_scoped_to_their_function() {
    rejects_with(
        r#"
module demo
theory T {
    fn id(n: Nat) -> Nat = n
    let leaked = n
}
"#,
        DiagnosticKind::NameError,
    );
    // Parameters shadow theory values inside the body only.
    let source = r#"
module demo
theory T {
    let n = 100
    fn id(n: Nat) -> Nat = n
    print_decimal(id(1))
    print_decimal(n)
}
"#;
    assert_eq!(run(source), vec!["1", "100"]);
}

#[test]
fn malformed_fn_declarations_are_parse_errors() {
    for source in [
        "module m\ntheory T {\n    fn f(n: Nat) = n\n}\n",
        "module m\ntheory T {\n    fn f(n) -> Nat = n\n}\n",
        "module m\ntheory T {\n    fn f(n: Nat) -> Nat\n}\n",
        "module m\ntheory T {\n    fn f(n: Nat, n: Nat) -> Nat = n\n}\n",
        "module m\ntheory T {\n    fn f(n: any) -> Nat = 1\n}\n",
    ] {
        let diagnostics = parse_module(source).expect_err(source);
        assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::ParseError), "{source}: {diagnostics:?}");
    }
    // v0.74: a capitalized unknown type is a record reference, resolved by the checker.
    rejects_with("module m\ntheory T {\n    fn f(n: Any) -> Nat = 1\n}\n", DiagnosticKind::NameError);
}

#[test]
fn runtime_semantics_of_nat_and_partial_operations() {
    let source = r#"
module demo
theory T {
    print_decimal(monus(3, 10))
    print_decimal(pred(0))
    print_symbolic(index(list(5, 6), 1))
    print_symbolic(index(sequence(5, 6), 2))
    print_symbolic(or(true, false))
    print_symbolic(and(false, true))
    print_decimal(if_then_else(false, 1, 2))
}
"#;
    assert_eq!(run(source), vec!["0", "0", "some(6)", "none", "true", "false", "2"]);

    let overflow = r#"
module demo
theory T {
    let big = mul(340282366920938463463374607431768211455, 2)
}
"#;
    let module = parse_module(overflow).unwrap();
    assert!(Checker::new().check_module(&module).ok());
    assert!(Runtime::new().run_module(&module).is_err(), "Nat overflow must be a runtime error, not wraparound");
}
