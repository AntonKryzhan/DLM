//! v0.103 — complex numbers with computable real components (numeric criterion
//! N09): host `math::ccomplex`, ordinary DLM `std.ccomplex`, interpreter and
//! native C, against independent Fraction/Decimal fixtures
//! (`tools/generate_ccomplex103_oracle.py`).
use dlm_core::math::ccomplex::CComplex;
use dlm_core::math::creal::CReal;
use dlm_core::math::exact::ExactBudget;
use dlm_core::math::MathError;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/ccomplex103.tsv");

struct Row<'a> {
    kind: &'a str,
    bits: Option<usize>,
    n: u32,
    x: &'a str,
    y: &'a str,
    program: &'a str,
    value: &'a str,
    source: u8,
}

fn rows() -> Vec<Row<'static>> {
    FIXTURES
        .lines()
        .map(|line| {
            let f: Vec<&str> = line.split('\t').collect();
            assert_eq!(f.len(), 8, "{line}");
            Row {
                kind: f[0],
                bits: f[1].parse().ok(),
                n: f[2].parse().unwrap(),
                x: f[3],
                y: f[4],
                program: f[5],
                value: f[6],
                source: f[7].parse().unwrap(),
            }
        })
        .collect()
}

fn budget(bits: usize) -> ExactBudget {
    ExactBudget::new(bits, u64::MAX / 4).unwrap()
}

fn wide() -> ExactBudget {
    budget(16384)
}

fn complex(text: &str) -> CComplex {
    CComplex::parse(text, &mut wide()).unwrap_or_else(|e| panic!("{text}: {e}"))
}

enum Built {
    Complex(CComplex),
    Real(CReal),
}

fn build(row: &Row) -> Built {
    let b = &mut wide();
    let z = complex(row.x);
    let w = || complex(row.y);
    match row.kind {
        "add" => Built::Complex(z.checked_add(&w(), b).unwrap()),
        "sub" => Built::Complex(z.checked_sub(&w(), b).unwrap()),
        "mul" => Built::Complex(z.checked_mul(&w(), b).unwrap()),
        "div" => Built::Complex(z.checked_div(&w(), b).unwrap()),
        "neg" => Built::Complex(z.checked_neg(b).unwrap()),
        "conj" => Built::Complex(z.conj(b).unwrap()),
        "inv" => Built::Complex(z.checked_inv(b).unwrap()),
        "abs" => Built::Real(z.abs(b).unwrap()),
        "abs_sq" => Built::Real(z.abs_sq(b).unwrap()),
        other => panic!("{other}"),
    }
}

fn evaluate(row: &Row, b: &mut ExactBudget) -> String {
    let result = (|| -> Result<String, MathError> {
        if row.kind == "equal" {
            return Ok(match complex(row.x).equals(&complex(row.y), row.n, b)? {
                Some(false) => "1",
                Some(true) => "0",
                None => "2",
            }
            .to_string());
        }
        Ok(match build(row) {
            Built::Complex(z) => {
                let (re, im) = z.approx(row.n, b)?;
                format!("{re} | {im}")
            }
            Built::Real(x) => x.approx(row.n, b)?.to_string(),
        })
    })();
    result.unwrap_or_else(|e| e.0)
}

fn expected(value: &str) -> &str {
    match value {
        "ERR:zero" => "computable real division by zero",
        "ERR:negative" => "computable real square root of a negative value",
        v => v,
    }
}

#[test]
fn host_constructions_and_approximations_match_the_oracle_with_precision_boundaries() {
    let (mut total, mut boundaries, mut unknown) = (0, 0, 0);
    for row in rows() {
        total += 1;
        if row.kind != "equal" {
            let text = match build(&row) {
                Built::Complex(z) => z.to_string(),
                Built::Real(x) => x.to_string(),
            };
            assert_eq!(text, row.program, "{} {} {}", row.kind, row.x, row.y);
        }
        match row.bits {
            None => {
                let got = evaluate(&row, &mut wide());
                assert!(
                    got.starts_with("UnknownWithinBudget"),
                    "{} {}: {got}",
                    row.kind,
                    row.x
                );
                unknown += 1;
            }
            Some(bits) => {
                assert_eq!(
                    evaluate(&row, &mut budget(bits)),
                    expected(row.value),
                    "{} {} {} n={}",
                    row.kind,
                    row.x,
                    row.y,
                    row.n
                );
                if bits > 1 {
                    let narrow = evaluate(&row, &mut budget(bits - 1));
                    assert!(
                        narrow.starts_with("UnknownWithinBudget") && narrow.contains("precision"),
                        "{} {}: {narrow}",
                        row.kind,
                        row.x
                    );
                    boundaries += 1;
                }
            }
        }
    }
    assert_eq!((total, boundaries, unknown), (497, 495, 2));
}

#[test]
fn host_identities_text_and_limits() {
    let b = &mut wide();
    let i = complex("c 0 | c 1");
    let minus_one = complex("c -1 | c 0");
    let ii = i.checked_mul(&i, b).unwrap();
    assert_eq!(ii.equals(&minus_one, 0, b).unwrap(), Some(true));
    // z · (1/z) = 1 within the bounds, z / z likewise, |z|² = z · conj(z)
    let z = complex("c 2 ; sqrt 0 | c 3 ; sqrt 0 ; neg 1");
    let one = complex("c 1 | c 0");
    for n in [0, 10, 40] {
        let p = z.checked_mul(&z.checked_inv(b).unwrap(), b).unwrap();
        assert_ne!(p.equals(&one, n, b).unwrap(), Some(false));
        let q = z.checked_div(&z, b).unwrap();
        assert_ne!(q.equals(&one, n, b).unwrap(), Some(false));
        let (re, im) = z
            .checked_mul(&z.conj(b).unwrap(), b)
            .unwrap()
            .approx(n, b)
            .unwrap();
        let m = z.abs_sq(b).unwrap().approx(n, b).unwrap();
        assert!(re.intersect(&m, b).unwrap().is_some());
        assert!(im.contains_zero(b).unwrap());
    }
    // |3 − 4i| = 5 exactly; |z|² of an exact point is a point
    let five = complex("c 3 | c -4").abs(b).unwrap().approx(8, b).unwrap();
    assert!(five.is_point() && five.lo().to_string() == "5/1");
    // division by zero is a proved domain error; by an undecided zero, unknown
    let zero = complex("c 0 | c 0");
    assert_eq!(
        one.checked_div(&zero, b)
            .unwrap()
            .approx(8, b)
            .unwrap_err()
            .0,
        "computable real division by zero"
    );
    let e = complex("c 2 ; sqrt 0 ; sub 1 1 | c 0")
        .checked_inv(b)
        .unwrap()
        .approx(4, b)
        .unwrap_err()
        .0;
    assert!(e.starts_with("UnknownWithinBudget"), "{e}");
    // text
    assert_eq!(
        complex("c 1/2 | c -2 ; sqrt 0").to_string(),
        "c 1/2 | c -2/1 ; sqrt 0"
    );
    for bad in [
        "c 1",
        "c 1 | c 2 | c 3",
        "c 1 |c 2",
        "c 1| c 2",
        "| c 1",
        "c 1 | ",
    ] {
        assert!(CComplex::parse(bad, b).is_err(), "{bad}");
    }
    // composed programs keep the 64-node limit
    let mut big = complex("c 2 ; sqrt 0 ; sqrt 1 ; sqrt 2 | c 3 ; sqrt 0 ; sqrt 1");
    let mut err = None;
    for _ in 0..8 {
        match big.checked_mul(&big, b) {
            Ok(next) => big = next,
            Err(e) => {
                err = Some(e.0);
                break;
            }
        }
    }
    assert_eq!(
        err.as_deref(),
        Some("computable real program must have 1..64 nodes")
    );
    let r = CComplex::from_real(CReal::parse("c 7", b).unwrap(), b).unwrap();
    assert_eq!(r.to_string(), "c 7/1 | c 0/1");
}

// ---------- ordinary DLM std.ccomplex, interpreter and native C ----------

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn header() -> String {
    r#"module test.ccomplex
import std.ccomplex
theory App extends StdCComplex {
 fn show(r: Result<IntervalQCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn cshow(r: Result<CComplexApprox, Text>) -> Text = match r { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, re)) { err(e) => e, ok(a) => match ivq_text(field(a, budget), field(v, im)) { err(e) => e, ok(c) => concat(field(a, value), concat(" | ", field(c, value))) } } }
 fn text(r: Result<CComplexCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match cc_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn rtext(r: Result<CRealCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match creal_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn op1(b: ExactBudget, k: Text, x: CComplex) -> Result<CComplexCalc, Text> = if_then_else(text_eq(k, "neg"), cc_neg(b, x), if_then_else(text_eq(k, "conj"), cc_conj(b, x), cc_inv(b, x)))
 fn op2(b: ExactBudget, k: Text, x: CComplex, y: CComplex) -> Result<CComplexCalc, Text> =
     if_then_else(text_eq(k, "add"), cc_add(b, x, y), if_then_else(text_eq(k, "sub"), cc_sub(b, x, y), if_then_else(text_eq(k, "mul"), cc_mul(b, x, y), cc_div(b, x, y))))
 fn built(b: ExactBudget, k: Text, s: Text, t: Text) -> Result<CComplexCalc, Text> = match cc_parse(b, s) { err(e) => err(e, CComplexCalc), ok(x) =>
     if_then_else(text_eq(t, "-"), op1(field(x, budget), k, field(x, value)), match cc_parse(field(x, budget), t) { err(e) => err(e, CComplexCalc), ok(y) => op2(field(y, budget), k, field(x, value), field(y, value)) }) }
 fn real(b: ExactBudget, k: Text, s: Text) -> Result<CRealCalc, Text> = match cc_parse(b, s) { err(e) => err(e, CRealCalc), ok(x) => if_then_else(text_eq(k, "abs"), cc_abs(field(x, budget), field(x, value)), cc_abs_sq(field(x, budget), field(x, value))) }
 fn approx(b: ExactBudget, k: Text, s: Text, t: Text, n: Nat) -> Text = match built(b, k, s, t) { err(e) => e, ok(z) => cshow(cc_approx(field(z, budget), field(z, value), n)) }
 fn rapprox(b: ExactBudget, k: Text, s: Text, n: Nat) -> Text = match real(b, k, s) { err(e) => e, ok(x) => show(creal_approx(field(x, budget), field(x, value), n)) }
 fn compare_complex(b: ExactBudget, s: Text, t: Text, n: Nat) -> Text = match cc_parse(b, s) { err(e) => e, ok(x) => match cc_parse(field(x, budget), t) { err(e) => e, ok(y) => match cc_equal(field(y, budget), field(x, value), field(y, value), n) { err(e) => e, ok(o) => int_to_text(field(o, order)) } } }
"#
    .into()
}

fn source_budget(bits: usize) -> String {
    let work = if bits > 1024 {
        "18446744073709551615"
    } else {
        "1000000000000000"
    };
    format!("ExactBudget({bits}, {work})")
}

fn call(row: &Row, budget: &str) -> String {
    match row.kind {
        "equal" => format!("compare_complex({budget}, \"{}\", \"{}\", {})", row.x, row.y, row.n),
        "abs" | "abs_sq" => format!(
            "rapprox({budget}, \"{}\", \"{}\", {})",
            row.kind, row.x, row.n
        ),
        k => format!(
            "approx({budget}, \"{k}\", \"{}\", \"{}\", {})",
            row.x, row.y, row.n
        ),
    }
}

fn run_both(source: &str, expected: &[String], name: &str) {
    let dir = std::env::temp_dir().join(format!("dlm-ccomplex-{}-{name}", std::process::id()));
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
    assert_eq!(interpreted, native, "{name}: interpreter vs native C");
    assert_eq!(native.len(), expected.len());
    for (i, (got, want)) in native.iter().zip(expected).enumerate() {
        if want == "UNKNOWN" {
            assert!(got.starts_with("UnknownWithinBudget"), "{name} #{i}: {got}");
        } else {
            assert_eq!(got, want, "{name} #{i}");
        }
    }
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn source_matches_oracle_and_native_c_with_precision_boundaries() {
    let mut source = header();
    let mut expected_lines = Vec::new();
    let (mut vectors, mut boundaries) = (0, 0);
    for row in rows().iter().filter(|r| r.source > 0) {
        let bits = row.bits.unwrap_or(256);
        source += &format!(" print_line({})\n", call(row, &source_budget(bits)));
        expected_lines.push(expected(row.value).to_string());
        vectors += 1;
        if row.source == 2 && row.bits.is_some_and(|b| b > 1) {
            source += &format!(" print_line({})\n", call(row, &source_budget(bits - 1)));
            expected_lines.push("UNKNOWN".into());
            boundaries += 1;
        }
        // the source construction yields the host program text
        if row.kind != "equal" {
            let call = if row.kind.starts_with("abs") {
                format!(
                    "rtext(real({}, \"{}\", \"{}\"))",
                    source_budget(16384),
                    row.kind,
                    row.x
                )
            } else {
                format!(
                    "text(built({}, \"{}\", \"{}\", \"{}\"))",
                    source_budget(16384),
                    row.kind,
                    row.x,
                    row.y
                )
            };
            source += &format!(" print_line({call})\n");
            expected_lines.push(row.program.to_string());
        }
    }
    source += "}\n";
    assert_eq!((vectors, boundaries), (74, 56));
    run_both(&source, &expected_lines, "oracle");
}

#[test]
fn source_raw_records_text_and_privacy() {
    let mut source = header();
    source += r#"
 let b = ExactBudget(64, 1000000000000000)
 let zero = BigRational(false, empty_list(Nat), list(1))
 print_line(text(cc_parse(b, "c 1/2 | c -2 ; sqrt 0")))
 print_line(text(cc_parse(b, "c 1")))
 print_line(text(cc_parse(b, "c 1 | c 2 | c 3")))
 print_line(text(cc_parse(b, "c 1  | c 2")))
 print_line(text(cc_parse(b, "c 1 | c 2 ; sqrt 5")))
 print_line(text(cc_check(b, CComplex(CReal(empty_list(CRealNode)), CReal(list(CRealNode(0, 0, 0, zero)))))))
 print_line(text(cc_from_real(b, CReal(list(CRealNode(0, 0, 0, BigRational(false, list(7), list(1))))))))
 print_line(compare_complex(b, "c 0 | c 1", "c 0 | c 1", 4))
}
"#;
    let expected: Vec<String> = [
        "c 1/2 | c -2/1 ; sqrt 0",
        "invalid computable complex text",
        "invalid computable complex text",
        "invalid computable complex text",
        "computable real operand must refer to an earlier node",
        "computable real program must have 1..64 nodes",
        "c 7/1 | c 0/1",
        "0",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    run_both(&source, &expected, "raw");
    let private = header() + " fn probe() -> Nat = cc_arity(3)\n}\n";
    let dir = std::env::temp_dir().join(format!("dlm-ccomplex-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, private).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(!report.ok());
    std::fs::remove_dir_all(dir).unwrap();
}
