//! v0.82 — multi-line declarations, `let ... in ...`, `match`, grouping
//! parentheses.

use dlm_core::{parse_expr, parse_module, render_expr, CheckReport, Checker, DiagnosticKind, ExprKind, Runtime};

const EXAMPLE: &str = include_str!("../../../examples/valid/patterns_and_let.dlm");

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn theory(body: &str) -> String {
    format!("module m\ntheory T {{\n{body}\n}}\n")
}

fn run(source: &str) -> Vec<String> {
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    Runtime::new().run_module(&module).expect("run").output
}

fn parse_error(source: &str, needle: &str) {
    let diagnostics = parse_module(source).expect_err(needle);
    assert!(diagnostics.iter().any(|d| d.kind == DiagnosticKind::ParseError && d.message.contains(needle)), "{needle}: {diagnostics:?}");
}

#[test]
fn example_checks_proves_and_runs() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(report.certificates.len(), 6);
    // `clamp_le` needs Nat case splitting on the `match` conditions.
    let clamp = &report.inferred.iter().find(|(n, _)| n == "Patterns.clamp_le").unwrap().1;
    assert!(clamp.to_string().contains("proof:symbolic:normalize:cases[n<=1]"), "{clamp}");
    assert_eq!(run(EXAMPLE), ["10", "120", "55", "8", "42"]);
}

#[test]
fn match_agrees_with_the_explicit_definition() {
    // The kernel proves the desugared `match` equal to a hand-written version,
    // and the runtime cross-check evaluates both.
    let source = theory(
        "    fn pred_m(n: Nat) -> Nat = match n { 0 => 0, succ(k) => k }
    fn len_m(xs: List<Nat>) -> Nat decreasing xs = match xs { [] => 0, cons(_, t) => 1 + len_m(t) }
    fn get_m(o: Option<Nat>) -> Nat = match o { some(x) => x + 1, none => 0 }
    fn sel(n: Nat) -> Nat = match n { 2 => 20, 0 => 0, x => x + 1 }
    theorem pred_same(n: Nat): nat_eq(pred_m(n), pred(n)) by normalize
    theorem len_same(xs: List<Nat>): nat_eq(len_m(xs), length(xs)) by induction xs
    theorem get_none: nat_eq(get_m(none(Nat)), 0)
    theorem get_some(n: Nat): nat_eq(get_m(some(n)), n + 1) by normalize
    theorem sel_values(n: Nat) below 10: nat_eq(sel(n), if_then_else(nat_eq(n, 2), 20, if_then_else(nat_eq(n, 0), 0, n + 1)))",
    );
    let report = check(&source);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for (name, _) in report.inferred.iter().filter(|(n, _)| n.ends_with("_same") || n.ends_with("get_some")) {
        let passport = &report.inferred.iter().find(|(n, _)| n == name).unwrap().1;
        assert!(passport.to_string().contains("crosscheck:runtime:agree"), "{name}");
    }
}

#[test]
fn let_binds_once_and_scopes_lexically() {
    // A let value with an effect is evaluated once.
    let out = run(&theory("    let x = (let v = print_decimal(7) in v)\n    print_decimal(let a = 2 in (let b = a + 1 in mul(a, b)))"));
    assert_eq!(out, ["7", "6"]);
    // Shadowing: the inner binding wins inside, the outer one outside.
    let out = run(&theory("    fn f(n: Nat) -> Nat = (let n = n + 10 in n) + n\n    print_decimal(f(1))"));
    assert_eq!(out, ["12"]);
    // A closure captures a let binding.
    let out = run(&theory("    fn f(n: Nat) -> Nat = let k = n + 1 in fold(list(1, 2), 0, fun(acc: Nat, x: Nat, acc + x + k))\n    print_decimal(f(1))"));
    assert_eq!(out, ["7"]);
    let expr = parse_expr("let x = 1 in let y = x + 1 in y", 1).unwrap();
    assert!(matches!(expr.kind, ExprKind::Let { .. }));
    assert_eq!(render_expr(&expr), "(let x = 1 in (let y = (x + 1) in y))");
    // `in` of a nested let in the value is matched correctly.
    assert_eq!(render_expr(&parse_expr("let x = let y = 1 in y in x", 1).unwrap()), "(let x = (let y = 1 in y) in x)");
    assert_eq!(render_expr(&parse_expr("(1 + 2) + (3)", 1).unwrap()), "((1 + 2) + 3)");
}

#[test]
fn recursion_through_match_and_let_aliases() {
    let ok = theory(
        "    fn halve(n: Nat) -> Nat decreasing n = match n { 0 => 0, 1 => 0, succ(k) => halve(pred(k)) + 1 }
    fn drop2(xs: List<Nat>) -> Nat decreasing xs = match xs { [] => 0, cons(h, t) => h + drop2(tail(t)) }
    fn via_let(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, let m = pred(n) in let j = monus(m, 1) in via_let(j) + 1)",
    );
    let report = check(&ok);
    assert!(report.ok(), "{:?}", report.diagnostics);
    for (body, needle) in [
        ("    fn f(n: Nat) -> Nat decreasing n = match n { 0 => 0, succ(k) => let k = n in f(k) }", "recursive call to 'f' must pass"),
        ("    fn f(n: Nat) -> Nat decreasing n = let n = 5 in f(pred(n))", "shadows a recursive function or a measured parameter"),
        ("    fn f(n: Nat) -> Nat decreasing n = let m = pred(n) in f(m)", "is not guarded"),
    ] {
        let report = check(&theory(body));
        assert!(
            report.diagnostics.iter().any(|d| d.kind == DiagnosticKind::RecursionBoundaryError && d.message.contains(needle)),
            "{body}: {:?}",
            report.diagnostics
        );
    }
    let report = check(include_str!("../../../examples/invalid/let_alias_not_decreasing.dlm"));
    assert!(!report.ok());
}

#[test]
fn match_and_let_syntax_errors() {
    for (body, needle) in [
        ("    fn f(n: Nat) -> Nat = match n { 0 => 1 }", "not exhaustive"),
        ("    fn f(n: Nat) -> Nat = match n { _ => 1, 0 => 2 }", "unreachable after a catch-all"),
        ("    fn f(n: Nat) -> Nat = match n { succ(k) => 1, 3 => 2, 0 => 0 }", "succ(_) already covers"),
        ("    fn f(n: Nat) -> Nat = match n { 0 => 1, [] => 2, _ => 3 }", "mixes Nat and List patterns"),
        ("    fn f(n: Nat) -> Nat = match n { 0 => 1, 00 => 2, _ => 3 }", "duplicate pattern"),
        ("    fn f(n: Nat) -> Nat = match n { 0 -> 1 }", "has no `=>`"),
        ("    fn f(n: Nat) -> Nat = match n { cons(a) => 1, _ => 2 }", "cons pattern needs two variables"),
        ("    fn f(n: Nat) -> Nat = match n { 0 => 1, succ(__k) => 2 }", "invalid pattern variable"),
        ("    fn f(n: Nat) -> Nat = match n 0 => 1", "match without `{`"),
        ("    fn f(n: Nat) -> Nat = let x = 1 x", "has no `in`"),
        ("    fn f(n: Nat) -> Nat = let __x = 1 in n", "reserved"),
        ("    fn f(n: Nat) -> Nat = __match_head(list(n))", "reserved"),
        ("    fn f(n: Nat) -> Nat = 1 + match n { _ => 1 }", "must be parenthesized"),
    ] {
        parse_error(&theory(body), needle);
    }
    parse_error(include_str!("../../../examples/invalid/match_not_exhaustive.dlm"), "not exhaustive");
    parse_error(include_str!("../../../examples/invalid/match_arm_unreachable.dlm"), "unreachable");
    parse_error(include_str!("../../../examples/invalid/let_in_operator_needs_parentheses.dlm"), "must be parenthesized");
    // Type errors of patterns surface in the checker.
    let report = check(&theory("    fn f(n: Nat) -> Nat = match n { [] => 0, cons(h, t) => h }"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("is_empty expects List<T>")), "{:?}", report.diagnostics);
}

#[test]
fn logical_lines_join_continuations() {
    let source = "module m\ntheory T {\n    fn f(a: Nat,\n         b: Nat) -> Nat =\n        a +\n        b\n    theorem t(a: Nat, b: Nat):\n        nat_eq(f(a, b), b + a)\n        by normalize\n    let x = f(1, 2)\n}\n";
    let module = parse_module(source).expect("parse");
    let dlm_core::ModuleItem::Theory(theory) = &module.items[0] else { panic!() };
    assert_eq!(theory.items.len(), 3);
    let dlm_core::TheoryItem::Fn(f) = &theory.items[0] else { panic!() };
    assert_eq!((f.line, f.params.len()), (3, 2));
    let dlm_core::TheoryItem::Statement(t) = &theory.items[1] else { panic!() };
    assert_eq!(t.line, 7);
    assert!(t.proof.is_some());
    assert!(Checker::new().check_module(&module).ok());
    // Errors point at the invalid match inside the continued declaration.
    let diagnostics = parse_module("module m\ntheory T {\n    fn f(n: Nat) -> Nat =\n        match n {\n            0 => 1\n        }\n}\n").unwrap_err();
    assert_eq!(diagnostics[0].line, Some(4));
}
