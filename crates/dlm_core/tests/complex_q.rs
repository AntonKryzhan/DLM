//! v0.100.1 — additive exact Gaussian rationals Q(i) APIs: host API,
//! ordinary DLM `std.complex_q`, interpreter and native C, against independent
//! Python Fraction fixtures (`tools/generate_complex097_oracle.py`).
use dlm_core::math::complex::ComplexRational;
use dlm_core::math::exact::{ExactBudget, Natural, Rational};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/complex_q097.tsv");

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn wide() -> ExactBudget {
    ExactBudget::new(16384, u64::MAX / 4).unwrap()
}

fn complex(text: &str) -> ComplexRational {
    ComplexRational::parse(text, &mut wide()).unwrap_or_else(|e| panic!("{text}: {e}"))
}

struct Row<'a> {
    op: &'a str,
    bits: usize,
    z: &'a str,
    w: &'a str,
    value: &'a str,
    source: u8,
}

fn rows() -> Vec<Row<'static>> {
    FIXTURES
        .lines()
        .filter(|l| !l.starts_with('#'))
        .map(|l| {
            let f: Vec<&str> = l.split('\t').collect();
            Row {
                op: f[0],
                bits: f[1].parse().unwrap(),
                z: f[2],
                w: f[3],
                value: f[4],
                source: f[5].parse().unwrap(),
            }
        })
        .collect()
}

/// Host result as fixture text, or `ERR:<message>`.
fn host(
    op: &str,
    z: &ComplexRational,
    w: Option<&ComplexRational>,
    budget: &mut ExactBudget,
) -> String {
    let w = || w.expect("binary operation");
    let shown = |r: Result<ComplexRational, dlm_core::math::MathError>| match r {
        Ok(v) => v.to_string(),
        Err(e) => format!("ERR:{e}"),
    };
    match op {
        "add" => shown(z.checked_add(w(), budget)),
        "sub" => shown(z.checked_sub(w(), budget)),
        "mul" => shown(z.checked_mul(w(), budget)),
        "div" => shown(z.checked_div(w(), budget)),
        "inv" => shown(z.checked_inv(budget)),
        "neg" => shown(z.checked_neg(budget)),
        "conj" => shown(z.checked_conj(budget)),
        "norm" => match z.checked_norm(budget) {
            Ok(v) => v.to_string(),
            Err(e) => format!("ERR:{e}"),
        },
        "eq" => match z.checked_eq(w(), budget) {
            Ok(v) => v.to_string(),
            Err(e) => format!("ERR:{e}"),
        },
        other => panic!("unknown operation {other}"),
    }
}

#[test]
fn host_operations_match_independent_fraction_oracles_with_exact_precision_boundaries() {
    let rows = rows();
    assert_eq!(rows.len(), 1944);
    let mut boundaries = 0;
    for row in &rows {
        let z = complex(row.z);
        let w = (row.w != "-").then(|| complex(row.w));
        let mut budget = ExactBudget::new(row.bits, u64::MAX / 4).unwrap();
        let got = host(row.op, &z, w.as_ref(), &mut budget);
        let expected = if row.value == "ERR:zero" {
            "ERR:exact complex division by zero"
        } else {
            row.value
        };
        assert_eq!(
            got, expected,
            "{} {} {} at {} bits",
            row.op, row.z, row.w, row.bits
        );
        if row.bits > 1 && row.value != "ERR:zero" {
            let mut narrow = ExactBudget::new(row.bits - 1, u64::MAX / 4).unwrap();
            let got = host(row.op, &z, w.as_ref(), &mut narrow);
            assert!(
                got.starts_with("ERR:UnknownWithinBudget") && got.contains("precision"),
                "{} {} {} at {} bits: {got}",
                row.op,
                row.z,
                row.w,
                row.bits - 1
            );
            boundaries += 1;
        }
    }
    assert_eq!(boundaries, 1867);
    assert!(
        rows.iter().any(|r| r.bits > 16000),
        "16384-bit host profile is exercised"
    );
}

#[test]
fn host_field_identities_hold_on_a_grid() {
    let parts = ["0", "1", "-1", "1/2", "-2/3", "5/7", "3"];
    let mut values = Vec::new();
    for a in parts {
        for b in parts {
            let b_text = b
                .strip_prefix('-')
                .map(|m| format!("-{m}"))
                .unwrap_or_else(|| format!("+{b}"));
            values.push(complex(&format!("{a}{b_text}i")));
        }
    }
    let one = ComplexRational::one();
    let b = &mut wide();
    let i = ComplexRational::i();
    assert_eq!(i.checked_mul(&i, b).unwrap().to_string(), "-1/1+0/1i");
    for z in &values {
        assert_eq!(&z.checked_conj(b).unwrap().checked_conj(b).unwrap(), z);
        assert_eq!(
            z.checked_add(&z.checked_neg(b).unwrap(), b).unwrap(),
            ComplexRational::zero()
        );
        let n = z.checked_norm(b).unwrap();
        assert!(!n.negative());
        let product = z.checked_mul(&z.checked_conj(b).unwrap(), b).unwrap();
        assert_eq!(
            product,
            ComplexRational::from_rational(n.clone(), b).unwrap()
        );
        if !z.is_zero() {
            assert_eq!(z.checked_mul(&z.checked_inv(b).unwrap(), b).unwrap(), one);
            assert_eq!(z.checked_div(z, b).unwrap(), one);
        }
        for w in &values {
            assert_eq!(z.checked_mul(w, b).unwrap(), w.checked_mul(z, b).unwrap());
            assert_eq!(z.checked_add(w, b).unwrap().checked_sub(w, b).unwrap(), *z);
            let zw = z.checked_mul(w, b).unwrap();
            let nw = w.checked_norm(b).unwrap();
            assert_eq!(zw.checked_norm(b).unwrap(), n.checked_mul(&nw, b).unwrap());
            assert_eq!(
                z.checked_conj(b)
                    .unwrap()
                    .checked_mul(&w.checked_conj(b).unwrap(), b)
                    .unwrap(),
                zw.checked_conj(b).unwrap()
            );
            if !w.is_zero() {
                assert_eq!(zw.checked_div(w, b).unwrap(), *z);
                assert_eq!(
                    z.checked_div(w, b).unwrap(),
                    z.checked_mul(&w.checked_inv(b).unwrap(), b).unwrap()
                );
            }
        }
    }
}

#[test]
fn host_text_is_strict_and_budgets_are_threaded() {
    for text in ["1/2-3/4i", "-1/1+0/1i", "0/1+0/1i", "-7/3-5/1i"] {
        assert_eq!(complex(text).to_string(), text);
    }
    assert_eq!(complex("2/4+6i").to_string(), "1/2+6/1i");
    assert_eq!(complex("-0-0i").to_string(), "0/1+0/1i");
    for bad in [
        "", "i", "1", "1+2", "+1+2i", "1+-2i", "1++2i", "1 + 2i", "1+2j", "1/0+1i", "1+1/0i",
        "1+2ii", "--1+2i", "1+i", "1+/2i", "1/2/3+1i",
    ] {
        assert!(
            ComplexRational::parse(bad, &mut wide()).is_err(),
            "accepted `{bad}`"
        );
    }
    let mut tiny = ExactBudget::new(8, 1_000).unwrap();
    let long = format!("{}+1i", "1".repeat(40));
    assert!(ComplexRational::parse(&long, &mut tiny)
        .unwrap_err()
        .0
        .starts_with("UnknownWithinBudget"));

    // Work is threaded through the component operations and exhausts honestly.
    let z = complex("12345/678-910/1112i");
    let w = complex("-1314/1516+1718/1920i");
    let mut budget = ExactBudget::new(256, 1_000_000).unwrap();
    let product = z.checked_mul(&w, &mut budget).unwrap();
    let used = budget.work_used();
    assert!(used > 0);
    let quotient = product.checked_div(&w, &mut budget).unwrap();
    assert_eq!(quotient, z);
    assert!(budget.work_used() > used);
    let mut starved = ExactBudget::new(256, used - 1).unwrap();
    let error = z.checked_mul(&w, &mut starved).unwrap_err();
    assert!(error.0.starts_with("UnknownWithinBudget"), "{error}");
    // Operands are validated against the precision before any shortcut.
    let wide_value = ComplexRational::new(
        Rational::from_integer(1 << 40),
        Rational::zero(),
        &mut wide(),
    )
    .unwrap();
    let mut narrow = ExactBudget::new(16, 1_000_000).unwrap();
    assert!(wide_value
        .checked_div(&ComplexRational::zero(), &mut narrow)
        .unwrap_err()
        .0
        .contains("precision"));
    assert!(wide_value.checked_conj(&mut narrow).is_err());
    assert!(ComplexRational::new(
        Rational::from_integer(1 << 40),
        Rational::zero(),
        &mut narrow
    )
    .is_err());
    assert_eq!(Natural::from_u128(5).to_string(), "5");
}

// ---------- ordinary DLM std.complex_q, interpreter and native C ----------

fn words(n: &Natural) -> String {
    let mut n = n.clone();
    let mut words = Vec::new();
    while !n.is_zero() {
        let (q, r) = n.div_rem(&Natural::from_u128(65536), &mut wide()).unwrap();
        words.push(r.to_string());
        n = q;
    }
    if words.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!("list({})", words.join(", "))
    }
}

fn source_rational(r: &Rational) -> String {
    format!(
        "BigRational({}, {}, {})",
        r.negative(),
        words(r.numerator()),
        words(r.denominator())
    )
}

fn source_complex(text: &str) -> String {
    let z = complex(text);
    format!(
        "BigComplex({}, {})",
        source_rational(z.re()),
        source_rational(z.im())
    )
}

fn header() -> String {
    r#"module test.complex_q
import std.complex_q
theory App extends StdComplexQ {
 fn show(r: Result<BigComplexCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match bigc_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn qshow(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn flag(r: Result<BigComplexFlag, Text>) -> Text = match r { err(e) => e, ok(v) => if_then_else(field(v, value), "true", "false") }
"#
    .into()
}

fn call(op: &str, budget: &str, z: &str, w: &str) -> String {
    match op {
        "add" | "sub" | "mul" | "div" => format!("show(bigc_{op}({budget}, {z}, {w}))"),
        "inv" | "neg" | "conj" => format!("show(bigc_{op}({budget}, {z}))"),
        "norm" => format!("qshow(bigc_norm({budget}, {z}))"),
        "eq" => format!("flag(bigc_eq({budget}, {z}, {w}))"),
        other => panic!("unknown operation {other}"),
    }
}

fn run_both(source: &str, expected: &[String], name: &str) {
    let dir = std::env::temp_dir().join(format!("dlm-complex-q-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "{name}: interpreter"
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
        expected.join("\n"),
        "{name}: native C"
    );
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn source_operations_match_oracles_and_native_c_with_precision_boundaries() {
    let mut source = header();
    let mut expected = Vec::new();
    let (mut vectors, mut boundaries) = (0, 0);
    for row in rows().iter().filter(|r| r.source > 0) {
        let z = source_complex(row.z);
        let w = if row.w == "-" {
            String::new()
        } else {
            source_complex(row.w)
        };
        let budget = format!("ExactBudget({}, 1000000000000000)", row.bits);
        source += &format!(" print_line({})\n", call(row.op, &budget, &z, &w));
        expected.push(if row.value == "ERR:zero" {
            "exact complex division by zero".to_string()
        } else {
            row.value.to_string()
        });
        vectors += 1;
        if row.source == 2 && row.bits > 1 {
            let narrow = format!("ExactBudget({}, 1000000000000000)", row.bits - 1);
            source += &format!(" print_line({})\n", call(row.op, &narrow, &z, &w));
            expected.push("UnknownWithinBudget: source precision exceeded".into());
            boundaries += 1;
        }
    }
    source += "}\n";
    assert_eq!((vectors, boundaries), (118, 47));
    run_both(&source, &expected, "oracles");
}

#[test]
fn source_text_errors_raw_records_and_budget_threading() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(64, 1000000000000000)
 let zero = BigRational(false, empty_list(Nat), list(1))
 let one = BigRational(false, list(1), list(1))
 let z = BigComplex(BigRational(false, list(1), list(2)), BigRational(true, list(3), list(4)))
 print_line(show(bigc_parse(b, "1/2-3/4i")))
 print_line(show(bigc_parse(b, "2/4+6i")))
 print_line(show(bigc_parse(b, "-0-0i")))
 print_line(show(bigc_i(b)))
 print_line(show(bigc_from_rational(b, one)))
 print_line(show(bigc_parse(b, "1+2")))
 print_line(show(bigc_parse(b, "+1+2i")))
 print_line(show(bigc_parse(b, "1+-2i")))
 print_line(show(bigc_parse(b, "1+i")))
 print_line(show(bigc_parse(b, "")))
 print_line(show(bigc_parse(b, "1/0+1i")))
 print_line(show(bigc_parse(ExactBudget(2, 1000), "10000000000000000+1i")))
 print_line(show(bigc_div(b, z, BigComplex(zero, zero))))
 print_line(show(bigc_inv(b, BigComplex(zero, zero))))
 print_symbolic(is_err(bigc_check(b, BigComplex(BigRational(false, list(2), list(4)), zero))))
 print_symbolic(is_err(bigc_check(b, BigComplex(zero, BigRational(true, empty_list(Nat), list(1))))))
 print_symbolic(is_err(bigc_check(b, BigComplex(zero, BigRational(false, list(1), empty_list(Nat))))))
 print_symbolic(is_err(bigc_check(b, BigComplex(BigRational(false, list(65536), list(1)), zero))))
 print_symbolic(is_err(bigc_mul(b, BigComplex(BigRational(false, list(1, 0), list(1)), zero), z)))
 print_symbolic(is_err(bigc_div(b, BigComplex(BigRational(false, list(2), list(4)), zero), BigComplex(zero, zero))))
 print_symbolic(is_err(bigc_add(ExactBudget(0, 1000), z, z)))
 print_symbolic(is_err(bigc_add(ExactBudget(16385, 1000), z, z)))
 print_symbolic(is_err(bigc_add(ExactBudget(8, 1000000000000001), z, z)))
 print_symbolic(is_err(bigc_mul(ExactBudget(64, 0), z, z)))
 let start = ExactBudget(64, 1000000000)
 let product = unwrap_or(bigc_mul(start, z, z), BigComplexCalc(z, ExactBudget(64, 0)))
 print_symbolic(1000000000 > field(field(product, budget), work))
 print_decimal(field(start, work))
 print_line(show(ok(product, Text)))
 print_line(show(bigc_div(field(product, budget), field(product, value), z)))
 print_line(flag(bigc_eq(b, z, z)))
 print_line(flag(bigc_eq(b, z, BigComplex(field(z, im), field(z, re)))))
 print_line(qshow(bigc_norm(b, z)))
 print_line(show(bigc_conj(b, z)))
 print_line(show(bigc_neg(b, z)))
}
"#;
    let expected = [
        "1/2-3/4i",
        "1/2+6/1i",
        "0/1+0/1i",
        "0/1+1/1i",
        "1/1+0/1i",
        "invalid exact complex text: expected <re>+<im>i or <re>-<im>i",
        "invalid exact decimal",
        "invalid exact decimal",
        "invalid exact complex text: expected <re>+<im>i or <re>-<im>i",
        "invalid exact complex text: expected <re>+<im>i or <re>-<im>i",
        "exact rational denominator is zero",
        "UnknownWithinBudget: exact complex input exceeded",
        "exact complex division by zero",
        "exact complex division by zero",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "true",
        "1000000000",
        "-5/16-3/4i",
        "1/2-3/4i",
        "true",
        "false",
        "13/16",
        "1/2+3/4i",
        "-1/2+3/4i",
    ]
    .map(String::from);
    run_both(&source, &expected, "errors");
}

#[test]
fn source_helpers_remain_private() {
    let dir = std::env::temp_dir().join(format!("dlm-complex-q-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (i, body) in [
        "let x = bc_zero()",
        "let x = bc_split(0, \"\", 0, none(Nat))",
        "let x = bc_split_block(0, \"\", 0, none(Nat))",
        "let x = bc_norm(exact_default_budget(), BigRational(false, list(1), list(1)), BigRational(false, list(1), list(1)))",
        "fn bc_is_zero(z: BigComplex) -> Bool = true",
    ]
    .iter()
    .enumerate()
    {
        let path = dir.join(format!("bad{i}.dlm"));
        std::fs::write(&path, format!("module test.private_complex\nimport std.complex_q\ntheory App extends StdComplexQ {{\n {body}\n}}\n")).unwrap();
        let rejected = match load_program_with_paths(&path, &[repo().join("lib")]) {
            Err(_) => true,
            Ok(p) => !Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p).ok(),
        };
        assert!(rejected, "private helper escaped: {body}");
    }
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn source_complex_apis_coexist_at_wide_precision_and_text_limits() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(16384, 18446744073709551615)
 let zero = BigRational(false, empty_list(Nat), list(1))
 let re = BigRational(false, list(1), list(2))
 let im = BigRational(true, list(3), list(4))
 let z = BigComplex(re, im)
 let old = ComplexQ(re, im)
 print_line(match complexq_text(b, old) { err(e) => e, ok(t) => field(t, value) })
 print_line(show(bigc_check(b, z)))
 print_line(show(bigc_add(ExactBudget(1025, 1000), z, z)))
 print_line(show(bigc_add(ExactBudget(1025, 1000000000000000), z, z)))
 print_line(show(bigc_check(ExactBudget(16385, 18446744073709551615), z)))
"#;
    let mut high_words = vec!["0"; 1023];
    high_words.push("32768");
    source += &format!(
        " let high = BigComplex(BigRational(false, list({}), list(1)), zero)\n",
        high_words.join(", ")
    );
    source += r#"
 print_symbolic(not(is_err(bigc_check(b, high))))
 print_line(show(bigc_check(ExactBudget(16383, 18446744073709551615), high)))
"#;
    let digits = format!("{}1", "0".repeat(16384));
    let component = format!("{digits}/{digits}");
    let padded = format!("{component}+{component}i");
    assert_eq!(padded.len(), 4 * 16384 + 8);
    assert_eq!(complex(&padded).to_string(), "1/1+1/1i");
    source += &format!(" print_line(show(bigc_parse(b, \"{padded}\")))\n");
    source += &format!(" print_line(show(bigc_parse(b, \"0{padded}\")))\n");
    source += "}\n";
    let expected = [
        "1/2;-3/4",
        "1/2-3/4i",
        "UnknownWithinBudget: source work reservation exhausted",
        "1/1-3/2i",
        "invalid exact source budget",
        "true",
        "UnknownWithinBudget: source precision exceeded",
        "1/1+1/1i",
        "UnknownWithinBudget: exact complex input exceeded",
    ]
    .map(String::from);
    run_both(&source, &expected, "compatibility-wide-text");
}
