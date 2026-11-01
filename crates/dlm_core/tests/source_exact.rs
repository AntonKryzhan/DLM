//! Exact ordinary DLM arithmetic compared with host arithmetic and native C.
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
    let dir = std::env::temp_dir().join(format!("dlm-source-exact-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "interpreter: {name}"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap_or_else(|e| panic!("{e:?}"));
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        String::from_utf8(out.stdout).unwrap().trim_end(),
        expected.join("\n"),
        "C: {name}"
    );
    std::fs::remove_dir_all(dir).unwrap();
}
fn b() -> ExactBudget {
    ExactBudget::default()
}
fn words(n: &Natural) -> String {
    let mut n = n.clone();
    let mut out = Vec::new();
    let radix = Natural::from_u128(65536);
    while !n.is_zero() {
        let (q, r) = n.div_rem(&radix, &mut b()).unwrap();
        out.push(r.to_string());
        n = q;
    }
    if out.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!("list({})", out.join(", "))
    }
}
fn integer(n: &Integer) -> String {
    format!("BigInt({}, {})", n.negative(), words(n.magnitude()))
}
fn rational(n: &Rational) -> String {
    format!(
        "BigRational({}, {}, {})",
        n.negative(),
        words(n.numerator()),
        words(n.denominator())
    )
}
fn header() -> String {
    r#"module test.exact
import std.exact
theory App extends StdExact {
 let b = ExactBudget(1024, 1000000000000000)
 fn show(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => concat("ERR:", e), ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => concat("ERR:", e), ok(t) => field(t, value) } }
 fn qshow(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => concat("ERR:", e), ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => concat("ERR:", e), ok(t) => field(t, value) } }
 fn comparison(r: Result<BigCompare, Text>) -> Int = match r { err(e) => int(999), ok(v) => field(v, order) }
 fn division(r: Result<BigDivision, Text>) -> Text = match r { err(e) => concat("ERR:", e), ok(v) => match big_text(field(v, budget), field(v, quotient)) { err(e) => concat("ERR:", e), ok(q) => match big_text(field(q, budget), field(v, remainder)) { err(e) => concat("ERR:", e), ok(rest) => concat(field(q, value), concat(",", field(rest, value))) } } }
"#.into()
}
#[test]
fn source_integer_carries_borrows_signs_products_and_euclidean_division() {
    let pairs = [
        ("0", "0"),
        ("1", "-1"),
        ("-7", "3"),
        ("7", "-3"),
        ("-7", "-3"),
        ("65535", "1"),
        ("65536", "65535"),
        ("-65536", "65535"),
        ("4294967295", "65537"),
        (
            "340282366920938463463374607431768211456",
            "18446744073709551617",
        ),
        ("-340282366920938463463374607431768211457", "65537"),
        (
            "1461501637330902918203684832716283019655932542975",
            "1208925819614629174706177",
        ),
    ];
    let mut source = header();
    let mut expected = Vec::new();
    for (a, c) in pairs {
        let a = Integer::parse(a, &mut b()).unwrap();
        let c = Integer::parse(c, &mut b()).unwrap();
        let sa = integer(&a);
        let sc = integer(&c);
        for (op, result) in [
            ("big_add", a.checked_add(&c, &mut b()).unwrap()),
            ("big_sub", a.checked_sub(&c, &mut b()).unwrap()),
            ("big_mul", a.checked_mul(&c, &mut b()).unwrap()),
        ] {
            source += &format!(" print_line(show({op}(b, {sa}, {sc})))\n");
            expected.push(result.to_string());
        }
        source += &format!(" print_decimal(comparison(big_cmp(b, {sa}, {sc})))\n");
        expected.push((a.checked_cmp(&c, &mut b()).unwrap() as i32).to_string());
        source += &format!(" print_line(show(big_gcd(b, {sa}, {sc})))\n");
        expected.push(
            a.magnitude()
                .checked_gcd(c.magnitude(), &mut b())
                .unwrap()
                .to_string(),
        );
        if !c.magnitude().is_zero() {
            let (q, r) = a.div_rem(&c, &mut b()).unwrap();
            source += &format!(" print_line(division(big_div_rem(b, {sa}, {sc})))\n");
            expected.push(format!("{q},{r}"));
        }
    }
    source += "}\n";
    check(&source, &expected, "integer");
}
#[test]
fn source_rationals_match_host_including_wide_cancellation_and_exact_comparison() {
    let samples = ["0", "1/2", "-1/2", "2/3", "-2/3"];
    let mut pairs = Vec::new();
    for a in samples {
        for c in samples {
            pairs.push((a.to_string(), c.to_string()));
        }
    }
    pairs.extend([
        (
            "1461501637330902918203684832716283019655932542977/3".into(),
            "-1461501637330902918203684832716283019655932542977/3".into(),
        ),
        (
            "340282366920938463463374607431768211455/2".into(),
            "340282366920938463463374607431768211455/2".into(),
        ),
        (
            "1461501637330902918203684832716283019655932542977/3".into(),
            "3/1461501637330902918203684832716283019655932542977".into(),
        ),
    ]);
    let mut source = header();
    let mut expected = Vec::new();
    for (a, c) in pairs {
        let a = Rational::parse(&a, &mut b()).unwrap();
        let c = Rational::parse(&c, &mut b()).unwrap();
        let sa = rational(&a);
        let sc = rational(&c);
        for (op, result) in [
            ("bigrat_add", a.checked_add(&c, &mut b()).unwrap()),
            ("bigrat_sub", a.checked_sub(&c, &mut b()).unwrap()),
            ("bigrat_mul", a.checked_mul(&c, &mut b()).unwrap()),
        ] {
            source += &format!(" print_line(qshow({op}(b, {sa}, {sc})))\n");
            expected.push(result.to_string());
        }
        if !c.is_zero() {
            source += &format!(" print_line(qshow(bigrat_div(b, {sa}, {sc})))\n");
            expected.push(a.checked_div(&c, &mut b()).unwrap().to_string());
        }
        source += &format!(" print_decimal(comparison(bigrat_cmp(b, {sa}, {sc})))\n");
        expected.push((a.checked_cmp(&c, &mut b()).unwrap() as i32).to_string());
    }
    source += "}\n";
    check(&source, &expected, "rational");
}
#[test]
fn decimal_parsing_formatting_and_1024_bit_boundary_match_host_and_c() {
    let mut large = Natural::from_u128(1);
    for _ in 0..1023 {
        large = large.checked_add(&large, &mut b()).unwrap();
    }
    let mut source = header();
    let mut expected = Vec::new();
    for text in [
        "0".to_string(),
        "-000".into(),
        i128::MIN.to_string(),
        "340282366920938463463374607431768211456".into(),
        large.to_string(),
    ] {
        source += &format!(" print_line(show(big_parse(b, \"{text}\")))\n");
        expected.push(Integer::parse(&text, &mut b()).unwrap().to_string());
    }
    let high = format!("BigInt(false, {})", words(&large));
    source += &format!(" print_symbolic(is_err(big_add(b, {high}, {high})))\n");
    expected.push("true".into());
    for text in [
        "-000/0007",
        "-6/8",
        "4294967296/3",
        "-340282366920938463463374607431768211456/3",
    ] {
        source += &format!(" print_line(qshow(bigrat_parse(b, \"{text}\")))\n");
        expected.push(Rational::parse(text, &mut b()).unwrap().to_string());
    }
    let n = Integer::from_i128(-6);
    let d = Integer::from_i128(-8);
    source += &format!(
        " print_line(qshow(bigrat_from_ints(b, {}, {})))\n",
        integer(&n),
        integer(&d)
    );
    expected.push("3/4".into());
    source += " print_line(show(big_from_int(b, -170141183460469231731687303715884105728)))\n}\n";
    expected.push(i128::MIN.to_string());
    check(&source, &expected, "text");
}
#[test]
fn raw_records_oversized_nats_noncanonical_values_and_zero_division_are_rejected() {
    let source = r#"module test.invalid_exact
import std.exact
theory App extends StdExact {
 let b = ExactBudget(256, 1000000000000000)
 let zero = BigInt(false, empty_list(Nat))
 let one = BigInt(false, list(1))
 let half = BigRational(false, list(1), list(2))
 print_symbolic(is_err(exact_budget(0, 100)))
 print_symbolic(is_err(exact_budget(16385, 100)))
 print_symbolic(is_err(exact_budget(256, 1000000000000001)))
 print_symbolic(is_err(big_add(ExactBudget(340282366920938463463374607431768211455, 100), one, one)))
 print_symbolic(is_err(big_add(b, BigInt(false, list(340282366920938463463374607431768211455)), one)))
 print_symbolic(is_err(big_add(b, BigInt(false, list(1, 0)), one)))
 print_symbolic(is_err(big_mul(b, BigInt(true, empty_list(Nat)), one)))
 print_symbolic(is_err(big_text(b, BigInt(false, list(0)))))
 print_symbolic(is_err(big_div_rem(b, one, zero)))
 print_symbolic(is_err(bigrat_from_ints(b, one, zero)))
 print_symbolic(is_err(bigrat_add(b, BigRational(false, list(2), list(4)), half)))
 print_symbolic(is_err(bigrat_cmp(b, BigRational(false, list(1), empty_list(Nat)), half)))
 print_symbolic(is_err(bigrat_text(b, BigRational(true, empty_list(Nat), list(1)))))
 print_symbolic(is_err(bigrat_div(b, half, BigRational(false, empty_list(Nat), list(1)))))
 print_symbolic(is_err(bigrat_inv(b, BigRational(false, empty_list(Nat), list(1)))))
 print_symbolic(is_err(bigrat_parse(b, "1/0")))
 print_symbolic(is_err(bigrat_parse(b, "1/-2")))
 print_symbolic(is_err(bigrat_parse(b, "1/2/3")))
 print_symbolic(is_err(big_parse(b, "+1")))
 print_symbolic(is_err(big_parse(b, " 1")))
 print_symbolic(is_err(big_parse(b, "١")))
 print_symbolic(is_err(big_parse(b, "")))
}
"#;
    check(source, &vec!["true".into(); 22], "invalid");
}
#[test]
fn precision_errors_and_immutable_budget_threading_are_explicit() {
    let mut source = header();
    let mut expected = Vec::new();
    source += r#"
 let one = BigInt(false, list(1))
 let start = ExactBudget(128, 20000000)
 let first = unwrap_or(big_add(start, one, one), BigIntCalc(BigInt(false, empty_list(Nat)), start))
 print_decimal(field(field(first, budget), work))
 print_symbolic(is_err(big_add(field(first, budget), field(first, value), one)))
 print_decimal(field(start, work))
 print_line(show(big_from_words(b, true, list(0, 0))))
 print_symbolic(is_err(big_add(ExactBudget(1, 1000000000000000), one, one)))
 print_symbolic(is_err(big_add(ExactBudget(128, 0), one, one)))
 print_symbolic(is_err(bigrat_add(ExactBudget(128, 0), BigRational(false, list(1), list(3)), BigRational(false, list(1), list(7)))))
 print_symbolic(is_err(big_parse(ExactBudget(128, 1000000000000000), "340282366920938463463374607431768211456")))
"#;
    expected.extend(
        [
            "7500000", "true", "20000000", "0", "true", "true", "true", "true",
        ]
        .map(str::to_string),
    );
    let max = Rational::parse(&format!("{}/2", u128::MAX), &mut b()).unwrap();
    source += &format!(
        " print_line(qshow(bigrat_add(ExactBudget(128, 1000000000000000), {}, {})))\n",
        rational(&max),
        rational(&max)
    );
    expected.push(format!("{}/1", u128::MAX));
    source += "}\n";
    check(&source, &expected, "budget");
}
#[test]
fn inverse_negation_comparison_and_constructor_signs_preserve_canonical_zero() {
    let source = header()
        + r#"
 let n = BigInt(true, list(3))
 let q = BigRational(true, list(3), list(7))
 print_line(show(big_neg(b, n)))
 print_line(show(big_neg(b, BigInt(false, empty_list(Nat)))))
 print_line(qshow(bigrat_inv(b, q)))
 print_line(qshow(bigrat_neg(b, q)))
 print_line(qshow(bigrat_neg(b, BigRational(false, empty_list(Nat), list(1)))))
 print_line(qshow(bigrat_from_ints(b, BigInt(false, empty_list(Nat)), n)))
 print_decimal(comparison(big_cmp(b, BigInt(true, list(65535)), BigInt(true, list(65536)))))
}
"#;
    // The last input is an invalid radix word, so the explicit error sentinel is 999.
    check(
        &source,
        &["3", "0", "-7/3", "3/7", "0/1", "0/1", "999"].map(str::to_string),
        "unary",
    );
}
#[test]
fn independent_python_integer_and_fraction_source_oracle() {
    let mut source = header();
    let mut expected = Vec::new();
    let mut count = 0;
    for line in include_str!("fixtures/source_exact094.tsv")
        .lines()
        .filter(|s| !s.starts_with('#'))
    {
        let v: Vec<_> = line.split('\t').collect();
        if v[0] == "I" {
            let a = integer(&Integer::parse(v[2], &mut b()).unwrap());
            let c = integer(&Integer::parse(v[3], &mut b()).unwrap());
            source += &match v[1] {
                "cmp" => format!(" print_decimal(comparison(big_cmp(b, {a}, {c})))\n"),
                "div" => format!(" print_line(division(big_div_rem(b, {a}, {c})))\n"),
                op => format!(" print_line(show(big_{op}(b, {a}, {c})))\n"),
            };
        } else {
            let a = rational(&Rational::parse(v[2], &mut b()).unwrap());
            let c = rational(&Rational::parse(v[3], &mut b()).unwrap());
            source += &if v[1] == "cmp" {
                format!(" print_decimal(comparison(bigrat_cmp(b, {a}, {c})))\n")
            } else {
                format!(" print_line(qshow(bigrat_{}(b, {a}, {c})))\n", v[1])
            };
        }
        expected.push(v[4].to_string());
        count += 1;
    }
    assert_eq!(count, 124);
    source += "}\n";
    check(&source, &expected, "python-oracle");
}
#[test]
fn private_helpers_and_inherited_names_cannot_bypass_validators() {
    let dir = std::env::temp_dir().join(format!("dlm-exact-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    for(body,message)in[(" print_symbolic(bx_add(list(1), list(1)))\n","private"),(" fn bx_words(xs: List<Nat>) -> Bool = true\n print_symbolic(is_err(big_add(exact_default_budget(), BigInt(false, list(65536)), BigInt(false, list(1)))))\n","redeclares")]{
        std::fs::write(&path,format!("module test.private\nimport std.exact\ntheory App extends StdExact {{\n{body}}}\n")).unwrap();
        let p=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();let report=Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
        assert!(!report.ok());assert!(report.diagnostics.iter().any(|d|d.message.contains(message)),"{:?}",report.diagnostics);assert!(lower_program(&p,CheckPolicy::trusted_only()).is_err());
    }
    std::fs::remove_dir_all(dir).unwrap();
}
