//! v0.81 — `mutual { ... }` blocks: mutually recursive functions with a shared
//! decreasing measure, in the checker, the symbolic kernel and the runtime.

use dlm_core::{parse_module, CheckReport, Checker, DiagnosticKind, Runtime, TypeKind};

const EXAMPLE: &str = include_str!("../../../examples/valid/mutual_recursion.dlm");

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn rejects(source: &str, kind: DiagnosticKind, needle: &str) {
    let diagnostics = match parse_module(source) {
        Ok(module) => Checker::new().check_module(&module).diagnostics,
        Err(diagnostics) => diagnostics,
    };
    assert!(
        diagnostics.iter().any(|d| d.kind == kind && d.message.contains(needle)),
        "expected {kind:?} containing `{needle}`, got {diagnostics:?}"
    );
}

fn group(body: &str) -> String {
    format!("module m\ntheory T {{\n    mutual {{\n{body}\n    }}\n}}\n")
}

#[test]
fn mutual_functions_check_prove_and_run() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(report.certificates.len(), 4);
    for name in ["even_n", "odd_n", "even_pos", "odd_pos"] {
        let (_, recursion) = report
            .inferred
            .iter()
            .find(|(n, _)| n == &format!("Parity.{name}.recursion"))
            .unwrap_or_else(|| panic!("recursion scheme of {name}"));
        assert!(matches!(recursion.ty, TypeKind::RecursionScheme { .. }), "{name}: {}", recursion.ty);
    }
    let run = Runtime::new().run_module(&parse_module(EXAMPLE).unwrap()).expect("run");
    assert_eq!(run.output, vec!["9".to_string()]);
}

#[test]
fn deep_mutual_recursion_runs_within_fuel() {
    let source = "module m\ntheory T {\n    mutual {\n        fn ev(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, od(pred(n)))\n        fn od(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), false, ev(pred(n)))\n    }\n    let big = ev(15000)\n    print_symbolic(big)\n}\n";
    assert!(check(source).ok());
    let run = Runtime::new().run_module(&parse_module(source).unwrap()).expect("run");
    assert_eq!(run.output, vec!["true".to_string()]);
}

#[test]
fn every_mutual_call_must_decrease_under_a_guard() {
    rejects(
        include_str!("../../../examples/invalid/mutual_call_not_decreasing.dlm"),
        DiagnosticKind::RecursionBoundaryError,
        "mutual call from 'ping' to 'pong' must pass pred(n)",
    );
    rejects(
        include_str!("../../../examples/invalid/mutual_call_unguarded.dlm"),
        DiagnosticKind::RecursionBoundaryError,
        "is not guarded by a check that 'n' is positive",
    );
    rejects(
        &group("        fn a(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, b(list(n)))\n        fn b(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, a(0))"),
        DiagnosticKind::RecursionBoundaryError,
        "measure different kinds of values",
    );
    rejects(
        &group("        fn a(n: Nat) -> Nat = b(n)\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, a(pred(n)))"),
        DiagnosticKind::RecursionBoundaryError,
        "is in a mutual block but declares no measure",
    );
    rejects(
        &group("        fn a(n: Nat, m: Nat) -> Nat decreasing (n, m) = if_then_else(nat_eq(n, 0), 0, b(pred(n)))\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, a(pred(n), 0))"),
        DiagnosticKind::RecursionBoundaryError,
        "supports one measured parameter",
    );
    rejects(
        &group("        fn a(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, apply_twice(fun(x: Nat, b(x)), n))\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, a(pred(n)))"),
        DiagnosticKind::RecursionBoundaryError,
        "may not be called inside a lambda",
    );
}

#[test]
fn mutual_groups_must_be_pure_and_total() {
    for (body, culprit) in [
        ("        fn a(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), read_nat(), b(pred(n)))\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, a(pred(n)))", "'a' is not"),
        // `a` was checked assuming `b` pure; the group is still rejected.
        ("        fn a(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, b(pred(n)))\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), read_nat(), a(pred(n)))", "'b' is not"),
    ] {
        let source = group(body) .replace("    }\n}\n", "    }\n    let x = a(3)\n}\n");
        let report = check(&source);
        assert!(
            report.diagnostics.iter().any(|d| d.message.contains("must be pure and total") && d.message.contains(culprit)),
            "{:?}",
            report.diagnostics
        );
        // A rejected group leaves no partially registered member behind.
        assert!(report.diagnostics.iter().any(|d| d.message.contains("unknown builtin or function 'a'")), "{:?}", report.diagnostics);
    }
}

#[test]
fn mutual_block_syntax() {
    for (source, needle) in [
        (group("        fn a(n: Nat) -> Nat = n"), "at least two fn declarations"),
        (group("        let x = 1\n        fn a(n: Nat) -> Nat = n\n        fn b(n: Nat) -> Nat = n"), "only fn declarations may appear"),
        ("module m\ntheory T {\n    mutual {\n    mutual {\n    }\n    }\n}\n".to_string(), "cannot be nested"),
    ] {
        let diagnostics = parse_module(&source).expect_err(needle);
        assert!(diagnostics.iter().any(|d| d.message.contains(needle)), "{needle}: {diagnostics:?}");
    }
    rejects(
        include_str!("../../../examples/invalid/forward_call_without_mutual.dlm"),
        DiagnosticKind::RecursionBoundaryError,
        "fn 'ping' calls 'pong', which is declared later in theory 'T'",
    );
    rejects(
        &group("        fn a(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, a(pred(n)))\n        fn a(n: Nat) -> Nat decreasing n = n"),
        DiagnosticKind::NameError,
        "duplicate value 'a' in theory 'T'",
    );
    rejects(
        &group("        fn length(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, b(pred(n)))\n        fn b(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, length(pred(n)))"),
        DiagnosticKind::NameError,
        "shadows a builtin",
    );
}
