//! v0.85 — exact signed integers: `Int`, binary and unary `-`, total
//! Euclidean `div`/`mod`, `abs`, conversions; checker, runtime, symbolic
//! kernel (with a soundness fuzz) and the C backend.

use std::collections::BTreeMap;

use dlm_core::{
    parse_expr, parse_module, prove_by_normalization, render_expr, CheckReport, Checker, DiagnosticKind, ExprKind,
    FnDecl, FnParam, ModuleItem, Runtime, TheoryItem,
};

fn theory(body: &str) -> String {
    format!("module m\ntheory T {{\n{body}\n}}\n")
}

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn output(body: &str) -> Vec<String> {
    let module = parse_module(&theory(body)).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    Runtime::new().run_module(&module).expect("run").output
}

fn run_error(body: &str) -> String {
    let module = parse_module(&theory(body)).expect("parse");
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    Runtime::new().run_module(&module).expect_err("runtime error").message
}

#[test]
fn subtraction_and_negation_parse() {
    let call = |source: &str| match parse_expr(source, 1).unwrap().kind {
        ExprKind::Call { name, args } => (name, args.len()),
        other => panic!("{source}: {other:?}"),
    };
    assert_eq!(call("a - b"), ("__sub".to_string(), 2));
    assert_eq!(call("-a"), ("__neg".to_string(), 1));
    assert_eq!(call("a - -b"), ("__sub".to_string(), 2));
    assert_eq!(call("-(a + b)"), ("__neg".to_string(), 1));
    // `-` is left-associative and binds like `+`.
    assert_eq!(render_expr(&parse_expr("a - b + c - d", 1).unwrap()), "(((a - b) + c) - d)");
    assert_eq!(render_expr(&parse_expr("a - -7", 1).unwrap()), "(a - -7)");
    // `->` and `=>` are not subtraction.
    let module = parse_module(&theory("    fn f(i: Int) -> Int = match int_eq(i, -1) { true => int(0), false => i - int(1) }")).unwrap();
    assert!(Checker::new().check_module(&module).ok());
}

#[test]
fn int_is_never_mixed_with_nat() {
    for (body, needle) in [
        ("    fn gap(a: Nat, b: Nat) -> Nat = a - b", "monus"),
        ("    fn shift(i: Int, n: Nat) -> Int = i + n", "int(n)"),
        ("    let x = -3 + 2", "int(n)"),
        ("    fn g(i: Int) -> Bool = i > 0", "Int"),
        ("    let y = abs(5)", "abs"),
        ("    fn h(n: Nat) -> Nat = -n", "Int"),
    ] {
        let report = check(&theory(body));
        assert!(
            report.diagnostics.iter().any(|d| format!("{} {}", d.message, d.help.as_deref().unwrap_or("")).contains(needle)),
            "{body}: {:?}",
            report.diagnostics
        );
    }
    // Int is a reserved type name.
    let reserved = parse_module(&theory("    record Int { x: Nat }"));
    assert!(reserved.map(|m| !Checker::new().check_module(&m).ok()).unwrap_or(true));
}

#[test]
fn exact_arithmetic_and_total_division() {
    let out = output(
        r#"    print_decimal(int(5) - mul(int(7), int(3)))
    print_decimal(-(int(2)) - -3)
    print_symbolic(list(div(-7, int(2)), mod(-7, int(2)), div(int(7), -2), mod(int(7), -2), div(-7, -2), mod(-7, -2)))
    print_symbolic(list(div(-7, int(0)), mod(-7, int(0)), div(int(0), -3)))
    print_symbolic(list(div(7, 2), mod(7, 2), div(7, 0), mod(7, 0)))
    print_decimal(abs(-9) + abs(int(4)))
    print_line(int_to_text(-170141183460469231731687303715884105728))
    print_symbolic(list(text_to_int("-12"), text_to_int("+3"), text_to_int("--1"), text_to_int("-"), text_to_int("007")))
    print_symbolic(int_eq(int(3) - int(5), -2))
    print_symbolic(int(1) > -1)"#,
    );
    assert_eq!(
        out,
        [
            "-16",
            "1",
            "[-4, 1, -3, 1, 4, 1]",
            "[0, -7, 0]",
            "[3, 1, 0, 7]",
            "13",
            "-170141183460469231731687303715884105728",
            "[some(-12), none, none, none, some(7)]",
            "true",
            "true",
        ]
    );
    // Euclidean identity a = div(a, b) * b + mod(a, b) and 0 <= mod(a, b) < |b|.
    let mut checks = String::new();
    for a in -6i32..=6 {
        for b in -3i32..=3 {
            let lit = |v: i32| if v < 0 { v.to_string() } else { format!("int({v})") };
            let (a, b) = (lit(a), lit(b));
            checks.push_str(&format!(
                "    print_symbolic(and(int_eq(mul(div({a}, {b}), {b}) + mod({a}, {b}), {a}), or(int_eq({b}, int(0)), and(not(int(0) > mod({a}, {b})), int(abs({b})) > mod({a}, {b})))))\n"
            ));
        }
    }
    assert!(output(&checks).iter().all(|l| l == "true"));
}

#[test]
fn overflow_is_a_runtime_error() {
    let min = "-170141183460469231731687303715884105728";
    assert!(run_error(&format!("    print_decimal({min} - int(1))")).contains("Int subtraction overflows"));
    assert!(run_error(&format!("    print_decimal(-({min}))")).contains("Int negation overflows"));
    assert!(run_error(&format!("    print_decimal(div({min}, -1))")).contains("overflows"));
    assert!(run_error("    print_decimal(int(340282366920938463463374607431768211455))").contains("overflows"));
    assert!(run_error(&format!("    print_decimal(mul({min}, int(2)))")).contains("Int multiplication overflows"));
}

#[test]
fn kernel_proves_ring_identities_and_cross_checks_them() {
    let report = check(&theory(
        r#"    fn sq(i: Int) -> Int = mul(i, i)
    fn diff(a: Nat, b: Nat) -> Int = int(a) - int(b)
    theorem sub_self(i: Int): int_eq(i - i, int(0)) by normalize
    theorem square_expand(a: Int, b: Int): int_eq(sq(a - b), sq(a) - mul(int(2), mul(a, b)) + sq(b)) by normalize
    theorem antisym(a: Nat, b: Nat): int_eq(diff(a, b), -diff(b, a)) by normalize
    theorem abs_sym(a: Int, b: Int): nat_eq(abs(a - b), abs(b - a)) by normalize
    theorem step(i: Int): i + int(1) > i by normalize
    theorem by_one(i: Int): int_eq(div(i, int(1)) + mod(i, int(1)), i) by normalize
    theorem by_zero(i: Int): int_eq(div(i, int(0)) + mod(i, int(0)), i) by normalize"#,
    ));
    assert!(report.ok(), "{:?}", report.diagnostics);
    let history = |name: &str| report.inferred.iter().find(|(n, _)| n == &format!("T.{name}")).unwrap().1.to_string();
    assert!(history("sub_self").contains("crosscheck:runtime:agree:cases=11"), "{}", history("sub_self"));
    assert!(history("square_expand").contains("crosscheck:runtime:agree:cases=121"), "{}", history("square_expand"));
    assert!(history("antisym").contains("crosscheck:runtime:agree:cases=36"), "{}", history("antisym"));
    for wrong in [
        "    theorem bad(a: Int): a + a > a by normalize",
        "    theorem bad(a: Int): int_eq(a - int(1), int(1) - a) by normalize",
        "    theorem bad(a: Int): not(int(0) > mul(a, a)) by normalize",
        "    theorem bad(a: Int): nat_eq(abs(a), abs(a + int(1))) by normalize",
        "    theorem bad(a: Int): int_eq(div(a, int(2)), int(0)) by normalize",
        "    theorem bad(a: Int): int_eq(a, a) by induction a",
        "    theorem bad(a: Int): int_eq(a, a)",
    ] {
        let report = check(&theory(wrong));
        assert!(report.diagnostics.iter().any(|d| d.kind == DiagnosticKind::StatementTheoremError), "{wrong}: {:?}", report.diagnostics);
    }
}

const INT_DEFINITIONS: &str = r#"
module defs
theory T {
    fn sq(i: Int) -> Int = mul(i, i)
    fn diff(a: Nat, b: Nat) -> Int = int(a) - int(b)
    fn sign(i: Int) -> Int = if_then_else(int_eq(i, int(0)), int(0), if_then_else(i > int(0), int(1), -1))
    fn clampi(i: Int) -> Int = if_then_else(i > int(2), int(2), i)
    fn dist(a: Int, b: Int) -> Nat = abs(a - b)
}
"#;

struct Rng(u64);

impl Rng {
    fn below(&mut self, n: u64) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0 % n
    }
}

fn int_expr(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(7) {
            0 | 1 => "a".to_string(),
            2 => "b".to_string(),
            3 => "int(n)".to_string(),
            4 => format!("int({})", rng.below(3)),
            5 => format!("-{}", 1 + rng.below(2)),
            _ => "int(abs(a))".to_string(),
        };
    }
    let (x, y) = (int_expr(rng, depth - 1), int_expr(rng, depth - 1));
    match rng.below(10) {
        0 | 1 => format!("{x} + {y}"),
        2 | 3 => format!("{x} - {y}"),
        4 => format!("mul({x}, {y})"),
        5 => format!("-({x})"),
        6 | 7 => {
            // Constant divisors are frequent so that every division rule is exercised.
            let d = if rng.below(2) == 0 { y } else { ["int(1)", "int(2)", "int(3)", "-2", "int(0)"][rng.below(5) as usize].to_string() };
            format!("{}({x}, {d})", if rng.below(2) == 0 { "div" } else { "mod" })
        }
        8 => format!("sq({x})"),
        _ => match rng.below(3) {
            0 => format!("sign({x})"),
            1 => format!("clampi({x})"),
            _ => format!("diff(n, abs({x}))"),
        },
    }
}

#[test]
fn int_kernel_never_proves_a_false_statement() {
    let module = parse_module(INT_DEFINITIONS).expect("definitions parse");
    let ModuleItem::Theory(theory) = &module.items[0] else { panic!("theory") };
    let decls: Vec<FnDecl> = theory.items.iter().filter_map(|i| if let TheoryItem::Fn(d) = i { Some(d.clone()) } else { None }).collect();
    let functions: BTreeMap<String, FnDecl> = decls.iter().map(|d| (d.name.clone(), d.clone())).collect();
    let vars = vec![
        FnParam { name: "a".into(), ty: "Int".into() },
        FnParam { name: "b".into(), ty: "Int".into() },
        FnParam { name: "n".into(), ty: "Nat".into() },
    ];
    let mut rng = Rng(0x9E37_79B9_7F4A_7C15);
    let (mut proved, mut rejected) = (0usize, 0usize);
    for round in 0..3000 {
        let (x, y) = (int_expr(&mut rng, 2), int_expr(&mut rng, 2));
        let statement = match round % 8 {
            0 => format!("int_eq({x}, {y})"),
            1 => format!("{x} > {y}"),
            2 => format!("not({x} > {y})"),
            3 => format!("nat_eq(abs({x}), abs({y}))"),
            4 => format!("int_eq({x} - {y}, -({y} - {x}) + int({}))", rng.below(2)),
            5 => format!("{x} + int({}) > {x}", rng.below(2)),
            6 => format!("int_eq({x}, int({}))", rng.below(3)),
            _ => format!("int_eq({x}, -{})", 1 + rng.below(2)),
        };
        let body = parse_expr(&statement, 1).expect("parse");
        if prove_by_normalization(&functions, &[], &vars, &body).is_ok() {
            proved += 1;
            let outcome = Runtime::new()
                .evaluate_statement("T", &decls, &[], &vars, 6, &body)
                .unwrap_or_else(|err| panic!("kernel proved `{statement}` but evaluation failed: {}", err.message));
            assert!(
                outcome.counterexample.is_none(),
                "SOUNDNESS BUG: kernel proved `{statement}` but it is false at {:?}",
                outcome.counterexample
            );
        } else {
            rejected += 1;
        }
    }
    eprintln!("int fuzz: proved {proved}, rejected {rejected} of 3000");
    assert!(proved > 300 && rejected > 500, "{proved} / {rejected}");
}
