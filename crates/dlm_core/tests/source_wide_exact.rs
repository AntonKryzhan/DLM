use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn header() -> String {
    r#"module test.wide_exact
import std.exact
theory App extends StdExact {
 let wide = ExactBudget(16384, 18446744073709551615)
 fn words_equal(xs: List<Nat>, ys: List<Nat>) -> Bool decreasing xs fuel 4096 = if_then_else(is_empty(xs), is_empty(ys), if_then_else(is_empty(ys), false, and(nat_eq(unwrap_or(head(xs), 0), unwrap_or(head(ys), 0)), words_equal(tail(xs), tail(ys)))))
 fn integer_equal(a: BigInt, b: BigInt) -> Bool = and(if_then_else(field(a, negative), field(b, negative), not(field(b, negative))), words_equal(field(a, words), field(b, words)))
 fn work_equal(a: ExactBudget, b: ExactBudget) -> Bool = nat_eq(field(a, work), field(b, work))
 fn rational_equal(a: BigRational, b: BigRational) -> Bool = and(if_then_else(field(a, negative), field(b, negative), not(field(b, negative))), and(words_equal(field(a, numerator), field(b, numerator)), words_equal(field(a, denominator), field(b, denominator))))
 fn integer_matches(r: Result<BigIntCalc, Text>, value: BigInt) -> Bool = match r { err(e) => false, ok(v) => integer_equal(field(v, value), value) }
 fn rational_matches(r: Result<BigRationalCalc, Text>, value: BigRational) -> Bool = match r { err(e) => false, ok(v) => rational_equal(field(v, value), value) }
 fn division_matches(r: Result<BigDivision, Text>, q: BigInt, rem: BigInt) -> Bool = match r { err(e) => false, ok(v) => and(integer_equal(field(v, quotient), q), integer_equal(field(v, remainder), rem)) }
 fn root_matches(r: Result<BigSquareRoot, Text>, root: BigInt, rem: BigInt) -> Bool = match r { err(e) => false, ok(v) => and(integer_equal(field(v, root), root), integer_equal(field(v, remainder), rem)) }
 fn integer_error(r: Result<BigIntCalc, Text>, expected: Text) -> Bool = match r { err(e) => text_eq(e, expected), ok(v) => false }
 fn rational_error(r: Result<BigRationalCalc, Text>, expected: Text) -> Bool = match r { err(e) => text_eq(e, expected), ok(v) => false }
 fn padded(n: Nat, xs: List<Nat>) -> List<Nat> decreasing n fuel 4096 = if_then_else(nat_eq(n, 0), xs, padded(pred(n), cons(0, xs)))
 fn integer_roundtrip(b: ExactBudget, text: Text, expected: Text) -> Bool = match big_parse(b, text) { err(e) => false, ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => false, ok(t) => text_eq(field(t, value), expected) } }
 fn rational_roundtrip(b: ExactBudget, text: Text, expected: Text) -> Bool = match bigrat_parse(b, text) { err(e) => false, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => false, ok(t) => text_eq(field(t, value), expected) } }
"#.into()
}
fn check(source: &str, count: usize, name: &str) {
    let dir = std::env::temp_dir().join(format!("dlm-wide-exact-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let expected = vec!["true".to_string(); count];
    assert_eq!(
        String::from_utf8(out.stdout).unwrap().trim_end(),
        expected.join("\n")
    );
    eprintln!("{name}: native passed");
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    eprintln!("{name}: interpreter passed");
    std::fs::remove_dir_all(dir).unwrap();
}
fn fixture(mode: &str) -> (String, usize) {
    let mut source = header();
    let mut count = 0;
    for line in include_str!("fixtures/wide_exact097.tsv")
        .lines()
        .filter(|l| !l.starts_with('#'))
    {
        let f: Vec<_> = line.split('\t').collect();
        if f[1] != mode {
            continue;
        }
        let function = match f[0] {
            "integer" => "integer_matches",
            "rational" => "rational_matches",
            "division" => "division_matches",
            "root" => "root_matches",
            "error" => "integer_error",
            "qerror" => "rational_error",
            other => panic!("{other}"),
        };
        let extra = if f[4] == "-" {
            String::new()
        } else {
            format!(", {}", f[4])
        };
        source += &format!(" print_symbolic({function}({}, {}{extra}))\n", f[2], f[3]);
        count += 1;
    }
    source += "}\n";
    (source, count)
}
#[test]
fn wide_integer_and_rational_oracles_match_interpreter_and_c() {
    let (s, n) = fixture("both");
    assert_eq!(n, 33);
    check(&s, n, "oracles");
}
#[test]
fn wide_16384_bit_root_and_more_than_8192_gcd_steps() {
    let (s, n) = fixture("stress");
    assert_eq!(n, 2);
    check(&s, n, "stress");
}
#[test]
fn wide_decimal_roundtrip_long_inputs_slash_search_and_strict_errors() {
    let text = include_str!("fixtures/wide_decimal097.txt").trim();
    assert_eq!(text.len(), 4933);
    let mut s = header();
    s += &format!(" print_symbolic(integer_roundtrip(wide, \"{text}\", \"{text}\"))\n");
    s += &format!(
        " print_symbolic(integer_roundtrip(wide, \"{}1\", \"1\"))\n",
        "0".repeat(16385)
    );
    s += &format!(
        " print_symbolic(rational_roundtrip(wide, \"{}/1\", \"0/1\"))\n",
        "0".repeat(32769)
    );
    s+=&format!(" print_symbolic(rational_error(bigrat_parse(wide, \"{}/0\"), \"exact rational denominator is zero\"))\n","0".repeat(6000));
    s+=&format!(" print_symbolic(rational_error(bigrat_parse(wide, \"{}/a\"), \"invalid exact decimal\"))\n","0".repeat(6000));
    s += &format!(
        " print_symbolic(is_err(big_parse(wide, \"{}\")))\n",
        "0".repeat(16387)
    );
    s += &format!(
        " print_symbolic(is_err(bigrat_parse(wide, \"{}\")))\n",
        "0".repeat(32772)
    );
    s +=
        " print_symbolic(integer_error(big_parse(wide, \"１２\"), \"invalid exact decimal\"))\n}\n";
    check(&s, 8, "text");
}
#[test]
fn profiles_preserve_legacy_limits_and_wide_budgets_are_threaded() {
    let mut s = header();
    s += r#"
 let zero = BigInt(false, empty_list(Nat))
 let one = BigInt(false, list(1))
 print_symbolic(is_ok(exact_budget(1025, 18446744073709551615)))
 print_symbolic(is_ok(exact_budget(16384, 18446744073709551615)))
 print_symbolic(is_err(exact_budget(16385, 100)))
 print_symbolic(is_err(exact_budget(16384, 18446744073709551616)))
 print_symbolic(is_err(exact_budget(1024, 1000000000000001)))
 print_symbolic(is_ok(exact_budget(1025, 1000000000000001)))
 print_symbolic(nat_eq(field(exact_default_budget(), bits), 256))
 print_symbolic(nat_eq(field(exact_default_budget(), work), 1000000000000000))
 print_symbolic(is_err(big_from_words(ExactBudget(1024, 1000000000000000), false, padded(65, empty_list(Nat)))))
 print_symbolic(integer_matches(big_from_words(wide, true, padded(1024, empty_list(Nat))), zero))
 print_symbolic(is_err(big_from_words(wide, false, padded(1025, empty_list(Nat)))))
 let large = BigInt(false, padded(1023, list(32768)))
 let cost = 353909839464020000
 print_symbolic(integer_error(big_add(ExactBudget(16384, 353909839464019999), large, zero), "UnknownWithinBudget: source work reservation exhausted"))
 print_symbolic(integer_error(big_pow(ExactBudget(16383, 18446744073709551615), large, 0), "UnknownWithinBudget: source precision exceeded"))
 let result = big_add(ExactBudget(16384, cost), large, zero)
 print_symbolic(match result { err(e) => false, ok(v) => and(integer_equal(field(v, value), large), nat_eq(field(field(v, budget), work), 0)) })
 print_symbolic(match result { err(e) => false, ok(v) => is_err(big_add(field(v, budget), one, one)) })
 print_symbolic(work_equal(wide, ExactBudget(16384, 18446744073709551615)))
 print_symbolic(is_err(big_from_words(wide, false, list(65536))))
 print_symbolic(is_err(big_sqrt_rem(wide, BigInt(true, list(1)))))
}
"#;
    check(&s, 18, "profiles");
}
#[test]
fn new_word_and_text_block_helpers_cannot_escape_import_privacy() {
    let dir = std::env::temp_dir().join(format!("dlm-wide-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (i, e) in [
        "bx_div_digit(16, 1, list(1), 0, empty_list(Nat))",
        "bx_div_words(list(1), list(1), empty_list(Nat), empty_list(Nat))",
        "bx_gcd_block(16, list(1), list(1))",
        "bx_parse_blocks(1, \"1\", 0, empty_list(Nat), 16384)",
        "bq_slash_block(16, \"1/2\", 0)",
        "bx_work_limit(16384)",
    ]
    .iter()
    .enumerate()
    {
        let p = dir.join(format!("bad{i}.dlm"));
        std::fs::write(&p,format!("module test.private_wide\nimport std.exact\ntheory App extends StdExact {{ let value = {e} }}\n")).unwrap();
        let rejected = match load_program_with_paths(&p, &[repo().join("lib")]) {
            Err(_) => true,
            Ok(p) => !Checker::with_policy(CheckPolicy::trusted_only())
                .check_program(&p)
                .ok(),
        };
        assert!(rejected, "{e}");
    }
    std::fs::remove_dir_all(dir).unwrap();
}
