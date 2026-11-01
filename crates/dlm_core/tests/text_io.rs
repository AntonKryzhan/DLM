//! v0.83 — Text literals and operations, line I/O, `args()` and `fn main`.

use dlm_core::{parse_expr, parse_module, render_expr, CheckReport, Checker, DiagnosticKind, ExprKind, Runtime, RuntimeValue};

fn theory(body: &str) -> String {
    format!("module m\ntheory T {{\n{body}\n}}\n")
}

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn run_with(source: &str, stdin: &str, args: &[&str]) -> dlm_core::RunReport {
    let module = parse_module(source).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    Runtime::with_stdin(stdin).with_args(args.iter().map(|a| a.to_string()).collect()).run_module(&module).expect("run")
}

fn output(source: &str) -> Vec<String> {
    run_with(source, "", &[]).output
}

#[test]
fn literals_and_escapes() {
    let expr = parse_expr(r#"concat("a\tb", "say \"hi\"\\n")"#, 1).unwrap();
    let ExprKind::Call { args, .. } = &expr.kind else { panic!() };
    assert_eq!(args[0].kind, ExprKind::TextLiteral("a\tb".into()));
    assert_eq!(args[1].kind, ExprKind::TextLiteral("say \"hi\"\\n".into()));
    assert_eq!(render_expr(&args[0]), r#""a\tb""#);
    // Commas, parentheses, `//` and `=>` inside literals do not confuse the parser.
    let out = output(&theory(r#"    print_line(concat("a, (b) => c", " // not a comment")) // a real comment"#));
    assert_eq!(out, ["a, (b) => c // not a comment"]);
    for (source, needle) in [
        (theory("    let t = \"open"), "unterminated text literal"),
        (theory(r#"    let t = "bad \q escape""#), "unknown escape"),
        (theory("    let __text_0 = 1"), "reserved"),
    ] {
        let diagnostics = parse_module(&source).expect_err(needle);
        assert!(diagnostics.iter().any(|d| d.kind == DiagnosticKind::ParseError && d.message.contains(needle)), "{needle}: {diagnostics:?}");
    }
}

#[test]
fn text_builtins() {
    let out = output(&theory(
        r#"    print_line(concat("ab", "cd"))
    print_decimal(text_length("héllo"))
    print_line(nat_to_text(1234))
    print_decimal(unwrap_or(text_to_nat("077"), 9) + unwrap_or(text_to_nat("7x"), 1) + unwrap_or(text_to_nat(""), 2))
    print_line(substring("abcdef", 2, 3))
    print_line(substring("abc", 5, 3))
    print_line(join(words("  a  b   c "), "-"))
    print_decimal(length(lines("x\ny\n")))
    print_symbolic(text_eq("a", "a"))"#,
    ));
    assert_eq!(out, ["abcd", "5", "1234", "80", "cde", "", "a-b-c", "2", "true"]);
    let report = check(&theory("    let x = concat(\"a\", 1)"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("concat")), "{:?}", report.diagnostics);
}

#[test]
fn text_theorems_are_proved_and_cross_checked() {
    let report = check(&theory(
        r#"    fn greet(name: Text) -> Text = concat("Hello, ", name)
    theorem refl(t: Text): text_eq(t, t) by normalize
    theorem folded: text_eq(concat("a", "b"), "ab") by normalize
    theorem length_lit(n: Nat): nat_eq(text_length(greet("DLM")) + n, n + 10) by normalize
    theorem digits(n: Nat): text_eq(nat_to_text(n + 0), nat_to_text(n)) by normalize"#,
    ));
    assert!(report.ok(), "{:?}", report.diagnostics);
    let refl = &report.inferred.iter().find(|(n, _)| n == "T.refl").unwrap().1;
    assert!(refl.to_string().contains("crosscheck:runtime:agree:cases=5:theorem=refl"), "{refl}");
    for wrong in [
        "    theorem bad: text_eq(\"a\", \"b\") by normalize",
        "    theorem bad(t: Text): text_eq(t, \"a\") by normalize",
        "    theorem bad: nat_eq(text_length(\"abc\"), 2) by normalize",
    ] {
        let report = check(&theory(wrong));
        assert!(report.diagnostics.iter().any(|d| d.kind == DiagnosticKind::StatementTheoremError), "{wrong}: {:?}", report.diagnostics);
    }
}

#[test]
fn line_input_arguments_and_main() {
    let source = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/valid/word_count.dlm")).unwrap();
    let report = run_with(&source, "one two\nthree", &["Anton"]);
    assert_eq!(report.output, ["hello, Anton", "lines: 2", "words: 3", "chars: 13"]);
    assert_eq!(report.main, Some(RuntimeValue::NatExact(0)));
    assert_eq!(report.exit_code(), 0);
    let report = run_with(&source, "", &[]);
    assert_eq!(report.output[0], "hello, nobody");

    let lines = theory(
        "    fn show(o: Option<Text>) -> Text = match o { some(t) => t, none => \"<eof>\" }
    print_line(show(read_line()))
    print_line(show(read_line()))
    print_line(read_all())
    print_line(show(read_line()))",
    );
    assert_eq!(run_with(&lines, "a\r\nb\nrest\nmore", &[]).output, ["a", "b", "rest\nmore", "<eof>"]);

    let status = theory("    fn main() -> Nat = 300");
    let report = run_with(&status, "", &[]);
    assert_eq!(report.exit_code(), 255, "exit status saturates");
    let text_main = theory("    fn main() -> Text = \"done\"");
    assert_eq!(run_with(&text_main, "", &[]).exit_code(), 0);
}

#[test]
fn main_and_effects_are_checked() {
    let report = check(&theory("    fn main(n: Nat) -> Nat = n"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("fn main must not take parameters")));
    let report = check("module m\ntheory A {\n    fn main() -> Nat = 0\n}\ntheory B {\n    fn main() -> Nat = 1\n}\n");
    assert!(report.diagnostics.iter().any(|d| d.message.contains("several main functions (theories A, B)")), "{:?}", report.diagnostics);
    let report = check(include_str!("../../../examples/invalid/effectful_function_in_theorem.dlm"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("must be pure and total; it uses effects [io@stdout]")), "{:?}", report.diagnostics);
    let report = check(&theory("    fn n_args() -> Nat = length(args())\n    theorem t: nat_eq(n_args(), 0)"));
    assert!(report.diagnostics.iter().any(|d| d.message.contains("io@args")), "{:?}", report.diagnostics);
}
