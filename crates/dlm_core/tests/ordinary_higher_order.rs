//! v0.75 — lambdas, function values, higher-order functions, closures and
//! lexicographic recursion measures.

use dlm_core::{parse_module, CheckPolicy, CheckReport, Checker, DiagnosticKind, Passport, Runtime, TrustLevel, TypeKind};

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
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing passport {name}"))
}

fn theory(body: &str) -> String {
    format!("module demo\ntheory T {{\n{body}\n}}\n")
}

fn rejects_with(source: &str, kind: DiagnosticKind) {
    let report = check(source);
    assert!(report.diagnostics.iter().any(|d| d.kind == kind), "expected {kind:?} for\n{source}\ngot {:?}", report.diagnostics);
}

fn value_type(report: &CheckReport, name: &str) -> TypeKind {
    passport(report, name).ty.clone()
}

#[test]
fn higher_order_example_runs() {
    let source = include_str!("../../../examples/valid/ordinary_higher_order.dlm");
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(value_type(&report, "HigherOrder.inc"), TypeKind::TypedValue { of_type: "Fn<Nat,Nat>".into() });
    assert_eq!(value_type(&report, "HigherOrder.curry"), TypeKind::TypedValue { of_type: "Fn<Nat,Fn<Nat,Nat>>".into() });
    assert_eq!(value_type(&report, "HigherOrder.fs"), TypeKind::ListValue { item: "Fn<Nat,Nat>".into(), len: 4 });
    assert_eq!(
        passport(&report, "HigherOrder.twice").ty,
        TypeKind::FunctionContract { name: "twice".into(), status: "verified".into() },
        "calling a function-typed parameter keeps a higher-order fn pure and total"
    );
    assert_eq!(
        run(source),
        vec!["2", "6", "12", "[11, 12, 13]", "6", "[101, 200, 105, 300]", "1024", "42", "4", "fun(n)"]
    );
}

#[test]
fn lambdas_go_through_the_core_lambda_term() {
    let report = check(&theory("    let k = 7\n    fn mk(n: Nat) -> Fn<Nat, Nat> = fun(m: Nat, m + n)\n    let f = fun(x: Nat, x + k)"));
    assert!(report.ok(), "{:?}", report.diagnostics);
    let f = passport(&report, "T.f").to_string();
    assert!(f.contains("lambda:captures=[]:fingerprint=fnv64:"), "{f}");
    // Theory-level values are global, not captures; enclosing parameters are.
    let module = parse_module(&theory("    fn mk(n: Nat) -> Fn<Nat, Nat> = fun(m: Nat, m + n)\n    let g = mk(1)")).unwrap();
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn closures_capture_lexically() {
    let source = theory(
        "    let x = 100\n\
         fn shadow(x: Nat) -> Nat = x\n\
         let reads_global = fun(n: Nat, n + x)\n\
         fn adder(k: Nat) -> Fn<Nat, Nat> = fun(n: Nat, n + k)\n\
         fn apply_with(f: Fn<Nat, Nat>, k: Nat) -> Nat = f(k)\n\
         let a = adder(1)\n\
         print_decimal(apply_with(a, 10))\n\
         print_decimal(apply_with(reads_global, 1))\n\
         print_decimal(shadow(5))",
    );
    // `a` captured k = 1 at creation; the callee's own `k = 10` does not leak in.
    assert_eq!(run(&source), vec!["11", "101", "5"]);
}

#[test]
fn function_values_must_be_pure_and_total() {
    rejects_with(include_str!("../../../examples/invalid/lambda_must_be_pure.dlm"), DiagnosticKind::FunctionContractError);
    rejects_with(include_str!("../../../examples/invalid/effectful_fn_as_value.dlm"), DiagnosticKind::FunctionContractError);
    rejects_with(&theory("    let f = fun(n: Nat, n + read_nat())"), DiagnosticKind::FunctionContractError);
    rejects_with(
        &theory("    fn needs(n: Nat) -> Nat = n + read_nat()\n    let ys = map(list(1), needs)"),
        DiagnosticKind::FunctionContractError,
    );
    // Effectful functions remain callable directly.
    let report = check(&theory("    fn shout(n: Nat) -> Text = print_decimal(n)\n    let r = shout(3)"));
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn function_types_are_checked_exactly() {
    rejects_with(include_str!("../../../examples/invalid/function_value_type_mismatch.dlm"), DiagnosticKind::FunctionTermError);
    rejects_with(include_str!("../../../examples/invalid/call_non_function_value.dlm"), DiagnosticKind::FunctionTermError);
    rejects_with(&theory("    let f = fun(n: Nat, n)\n    let y = f(true)"), DiagnosticKind::FunctionTermError);
    rejects_with(&theory("    let f = fun(n: Nat, n)\n    let y = f(1, 2)"), DiagnosticKind::FunctionTermError);
    rejects_with(&theory("    let ys = map(list(1), fun(b: Bool, 1))"), DiagnosticKind::TraversalError);
    rejects_with(&theory("    let ys = map(list(1), 5)"), DiagnosticKind::TraversalError);
    rejects_with(&theory("    let r = case(inl(1, Bool), fun(n: Nat, n), fun(b: Bool, b))"), DiagnosticKind::StructuralEliminationError);
}

#[test]
fn taint_of_function_values_is_visible() {
    rejects_with(include_str!("../../../examples/invalid/tainted_lambda_cannot_be_mapped.dlm"), DiagnosticKind::FunctionContractError);
    let source = theory("    let t = fun(n: Nat, n + axiom_nat())\n    let y = t(1)\n    fn apply(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(x)\n    let z = apply(t, 2)");
    let report = check(&source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(passport(&report, "T.t").trust, TrustLevel::Axiom);
    assert_eq!(passport(&report, "T.y").trust, TrustLevel::Axiom);
    assert_eq!(passport(&report, "T.z").trust, TrustLevel::Axiom);
    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_module(&parse_module(&source).unwrap());
    assert!(trusted.diagnostics.iter().any(|d| d.kind == DiagnosticKind::TrustTaintError));
}

#[test]
fn function_bindings_may_not_shadow_builtins() {
    rejects_with(include_str!("../../../examples/invalid/function_binding_shadows_builtin.dlm"), DiagnosticKind::NameError);
    rejects_with(&theory("    fn g(print_text: Fn<Nat, Nat>) -> Nat = 1"), DiagnosticKind::NameError);
    rejects_with(&theory("    let h = fun(list: Fn<Nat, Nat>, 1)"), DiagnosticKind::NameError);
    rejects_with(&theory("    fn g(g: Nat) -> Nat = g"), DiagnosticKind::NameError);
    // Non-function bindings with builtin names keep their old behavior.
    assert!(check(&theory("    let quote_count = 1")).ok());
}

#[test]
fn recursion_cannot_escape_through_function_values() {
    rejects_with(include_str!("../../../examples/invalid/recursion_inside_lambda.dlm"), DiagnosticKind::RecursionBoundaryError);
    rejects_with(
        &theory("    fn f(n: Nat) -> Fn<Nat, Nat> = if_then_else(nat_eq(n, 0), fun(k: Nat, k), f)"),
        DiagnosticKind::RecursionBoundaryError,
    );
    // A lambda parameter may not shadow the recursive function itself.
    rejects_with(
        &theory("    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, fold(list(1), 0, fun(f: Nat, x: Nat, f + x)) + f(pred(n)))"),
        DiagnosticKind::RecursionBoundaryError,
    );
}

#[test]
fn lexicographic_measures() {
    let source = include_str!("../../../examples/valid/ordinary_lexicographic.dlm");
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        passport(&report, "Lexicographic.ack.recursion").ty,
        TypeKind::RecursionScheme { name: "ack".into(), measure: "lexicographic".into(), status: "verified_well_founded".into() }
    );
    assert_eq!(run(source), vec!["9", "61", "20"]);

    for accepted in [
        "    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(m, 0), 0, f(pred(m), n + 5))",
        "    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(n, 0), 0, f(m, pred(n)))",
        "    fn f(xs: List<Nat>, n: Nat) -> Nat decreasing (xs, n) = if_then_else(is_empty(xs), n, if_then_else(n > 0, f(xs, pred(n)), f(tail(xs), 10)))",
    ] {
        let report = check(&theory(accepted));
        assert!(report.ok(), "{accepted}: {:?}", report.diagnostics);
    }
    for rejected in [
        include_str!("../../../examples/invalid/lexicographic_measure_not_decreasing.dlm").to_string(),
        theory("    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = f(m, pred(n))"),
        theory("    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(n, 0), 0, f(m, n))"),
        theory("    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(m, 0), 0, f(m, pred(m)))"),
    ] {
        rejects_with(&rejected, DiagnosticKind::RecursionBoundaryError);
    }
    assert!(parse_module(&theory("    fn f(m: Nat) -> Nat decreasing (m, m) = 1")).is_err());
    assert!(parse_module(&theory("    fn f(m: Nat) -> Nat decreasing (m, k) = 1")).is_err());
}

#[test]
fn lambda_syntax() {
    let module = parse_module(&theory("    let f = fun(g: Fn<Nat, Nat>, x: Nat, g(x))")).expect("Fn type with comma inside lambda params");
    let items = format!("{:?}", module.items);
    assert!(items.contains("Lambda"), "{items}");
    for bad in [
        "    let f = fun(1)",
        "    let f = fun(x: Nat)",
        "    let f = fun(x: Nat, y: Nat)",
        "    let f = fun(x: Nat, x, x)",
        "    let f = fun(x: Nat, x: Nat, x)",
        "    let f = fun(x: Any_thing, x)",
    ] {
        let source = theory(bad);
        match parse_module(&source) {
            Err(diagnostics) => assert!(diagnostics.iter().all(|d| d.kind == DiagnosticKind::ParseError), "{bad}"),
            Ok(module) => assert!(!Checker::new().check_module(&module).ok(), "{bad} must be rejected"),
        }
    }
}
