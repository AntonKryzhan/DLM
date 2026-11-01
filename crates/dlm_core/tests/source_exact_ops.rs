use dlm_core::math::exact::{ExactBudget, Integer, Natural, Rational};
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
    let dir = std::env::temp_dir().join(format!("dlm-exact-ops-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "{name}"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(
        String::from_utf8(output.stdout).unwrap().trim_end(),
        expected.join("\n")
    );
    std::fs::remove_dir_all(dir).unwrap();
}

fn words(n: &Natural) -> String {
    let mut n = n.clone();
    let mut words = Vec::new();
    while !n.is_zero() {
        let (q, r) = n
            .div_rem(&Natural::from_u128(65536), &mut ExactBudget::default())
            .unwrap();
        words.push(r.to_string());
        n = q;
    }
    if words.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!("list({})", words.join(", "))
    }
}

fn integer(text: &str) -> String {
    let n = Integer::parse(text, &mut ExactBudget::default()).unwrap();
    format!("BigInt({}, {})", n.negative(), words(n.magnitude()))
}

fn rational(text: &str) -> String {
    let n = Rational::parse(text, &mut ExactBudget::default()).unwrap();
    format!(
        "BigRational({}, {}, {})",
        n.negative(),
        words(n.numerator()),
        words(n.denominator())
    )
}

fn header() -> String {
    r#"module test.exact_ops
import std.exact
theory App extends StdExact {
 fn show(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn qshow(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn rootshow(r: Result<BigSquareRoot, Text>) -> Text = match r { err(e) => e, ok(v) => match big_text(field(v, budget), field(v, root)) { err(e) => e, ok(s) => match big_text(field(s, budget), field(v, remainder)) { err(e) => e, ok(t) => concat(field(s, value), concat(",", field(t, value))) } } }
"#.into()
}

#[test]
fn source_operations_match_96_independent_oracles_and_native_c_including_1024_bits() {
    let mut source = header();
    let mut expected = Vec::new();
    for line in include_str!("fixtures/exact_ops096.tsv").lines() {
        if line.starts_with('#') {
            continue;
        }
        let f: Vec<_> = line.split('\t').collect();
        if f[6] != "1" {
            continue;
        }
        let b = format!("ExactBudget({}, 1000000000000000)", f[1]);
        match f[0] {
            "pow" => {
                source += &format!(
                    " print_line(show(big_pow({b}, {}, {})))\n",
                    integer(f[2]),
                    f[3]
                )
            }
            "qpow" => {
                let exponent = if f[3].starts_with('-') {
                    f[3].into()
                } else {
                    format!("int({})", f[3])
                };
                source += &format!(
                    " print_line(qshow(bigrat_pow({b}, {}, {exponent})))\n",
                    rational(f[2])
                );
            }
            "lcm" => {
                source += &format!(
                    " print_line(show(big_lcm({b}, {}, {})))\n",
                    integer(f[2]),
                    integer(f[3])
                )
            }
            "sqrt" => {
                source += &format!(
                    " print_line(rootshow(big_sqrt_rem({b}, {})))\n",
                    integer(f[2])
                )
            }
            op => panic!("unknown operation {op}"),
        }
        expected.push(if f[0] == "sqrt" {
            format!("{},{}", f[4], f[5])
        } else {
            f[4].into()
        });
    }
    source += "}\n";
    assert_eq!(expected.len(), 96);
    check(&source, &expected, "oracles");
}

#[test]
fn source_errors_validate_raw_records_domains_and_inputs_even_for_zero_exponents() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(8, 1000000000000000)
 let zero = BigInt(false, empty_list(Nat))
 let one = BigInt(false, list(1))
 let qzero = BigRational(false, empty_list(Nat), list(1))
 print_symbolic(is_err(big_pow(b, BigInt(true, empty_list(Nat)), 0)))
 print_symbolic(is_err(big_pow(b, BigInt(false, list(1, 0)), 0)))
 print_symbolic(is_err(big_pow(b, BigInt(false, list(65536)), 0)))
 print_symbolic(is_err(big_pow(ExactBudget(1, 1000000000000000), BigInt(false, list(2)), 0)))
 print_symbolic(is_err(big_lcm(b, zero, BigInt(true, empty_list(Nat)))))
 print_symbolic(is_err(big_lcm(b, BigInt(false, list(128)), BigInt(false, list(3)))))
 print_symbolic(is_err(big_sqrt_rem(b, BigInt(true, list(1)))))
 print_symbolic(is_err(big_sqrt_rem(b, BigInt(true, empty_list(Nat)))))
 print_symbolic(is_err(big_sqrt_rem(b, BigInt(false, list(1, 0)))))
 print_symbolic(is_err(bigrat_pow(b, BigRational(false, list(2), list(4)), int(0))))
 print_symbolic(is_err(bigrat_pow(b, BigRational(false, list(1), empty_list(Nat)), int(0))))
 print_symbolic(is_err(bigrat_pow(b, BigRational(true, empty_list(Nat), list(1)), int(0))))
 print_symbolic(is_err(bigrat_pow(b, qzero, -1)))
 print_symbolic(is_err(bigrat_pow(b, BigRational(false, list(2), list(3)), int(8))))
 print_symbolic(is_err(big_pow(ExactBudget(0, 100), one, 0)))
 print_symbolic(is_err(big_lcm(ExactBudget(16385, 100), one, one)))
 print_symbolic(is_err(big_sqrt_rem(ExactBudget(8, 1000000000000001), one)))
 print_symbolic(is_err(bigrat_pow(ExactBudget(340282366920938463463374607431768211455, 100), qzero, int(0))))
}
"#;
    check(&source, &vec!["true".into(); 18], "invalid");
}

#[test]
fn source_operations_thread_budgets_and_preserve_original_values() {
    let mut source = header();
    source += r#"
 let one = BigInt(false, list(1))
 let two = BigInt(false, list(2))
 let fallback = BigIntCalc(one, ExactBudget(8, 0))
 let start = ExactBudget(8, 100000000)
 let p = unwrap_or(big_pow(start, two, 3), fallback)
 print_decimal(field(field(p, budget), work))
 print_decimal(field(start, work))
 print_line(show(big_pow(ExactBudget(2, 100000000), two, 1)))
 print_symbolic(is_err(big_pow(ExactBudget(8, 30000000), two, 3)))
 print_symbolic(is_err(big_pow(ExactBudget(8, 0), one, 0)))
 print_symbolic(is_err(big_lcm(ExactBudget(8, 0), one, one)))
 print_symbolic(is_err(big_sqrt_rem(ExactBudget(8, 0), one)))
 print_symbolic(is_err(bigrat_pow(ExactBudget(8, 0), BigRational(false, list(1), list(1)), int(0))))
 print_line(show(big_lcm(ExactBudget(8, 100000000), BigInt(true, list(255)), BigInt(false, list(255)))))
 print_line(qshow(bigrat_pow(ExactBudget(2, 100000000), BigRational(false, list(2), list(3)), -1)))
 print_line(rootshow(big_sqrt_rem(ExactBudget(1, 100000000), one)))
 print_symbolic(is_err(big_lcm(ExactBudget(8, 30000000), two, two)))
 print_symbolic(is_err(bigrat_pow(ExactBudget(8, 30000000), BigRational(false, list(1), list(2)), int(3))))
 print_symbolic(is_err(big_pow(field(p, budget), one, 7)))
 print_line(show(ok(p, Text)))
}
"#;
    let expected = [
        "50000000",
        "100000000",
        "2",
        "true",
        "true",
        "true",
        "true",
        "true",
        "255",
        "3/2",
        "1,0",
        "true",
        "true",
        "true",
        "8",
    ]
    .map(String::from);
    check(&source, &expected, "budgets");
}

#[test]
fn source_root_and_power_helpers_remain_private_and_cannot_be_shadowed() {
    let dir = std::env::temp_dir().join(format!("dlm-exact-ops-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (i, body) in [
        "let x = bx_power(0, exact_default_budget(), BigInt(false, list(1)), 0, BigInt(false, list(1)))",
        "let x = bx_sqrt_loop(0, empty_list(Nat), empty_list(Nat), empty_list(Nat))",
        "let x = bq_power(0, exact_default_budget(), BigRational(false, list(1), list(1)), 0, BigRational(false, list(1), list(1)))",
        "fn bx_sqrt_loop(n: Nat, a: List<Nat>, root: List<Nat>, remainder: List<Nat>) -> WordSquareRoot = WordSquareRoot(root, remainder)",
    ].iter().enumerate() {
        let path = dir.join(format!("bad{i}.dlm"));
        std::fs::write(&path, format!("module test.private_ops\nimport std.exact\ntheory App extends StdExact {{\n {body}\n}}\n")).unwrap();
        let rejected = match load_program_with_paths(&path, &[repo().join("lib")]) {
            Err(_) => true,
            Ok(p) => !Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p).ok(),
        };
        assert!(rejected, "private helper escaped: {body}");
    }
    std::fs::remove_dir_all(dir).unwrap();
}
