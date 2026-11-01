//! v0.80 — imports, theory extension, private declarations, library modules
//! and nested lemma patterns.

use dlm_core::{
    link_program, load_program_with_paths, parse_module, CheckPolicy, CheckReport, Checker, DiagnosticKind, Module,
    Passport, Program, Runtime, TrustLevel,
};

fn module(source: &str) -> Module {
    parse_module(source).unwrap_or_else(|d| panic!("parse: {d:?}"))
}

fn program(root: &str, library: &[&str]) -> Program {
    link_program(module(root), library.iter().map(|s| module(s)).collect()).unwrap_or_else(|d| panic!("link: {d:?}"))
}

fn check(root: &str, library: &[&str]) -> CheckReport {
    Checker::new().check_program(&program(root, library))
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing {name}"))
}

fn rejects(report: &CheckReport, kind: DiagnosticKind, needle: &str) {
    assert!(
        report.diagnostics.iter().any(|d| d.kind == kind && d.message.contains(needle)),
        "expected {kind:?} containing `{needle}`, got {:?}",
        report.diagnostics
    );
}

const BASE: &str = "module lib.base
theory Base {
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    private fn helper(n: Nat) -> Nat = n + n
    fn twice(n: Nat) -> Nat = helper(n)
    theorem add_spec(a: Nat, b: Nat): nat_eq(add(a, b), a + b) by induction a
    private theorem helper_spec(n: Nat): nat_eq(helper(n), mul(2, n)) by normalize
    theorem twice_spec(n: Nat): nat_eq(twice(n), mul(2, n)) by normalize using helper_spec
    axiom mystery(n: Nat): nat_eq(add(n, 0), n)
    let base_value = 7
}
";

#[test]
fn derived_theory_uses_inherited_functions_and_lemmas() {
    let report = check(
        "module app
import lib.base
theory App extends Base {
    fn quad(n: Nat) -> Nat = twice(twice(n))
    theorem quad_spec(n: Nat): nat_eq(quad(n), mul(4, n)) by normalize using twice_spec
    theorem add_twice(a: Nat): nat_eq(add(a, a), mul(2, a)) by normalize using add_spec
    let sixteen = quad(4)
    print_decimal(sixteen)
}
",
        &[BASE],
    );
    assert!(report.ok(), "{:?}", report.diagnostics);
    // The report describes the root module only.
    assert!(report.inferred.iter().all(|(name, _)| name.starts_with("App.")), "{:?}", report.inferred.iter().map(|(n, _)| n).collect::<Vec<_>>());
    assert_eq!(report.certificates.len(), 2);
    assert!(passport(&report, "App.quad_spec").to_string().contains("lemma:use:twice_spec"));
    assert!(passport(&report, "App.quad_spec").trust < TrustLevel::Axiom);

    // At run time the derived theory executes inherited code, including the
    // private helper behind `twice`.
    let program = program(
        "module app
import lib.base
theory App extends Base {
    fn quad(n: Nat) -> Nat = twice(twice(n))
    print_decimal(quad(4))
    print_decimal(add(20, 22))
}
",
        &[BASE],
    );
    let run = Runtime::new().run_program(&program).expect("run");
    assert_eq!(run.output, vec!["16".to_string(), "42".to_string()]);
}

#[test]
fn axiom_taint_crosses_module_boundaries() {
    let root = "module app
import lib.base
theory App extends Base {
    theorem uses_mystery(n: Nat): nat_eq(add(n, 0) + 1, n + 1) by normalize using mystery
}
";
    let research = check(root, &[BASE]);
    assert!(research.ok(), "{:?}", research.diagnostics);
    assert_eq!(passport(&research, "App.uses_mystery").trust, TrustLevel::Axiom);
    let trusted = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program(root, &[BASE]));
    assert!(!trusted.ok(), "an imported axiom must still be blocked by --trusted-only");
}

#[test]
fn private_declarations_are_hidden_across_modules_only() {
    for (body, name) in [
        ("fn f(n: Nat) -> Nat = helper(n)", "helper"),
        ("theorem t(n: Nat): nat_eq(twice(n), mul(2, n)) by normalize using helper_spec", "helper_spec"),
        ("let x = helper(3)", "helper"),
    ] {
        let report = check(&format!("module app\nimport lib.base\ntheory App extends Base {{\n    {body}\n}}\n"), &[BASE]);
        rejects(&report, DiagnosticKind::ModuleInterfaceError, &format!("'{name}' is private to module 'lib.base'"));
    }
    // A parameter may reuse a hidden name.
    let report = check("module app\nimport lib.base\ntheory App extends Base {\n    fn f(helper: Nat) -> Nat = helper + 1\n}\n", &[BASE]);
    assert!(report.ok(), "{:?}", report.diagnostics);

    // Within one module, private names are visible to derived theories ...
    let same = "module lib.same
theory A {
    private fn hidden(n: Nat) -> Nat = n + 1
}
theory B extends A {
    fn uses(n: Nat) -> Nat = hidden(n)
}
";
    let report = Checker::new().check_module(&module(same));
    assert!(report.ok(), "{:?}", report.diagnostics);
    // ... and stay private for other modules, also through a chain.
    let report = check("module app\nimport lib.same\ntheory C extends B {\n    fn f(n: Nat) -> Nat = hidden(n)\n}\n", &[same]);
    rejects(&report, DiagnosticKind::ModuleInterfaceError, "'hidden' is private to module 'lib.same'");
    let mid = "module lib.mid\nimport lib.base\ntheory Mid extends Base {\n    fn g(n: Nat) -> Nat = twice(n)\n}\n";
    let report = check("module app\nimport lib.mid\ntheory App extends Mid {\n    fn f(n: Nat) -> Nat = helper(n)\n}\n", &[mid, BASE]);
    rejects(&report, DiagnosticKind::ModuleInterfaceError, "'helper' is private to module 'lib.base'");
}

#[test]
fn extension_errors() {
    let report = check("module app\nimport lib.base\ntheory App extends Base {\n    fn add(a: Nat, b: Nat) -> Nat = a\n}\n", &[BASE]);
    rejects(&report, DiagnosticKind::NameError, "redeclares 'add' inherited from 'Base'");
    // Hidden names cannot be redeclared either: inherited bodies call them.
    let report = check("module app\nimport lib.base\ntheory App extends Base {\n    fn helper(n: Nat) -> Nat = n\n}\n", &[BASE]);
    rejects(&report, DiagnosticKind::NameError, "redeclares 'helper' inherited from 'Base'");
    // Values are state, not declarations: they are not inherited.
    let report = check("module app\nimport lib.base\ntheory App extends Base {\n    let x = base_value + 1\n}\n", &[BASE]);
    assert!(!report.ok());
    let report = Checker::new().check_module(&module("module m\ntheory A extends A {\n}\n"));
    rejects(&report, DiagnosticKind::ModuleImportError, "cannot extend itself");
    let report = Checker::new().check_module(&module("module m\ntheory B extends A {\n}\ntheory A {\n}\n"));
    rejects(&report, DiagnosticKind::ModuleImportError, "extends unknown theory 'A'");
    assert!(parse_module("module m\ntheory A extends {\n}\n").is_err());
    assert!(parse_module("module m\ntheory A {\n    private let x = 1\n}\n").is_err());
}

#[test]
fn imports_must_be_linked_and_errors_name_the_module() {
    let report = Checker::new().check_module(&module("module app\nimport lib.base\ntheory App {\n}\n"));
    rejects(&report, DiagnosticKind::ModuleImportError, "imports 'lib.base', which is not loaded");
    let broken = "module lib.broken\ntheory Broken {\n    theorem bad(n: Nat): nat_eq(n, 1) by induction n\n}\n";
    let report = check("module app\nimport lib.broken\ntheory App {\n}\n", &[broken]);
    rejects(&report, DiagnosticKind::StatementTheoremError, "in imported module 'lib.broken': cannot prove theorem 'bad'");
}

#[test]
fn nested_lemma_patterns_match_syntactically() {
    let lib = "module lib.lists
theory Lists {
    fn sum_list(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + sum_list(tail(xs)))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))
    fn dec_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, pred(x)))
    theorem inc_all_sum(xs: List<Nat>): nat_eq(sum_list(inc_all(xs)), sum_list(xs) + length(xs)) by induction xs
}
";
    let report = check(
        "module app\nimport lib.lists\ntheory App extends Lists {\n    fn f(xs: List<Nat>) -> Nat = sum_list(inc_all(xs)) + 1\n    theorem f_spec(xs: List<Nat>): nat_eq(f(xs), sum_list(xs) + length(xs) + 1) by normalize using inc_all_sum\n}\n",
        &[lib],
    );
    assert!(report.ok(), "{:?}", report.diagnostics);
    // A different inner function does not match the pattern.
    let report = check(
        "module app\nimport lib.lists\ntheory App extends Lists {\n    theorem wrong(xs: List<Nat>): nat_eq(sum_list(dec_all(xs)), sum_list(xs) + length(xs)) by normalize using inc_all_sum\n}\n",
        &[lib],
    );
    rejects(&report, DiagnosticKind::StatementTheoremError, "cannot prove theorem 'wrong'");
}

#[test]
fn the_standard_library_loads_from_files() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let example = root.join("examples/valid/std_library_usage.dlm");
    let program = load_program_with_paths(&example, &[root.join("lib")]).expect("load");
    let names: Vec<&str> = program.modules.iter().map(|m| m.name.as_str()).collect();
    assert_eq!(names, ["std.nat", "std.list", "examples.std_library_usage"]);
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let run = Runtime::new().run_program(&program).expect("run");
    assert_eq!(run.output, vec!["9".to_string(), "55".to_string(), "42".to_string()]);
    // A file whose module name differs from the import is rejected.
    let dir = std::env::temp_dir().join(format!("dlm_modules_test_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("main.dlm"), "module main\nimport other\ntheory M {\n}\n").unwrap();
    std::fs::write(dir.join("other.dlm"), "module not_other\ntheory O {\n}\n").unwrap();
    let err = load_program_with_paths(&dir.join("main.dlm"), &[]).unwrap_err();
    assert!(err[0].message.contains("declares module 'not_other', but it was imported as 'other'"), "{err:?}");
    std::fs::remove_dir_all(&dir).ok();
}
