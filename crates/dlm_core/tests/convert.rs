//! v0.105 — explicit numeric conversions (N11) and the portable numeric
//! format (N12): host `math::convert`/`math::portable`, ordinary DLM
//! `std.convert`, interpreter and native C, against independent Fraction and
//! mpmath fixtures (`tools/generate_convert105_oracle.py`).
use dlm_core::math::convert::{self, RoundMode};
use dlm_core::math::creal::CReal;
use dlm_core::math::exact::{ExactBudget, Integer, Natural, Rational};
use dlm_core::math::portable::{self, PortableNumber};
use dlm_core::math::MathError;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/convert105.tsv");

fn rows() -> Vec<[&'static str; 6]> {
    FIXTURES
        .lines()
        .map(|l| {
            let f: Vec<&str> = l.split('\t').collect();
            assert_eq!(f.len(), 6, "{l}");
            [f[0], f[1], f[2], f[3], f[4], f[5]]
        })
        .collect()
}

fn wide() -> ExactBudget {
    ExactBudget::new(16384, u64::MAX / 4).unwrap()
}

fn text(t: &str) -> &str {
    if t == "<empty>" {
        ""
    } else {
        t
    }
}

fn q(t: &str) -> Rational {
    Rational::parse(t, &mut wide()).unwrap()
}

fn expected(v: &str) -> &str {
    match v {
        "ERR:nondecimal" => "rational is not a finite decimal",
        "ERR:noninteger" => "rational is not an integer",
        "ERR:invalid" => "invalid decimal number",
        "ERR:exponent" => "decimal exponent out of range",
        "ERR:log" => "computable real logarithm of a non-positive value",
        v => v,
    }
}

fn portable_category(r: Result<PortableNumber, MathError>) -> String {
    match r {
        Ok(_) => "ok".into(),
        Err(e) if e.0 == "non-canonical portable number" => "noncanonical".into(),
        Err(e) if e.0 == "unsupported portable number version" => "version".into(),
        Err(e) if e.0.starts_with("unknown portable number family") => "family".into(),
        Err(_) => "invalid".into(),
    }
}

fn host(row: &[&str; 6]) -> String {
    let b = &mut wide();
    let [kind, a, m, c, _, _] = *row;
    let mode = || RoundMode::parse(m).unwrap();
    let r: Result<String, MathError> = match kind {
        "round" => convert::round_rational(&q(a), mode(), b).map(|i| i.to_string()),
        "dyadic" => convert::to_dyadic(&q(a), c.parse().unwrap(), mode(), b).map(|v| v.to_string()),
        "decimal" => convert::to_decimal(&q(a), c.parse().unwrap(), mode(), b),
        "decimal_exact" => convert::to_decimal_exact(&q(a), b),
        "integer_exact" => convert::to_integer_exact(&q(a), b).map(|i| i.to_string()),
        "parse_decimal" => convert::parse_decimal(text(a), b).map(|v| v.to_string()),
        "creal_round" => CReal::parse(a, b)
            .and_then(|x| convert::creal_round(&x, mode(), c.parse().unwrap(), b))
            .map(|i| i.to_string()),
        "creal_decimal" => CReal::parse(a, b)
            .and_then(|x| convert::creal_decimal(&x, c.parse().unwrap(), mode(), 64, b)),
        "portable" => return portable_category(portable::parse(text(a), b)),
        other => panic!("{other}"),
    };
    r.unwrap_or_else(|e| e.0)
}

#[test]
fn host_conversions_and_portable_texts_match_the_oracle() {
    let mut counts = std::collections::BTreeMap::new();
    for row in rows() {
        let got = host(&row);
        if row[4] == "UNKNOWN" {
            assert!(
                got.starts_with("UnknownWithinBudget: rounding undecided"),
                "{row:?}: {got}"
            );
        } else {
            assert_eq!(got, expected(row[4]), "{row:?}");
        }
        *counts.entry(row[0]).or_insert(0) += 1;
    }
    assert_eq!(rows().len(), 1876);
    assert_eq!(counts["portable"], 28);
}

#[test]
fn host_round_trips_ranges_and_schedule() {
    let b = &mut wide();
    // portable round trip for every family
    for t in [
        "dlm1:int:-42",
        "dlm1:rat:-3/4",
        "dlm1:ivq:[-1/2,3/4]",
        "dlm1:cq:1/2;-3/1",
        "dlm1:creal:pi ; sin 0 ; exp 1",
        "dlm1:ccomplex:c 0/1 | c 2/1 ; sqrt 0",
    ] {
        let v = portable::parse(t, b).unwrap();
        assert_eq!(portable::format(&v), t);
        assert_eq!(portable::parse_as(t, v.family(), b).unwrap(), v);
    }
    assert_eq!(
        portable::parse_as("dlm1:int:1", "rat", b).unwrap_err().0,
        "portable number is `int`, expected `rat`"
    );
    // decimal text round trip through the exact decimal
    for t in ["0", "-0.125", "12.5", "100", "-3.0001"] {
        let v = convert::parse_decimal(t, b).unwrap();
        assert_eq!(convert::to_decimal_exact(&v, b).unwrap(), t);
    }
    // machine ranges
    let big = |s: &str| Integer::parse(s, &mut wide()).unwrap();
    assert_eq!(
        convert::to_u128(&big("340282366920938463463374607431768211455")).unwrap(),
        u128::MAX
    );
    assert_eq!(
        convert::to_u128(&big("340282366920938463463374607431768211456"))
            .unwrap_err()
            .0,
        "integer does not fit in Nat"
    );
    assert_eq!(
        convert::to_u128(&big("-1")).unwrap_err().0,
        "integer does not fit in Nat"
    );
    assert_eq!(
        convert::to_i128(&big("-170141183460469231731687303715884105728")).unwrap(),
        i128::MIN
    );
    assert_eq!(
        convert::to_i128(&big("170141183460469231731687303715884105728"))
            .unwrap_err()
            .0,
        "integer does not fit in Int"
    );
    assert_eq!(convert::schedule(64), vec![0, 8, 16, 32, 64]);
    assert_eq!(convert::schedule(0), vec![0]);
    assert_eq!(convert::schedule(20), vec![0, 8, 16, 20]);
    // rounding is decided only by the bounds: sqrt(2)^2 has no decided floor
    let x = CReal::parse("c 2 ; sqrt 0 ; mul 1 1", b).unwrap();
    assert!(convert::creal_round(&x, RoundMode::Floor, 4096, b)
        .unwrap_err()
        .0
        .contains("undecided at precision 4096"));
    assert_eq!(
        convert::creal_round(&x, RoundMode::HalfEven, 0, b)
            .unwrap()
            .to_string(),
        "2"
    );
    assert!(convert::creal_round(&x, RoundMode::Floor, 4097, b).is_err());
    // decimal digit and exponent limits
    assert_eq!(
        convert::to_decimal(&q("1/3"), 4097, RoundMode::Floor, b)
            .unwrap_err()
            .0,
        "decimal digit count exceeds 4096"
    );
    assert_eq!(
        convert::to_decimal(&q("2/3"), 5, RoundMode::HalfEven, b).unwrap(),
        "0.66667"
    );
    assert_eq!(
        convert::to_decimal(&q("-1/1000"), 2, RoundMode::Trunc, b).unwrap(),
        "0.00"
    );
    assert_eq!(Integer::from_parts(true, Natural::zero()).to_string(), "0");
}

// ---------- ordinary DLM std.convert, interpreter and native C ----------

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn mode_code(m: &str) -> usize {
    ["floor", "ceil", "trunc", "half_even"]
        .iter()
        .position(|x| *x == m)
        .unwrap()
}

fn esc(t: &str) -> String {
    t.replace('\\', "\\\\").replace('"', "\\\"")
}

#[test]
fn source_matches_oracle_and_native_c() {
    let mut source = String::from(
        r#"module test.convert
import std.convert
theory App extends StdConvert {
 let b = ExactBudget(1024, 1000000000000000)
 fn q(t: Text) -> BigRational = match bigrat_parse(b, t) { err(e) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }
 fn x(t: Text) -> CReal = match creal_parse(b, t) { err(e) => CReal(empty_list(CRealNode)), ok(v) => field(v, value) }
 fn it(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn qt(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn tt(r: Result<BigTextCalc, Text>) -> Text = match r { err(e) => e, ok(v) => field(v, value) }
"#,
    );
    let mut expected_lines = Vec::new();
    for row in rows().iter().filter(|r| r[5] == "1") {
        let [kind, a, m, c, v, _] = *row;
        let call = match kind {
            "round" => format!("it(cv_round(b, q(\"{a}\"), {}))", mode_code(m)),
            "dyadic" => format!("qt(cv_dyadic(b, q(\"{a}\"), {c}, {}))", mode_code(m)),
            "decimal" => format!("tt(cv_decimal(b, q(\"{a}\"), {c}, {}))", mode_code(m)),
            "decimal_exact" => format!("tt(cv_decimal_exact(b, q(\"{a}\")))"),
            "integer_exact" => format!("it(cv_integer_exact(b, q(\"{a}\")))"),
            "parse_decimal" => format!("qt(cv_parse_decimal(b, \"{}\"))", esc(text(a))),
            "creal_round" => format!("it(cv_creal_round(b, x(\"{a}\"), {}, {c}))", mode_code(m)),
            "creal_decimal" => format!(
                "tt(cv_creal_decimal(b, x(\"{a}\"), {c}, {}, 64))",
                mode_code(m)
            ),
            "portable" => format!("pn_category(b, \"{}\")", esc(text(a))),
            other => panic!("{other}"),
        };
        source += &format!(" print_line({call})\n");
        expected_lines.push(expected(v).to_string());
    }
    source += "}\n";
    assert_eq!(expected_lines.len(), 234);
    let dir = std::env::temp_dir().join(format!("dlm-convert-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, &source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
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
    let native: Vec<String> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    for (i, (got, want)) in native.iter().zip(&expected_lines).enumerate() {
        if want == "UNKNOWN" {
            assert!(
                got.starts_with("UnknownWithinBudget: rounding undecided"),
                "#{i}: {got}"
            );
        } else {
            assert_eq!(got, want, "#{i}");
        }
    }
    assert_eq!(native.len(), expected_lines.len());
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn source_machine_integers_portable_formatting_and_limits() {
    let source = r#"module test.convert_raw
import std.convert
theory App extends StdConvert {
 let b = ExactBudget(1024, 1000000000000000)
 fn n(t: Text) -> BigInt = match big_parse(b, t) { err(e) => BigInt(false, empty_list(Nat)), ok(v) => field(v, value) }
 fn q(t: Text) -> BigRational = match bigrat_parse(b, t) { err(e) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }
 fn tt(r: Result<BigTextCalc, Text>) -> Text = match r { err(e) => e, ok(v) => field(v, value) }
 fn nt(r: Result<Nat, Text>) -> Text = match r { err(e) => e, ok(v) => nat_to_text(v) }
 fn zt(r: Result<Int, Text>) -> Text = match r { err(e) => e, ok(v) => int_to_text(v) }
 print_line(nt(cv_to_nat(b, n("340282366920938463463374607431768211455"))))
 print_line(nt(cv_to_nat(b, n("340282366920938463463374607431768211456"))))
 print_line(nt(cv_to_nat(b, n("-1"))))
 print_line(nt(cv_to_nat(b, n("0"))))
 print_line(zt(cv_to_int(b, n("170141183460469231731687303715884105727"))))
 print_line(zt(cv_to_int(b, n("170141183460469231731687303715884105728"))))
 print_line(zt(cv_to_int(b, n("-170141183460469231731687303715884105727"))))
 print_line(zt(cv_to_int(b, n("-170141183460469231731687303715884105728"))))
 print_line(zt(cv_to_int(b, n("-170141183460469231731687303715884105729"))))
 print_line(zt(cv_to_int(b, n("-12345"))))
 print_line(tt(pn_int(b, n("-42"))))
 print_line(tt(pn_rat(b, q("6/8"))))
 print_line(tt(pn_cq(b, q("1/2"), q("-3"))))
 print_line(tt(pn_check(b, "dlm1:rat:-3/4")))
 print_line(tt(pn_check(b, "dlm7:rat:1")))
 print_line(tt(pn_check(b, "dlm1:float:1")))
 print_line(tt(cv_decimal(b, q("1/3"), 4097, 0)))
 print_line(tt(cv_decimal(b, q("2/3"), 5, 3)))
 print_line(tt(cv_decimal(b, q("-1/1000"), 2, 2)))
 print_line(tt(cv_decimal_exact(b, q("1/3"))))
 print_line(tt(cv_creal_decimal(b, CReal(empty_list(CRealNode)), 2, 0, 4097)))
 print_line(tt(cv_decimal(b, q("1/3"), 2, 4)))
}
"#;
    let expected = [
        "340282366920938463463374607431768211455",
        "integer does not fit in Nat",
        "integer does not fit in Nat",
        "0",
        "170141183460469231731687303715884105727",
        "integer does not fit in Int",
        "-170141183460469231731687303715884105727",
        "-170141183460469231731687303715884105728",
        "integer does not fit in Int",
        "-12345",
        "dlm1:int:-42",
        "dlm1:rat:3/4",
        "dlm1:cq:1/2;-3/1",
        "dlm1:rat:-3/4",
        "unsupported portable number version",
        "unknown portable number family `float`",
        "decimal digit count exceeds 4096",
        "0.66667",
        "0.00",
        "rational is not a finite decimal",
        "computable real precision exceeds 4096 bits",
        "unknown rounding mode",
    ];
    let dir = std::env::temp_dir().join(format!("dlm-convert-raw-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
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
    let native: Vec<String> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    assert_eq!(native, expected);
    let b = &mut ExactBudget::new(1024, 1_000_000_000_000_000).unwrap();
    assert_eq!(
        portable::format(&portable::parse("dlm1:cq:1/2;-3/1", b).unwrap()),
        "dlm1:cq:1/2;-3/1"
    );
    std::fs::remove_dir_all(dir).unwrap();
}
