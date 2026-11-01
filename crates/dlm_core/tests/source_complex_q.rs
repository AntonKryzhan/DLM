use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn check(source: &str, expected: &[String], name: &str) {
    let dir = std::env::temp_dir().join(format!("dlm-complex-q-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let program = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
    assert!(report.ok(), "{name}: {:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&program).unwrap().output,
        expected,
        "{name}: interpreter"
    );
    eprintln!(
        "complex {name}: interpreter passed ({} assertions)",
        expected.len()
    );
    let ir = lower_program(&program, CheckPolicy::trusted_only()).unwrap();
    let c = emit_c(&ir);
    // Optional local acceptance artifact; no source/library behavior depends on it.
    if let Some(output) = std::env::var_os("DLM_COMPLEX_Q_VERIFY_DIR") {
        let output = PathBuf::from(output);
        std::fs::create_dir_all(&output).unwrap();
        std::fs::write(output.join(format!("complex-{name}.c")), &c).unwrap();
        std::fs::write(
            output.join(format!("complex-{name}.expected")),
            expected.join("\n") + "\n",
        )
        .unwrap();
    }
    let exe = build_c_executable(
        &c,
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let native = Command::new(exe).output().unwrap();
    assert!(
        native.status.success(),
        "{name}: {}",
        String::from_utf8_lossy(&native.stderr)
    );
    assert_eq!(
        String::from_utf8(native.stdout).unwrap().trim_end(),
        expected.join("\n"),
        "{name}: C"
    );
    eprintln!("complex {name}: C passed");
    std::fs::remove_dir_all(dir).unwrap();
}

fn header() -> String {
    r#"module test.complex_q
import std.complex_q
theory App extends StdComplexQ {
 fn same_bool(a: Bool, b: Bool) -> Bool = if_then_else(a, b, not(b))
 fn same_words(a: List<Nat>, c: List<Nat>) -> Bool decreasing a fuel 2048 =
  if_then_else(is_empty(a), is_empty(c), if_then_else(is_empty(c), false, and(nat_eq(unwrap_or(head(a), 0), unwrap_or(head(c), 0)), same_words(tail(a), tail(c)))))
 fn same_q(a: BigRational, c: BigRational) -> Bool = and(same_bool(field(a, negative), field(c, negative)), and(same_words(field(a, numerator), field(c, numerator)), same_words(field(a, denominator), field(c, denominator))))
 fn same_z(a: ComplexQ, c: ComplexQ) -> Bool = and(same_q(field(a, real), field(c, real)), same_q(field(a, imag), field(c, imag)))
 fn zresult(r: Result<ComplexQCalc, Text>, expected: ComplexQ) -> Bool = match r { err(e) => false, ok(v) => same_z(field(v, value), expected) }
 fn qresult(r: Result<BigRationalCalc, Text>, expected: BigRational) -> Bool = match r { err(e) => false, ok(v) => same_q(field(v, value), expected) }
 fn eresult(r: Result<ComplexQEqual, Text>, expected: Bool) -> Bool = match r { err(e) => false, ok(v) => same_bool(field(v, value), expected) }
 fn zerror(r: Result<ComplexQCalc, Text>, expected: Text) -> Bool = match r { err(e) => text_eq(e, expected), ok(v) => false }
 fn qerror(r: Result<BigRationalCalc, Text>, expected: Text) -> Bool = match r { err(e) => text_eq(e, expected), ok(v) => false }
 fn zspent(r: Result<ComplexQCalc, Text>) -> Bool = match r { err(e) => false, ok(v) => and(nat_eq(field(field(v, budget), work), 0), is_err(complexq_check(field(v, budget), field(v, value)))) }
 fn qspent(r: Result<BigRationalCalc, Text>) -> Bool = match r { err(e) => false, ok(v) => and(nat_eq(field(field(v, budget), work), 0), is_err(bigrat_check(field(v, budget), field(v, value)))) }
 fn espent(r: Result<ComplexQEqual, Text>) -> Bool = match r { err(e) => false, ok(v) => nat_eq(field(field(v, budget), work), 0) }
 fn tspent(r: Result<BigTextCalc, Text>) -> Bool = match r { err(e) => false, ok(v) => nat_eq(field(field(v, budget), work), 0) }
 fn show(r: Result<ComplexQCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match complexq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
"#.into()
}

#[test]
fn rational_pair_oracles_match_interpreter_and_c_at_all_precision_profiles() {
    let mut source = header();
    let mut expected = Vec::new();
    for line in include_str!("fixtures/complex_q098_source.tsv")
        .lines()
        .filter(|l| !l.starts_with('#'))
    {
        let f: Vec<_> = line.split('\t').collect();
        let comparator = match f[0] {
            "complex" => "zresult",
            "rational" => "qresult",
            "equal" => "eresult",
            "error" => "zerror",
            "qerror" => "qerror",
            kind => panic!("unknown {kind}"),
        };
        source += &format!(" print_symbolic({comparator}({}, {}))\n", f[1], f[2]);
        expected.push("true".into());
    }
    assert_eq!(expected.len(), 179);
    source += "}\n";
    check(&source, &expected, "oracles");
}

fn consumers(b: &str, a: &str, c: &str) -> Vec<String> {
    let mut calls = vec![
        format!("complexq_check({b}, {a})"),
        format!("complexq_new({b}, field({a}, real), field({a}, imag))"),
    ];
    for name in ["neg", "conj", "inv", "norm_squared", "text"] {
        calls.push(format!("complexq_{name}({b}, {a})"));
    }
    for name in ["add", "sub", "mul", "div", "equal"] {
        calls.push(format!("complexq_{name}({b}, {a}, {c})"));
        calls.push(format!("complexq_{name}({b}, {c}, {a})"));
    }
    calls
}

#[test]
fn every_public_consumer_rejects_malformed_or_out_of_profile_components_and_budgets() {
    let mut source = header();
    source += " let q0 = BigRational(false, empty_list(Nat), list(1))\n let q1 = BigRational(false, list(1), list(1))\n let valid = ComplexQ(q0, q1)\n let b = ExactBudget(8, 1000000000000000)\n";
    let mut expected = Vec::new();
    let padding = format!(
        "BigRational(false, list(1, {}), list(1))",
        vec!["0"; 64].join(", ")
    );
    let invalid = [
        "BigRational(true, empty_list(Nat), list(1))",
        "BigRational(false, list(2), list(4))",
        "BigRational(false, list(1), empty_list(Nat))",
        "BigRational(false, list(1, 0), list(1))",
        "BigRational(false, list(65536), list(1))",
        "BigRational(false, list(1), list(65536))",
        "BigRational(false, list(1), list(1, 0))",
        "BigRational(false, empty_list(Nat), list(2))",
        "BigRational(false, list(256), list(1))",
        "BigRational(false, list(1), list(256))",
        &padding,
    ];
    for bad in invalid {
        for a in [
            format!("ComplexQ({bad}, q0)"),
            format!("ComplexQ(q0, {bad})"),
        ] {
            for call in consumers("b", &a, "valid") {
                source += &format!(" print_symbolic(is_err({call}))\n");
                expected.push("true".into());
            }
        }
    }
    for b in [
        "ExactBudget(0, 1000000000)",
        "ExactBudget(16385, 1000000000)",
        "ExactBudget(8, 1000000000000001)",
        "ExactBudget(1025, 18446744073709551616)",
        "ExactBudget(8, 0)",
    ] {
        for call in consumers(b, "valid", "valid")
            .into_iter()
            .chain([format!("complexq_from_ints({b}, int(1), int(2))")])
        {
            source += &format!(" print_symbolic(is_err({call}))\n");
            expected.push("true".into());
        }
    }
    source += "}\n";
    assert_eq!(expected.len(), 464);
    check(&source, &expected, "invalid");
}

#[test]
fn compound_operations_thread_every_successor_budget_with_exact_reservation_boundaries() {
    let mut source = header();
    source += " let q1 = BigRational(false, list(1), list(1))\n let q2 = BigRational(false, list(2), list(1))\n let z = ComplexQ(q1, q2)\n let w = ComplexQ(q2, BigRational(true, list(1), list(1)))\n";
    let mut expected = Vec::new();
    for (name, cost, comparator, args) in [
        ("check", 25_000_000u64, "zspent", "z"),
        ("new", 25_000_000, "zspent", "q1, q2"),
        ("from_ints", 50_000_000, "zspent", "-3, int(7)"),
        ("equal", 75_000_000, "espent", "z, w"),
        ("neg", 50_000_000, "zspent", "z"),
        ("conj", 37_500_000, "zspent", "z"),
        ("add", 75_000_000, "zspent", "z, w"),
        ("sub", 75_000_000, "zspent", "z, w"),
        ("mul", 125_000_000, "zspent", "z, w"),
        ("div", 200_000_000, "zspent", "z, w"),
        ("inv", 200_000_000, "zspent", "z"),
        ("norm_squared", 62_500_000, "qspent", "z"),
        ("text", 50_000_000, "tspent", "z"),
    ] {
        source += &format!(
            " print_symbolic({comparator}(complexq_{name}(ExactBudget(8, {cost}), {args})))\n"
        );
        source += &format!(
            " print_symbolic(is_err(complexq_{name}(ExactBudget(8, {}), {args})))\n",
            cost - 1
        );
        expected.extend(["true".into(), "true".into()]);
    }
    source += r#"
 let original = ExactBudget(8, 125000000)
 print_symbolic(zspent(complexq_mul(original, z, w)))
 print_symbolic(nat_eq(field(original, work), 125000000))
 print_symbolic(same_z(z, ComplexQ(q1, q2)))
 print_symbolic(zresult(complexq_mul(ExactBudget(8, 125000000), ComplexQ(q1, q2), ComplexQ(q2, BigRational(true, list(1), list(1)))), ComplexQ(BigRational(false, list(4), list(1)), BigRational(false, list(3), list(1)))))
}
"#;
    expected.extend(vec!["true".into(); 4]);
    check(&source, &expected, "budgets");
}

#[test]
fn constructors_normalization_zero_signs_text_and_machine_extremes_remain_exact() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(128, 1000000000000000)
 let zero = BigRational(false, empty_list(Nat), list(1))
 let one = BigRational(false, list(1), list(1))
 print_line(show(complexq_from_ints(b, -170141183460469231731687303715884105728, int(170141183460469231731687303715884105727))))
 print_line(show(complexq_from_ints(b, int(0), int(0))))
 print_line(show(complexq_conj(b, ComplexQ(zero, zero))))
 print_line(show(complexq_neg(b, ComplexQ(zero, zero))))
 print_line(show(complexq_mul(b, ComplexQ(zero, one), ComplexQ(zero, one))))
 print_symbolic(is_err(complexq_from_ints(ExactBudget(127, 1000000000000000), -170141183460469231731687303715884105728, int(0))))
 print_symbolic(zerror(complexq_new(b, BigRational(false, list(2), list(4)), zero), "noncanonical big rational"))
 print_symbolic(zerror(complexq_div(b, ComplexQ(zero, BigRational(true, empty_list(Nat), list(1))), ComplexQ(zero, zero)), "invalid canonical big rational"))
 fn normalized(budget: ExactBudget) -> Result<ComplexQCalc, Text> =
  match bigrat_parse(budget, "-0000/003") { err(e) => err(e, ComplexQCalc), ok(r) =>
   match bigrat_parse(field(r, budget), "2/4") { err(e) => err(e, ComplexQCalc), ok(i) => complexq_new(field(i, budget), field(r, value), field(i, value)) } }
 print_line(show(normalized(b)))
}
"#;
    let expected = [
        format!("{}/1;{}/1", i128::MIN, i128::MAX),
        "0/1;0/1".into(),
        "0/1;0/1".into(),
        "0/1;0/1".into(),
        "-1/1;0/1".into(),
        "true".into(),
        "true".into(),
        "true".into(),
        "0/1;1/2".into(),
    ];
    check(&source, &expected, "constructors");
}

#[test]
fn implementation_helpers_cannot_be_imported_or_shadowed() {
    let dir = std::env::temp_dir().join(format!("dlm-complex-q-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (i, body) in [
        "let x = cq_zero()",
        "let x = cq_one()",
        "let x = cq_norm(exact_default_budget(), ComplexQ(BigRational(false, list(1), list(1)), BigRational(false, list(1), list(1))))",
        "fn cq_prepare(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ExactBudget, Text> = ok(b, Text)",
        "fn cq_product(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> = ok(ComplexQCalc(a, b), Text)",
        "fn cq_components(b: ExactBudget, a: ComplexQ, c: ComplexQ, f: Fn<ExactBudget, BigRational, BigRational, Result<BigRationalCalc, Text>>) -> Result<ComplexQCalc, Text> = ok(ComplexQCalc(a, b), Text)",
    ].iter().enumerate() {
        let path = dir.join(format!("bad{i}.dlm"));
        std::fs::write(&path, format!("module test.private_complex\nimport std.complex_q\ntheory App extends StdComplexQ {{ {body} }}\n")).unwrap();
        let rejected = match load_program_with_paths(&path, &[repo().join("lib")]) {
            Err(_) => true,
            Ok(p) => !Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p).ok(),
        };
        assert!(rejected, "helper escaped privacy: {body}");
    }
    std::fs::remove_dir_all(dir).unwrap();
}
