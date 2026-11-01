//! v0.104 — elementary functions of computable reals (numeric criterion N10):
//! exp, log, sin, cos and pi with guaranteed enclosures; host and ordinary DLM
//! `std.creal` (interpreter and native C) against the reference algorithm and
//! mpmath (`tools/generate_elementary104_oracle.py`).
use dlm_core::math::creal::CReal;
use dlm_core::math::elementary;
use dlm_core::math::exact::{ExactBudget, Rational};
use dlm_core::math::interval_q::IntervalQ;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::cmp::Ordering;
use std::path::{Path, PathBuf};
use std::process::Command;

const FIXTURES: &str = include_str!("fixtures/elementary104.tsv");

fn rows() -> Vec<(u32, &'static str, &'static str, bool)> {
    FIXTURES
        .lines()
        .map(|l| {
            let f: Vec<&str> = l.split('\t').collect();
            assert_eq!(f.len(), 4, "{l}");
            (f[0].parse().unwrap(), f[1], f[2], f[3] == "1")
        })
        .collect()
}

fn budget(bits: usize) -> ExactBudget {
    ExactBudget::new(bits, u64::MAX / 4).unwrap()
}

fn expected(value: &str) -> &str {
    match value {
        "ERR:log" => "computable real logarithm of a non-positive value",
        "ERR:zero" => "computable real division by zero",
        "ERR:negative" => "computable real square root of a negative value",
        v => v,
    }
}

fn host(text: &str, n: u32, b: &mut ExactBudget) -> String {
    CReal::parse(text, b)
        .and_then(|x| x.approx(n, b))
        .map(|v| v.to_string())
        .unwrap_or_else(|e| e.0)
}

fn q(t: &str) -> Rational {
    Rational::parse(t, &mut budget(4096)).unwrap()
}

#[test]
fn host_matches_reference_algorithm_and_mpmath_fixtures() {
    let (mut total, mut unknown, mut domain) = (0, 0, 0);
    for (n, text, value, _) in rows() {
        let got = host(text, n, &mut budget(4096));
        if value == "UNKNOWN" {
            assert!(got.starts_with("UnknownWithinBudget"), "{text}: {got}");
            unknown += 1;
        } else {
            assert_eq!(got, expected(value), "{text} n={n}");
        }
        domain += usize::from(value.starts_with("ERR:"));
        total += 1;
    }
    assert_eq!((total, unknown, domain), (263, 1, 42));
}

#[test]
fn host_enclosures_identities_and_limits() {
    let b = &mut budget(4096);
    let contains = |x: &IntervalQ, t: &str, b: &mut ExactBudget| x.contains(&q(t), b).unwrap();
    // pi and e: the 100-bit enclosures lie strictly between 29-digit bounds
    let between = |x: &IntervalQ, lo: &str, hi: &str, b: &mut ExactBudget| {
        x.lo().checked_cmp(&q(lo), b).unwrap() == Ordering::Greater
            && x.hi().checked_cmp(&q(hi), b).unwrap() == Ordering::Less
    };
    let p = CReal::parse("pi", b).unwrap().approx(100, b).unwrap();
    assert!(between(
        &p,
        "314159265358979323846264338327/100000000000000000000000000000",
        "314159265358979323846264338328/100000000000000000000000000000",
        b
    ));
    let e = CReal::parse("c 1 ; exp 0", b)
        .unwrap()
        .approx(100, b)
        .unwrap();
    assert!(between(
        &e,
        "271828182845904523536028747135/100000000000000000000000000000",
        "271828182845904523536028747136/100000000000000000000000000000",
        b
    ));
    // identities hold within the bounds: exp(log 2) = 2, sin² + cos² = 1, cos(pi) = -1
    for (text, value) in [
        ("c 2 ; log 0 ; exp 1", "2"),
        (
            "c 7/3 ; sin 0 ; square 1 ; c 7/3 ; cos 3 ; square 4 ; add 2 5",
            "1",
        ),
        ("pi ; cos 0", "-1"),
        ("pi ; c 6 ; div 0 1 ; sin 2", "1/2"),
        ("c 1 ; log 0", "0"),
        ("c 1000 ; exp 0 ; log 1", "1000"),
    ] {
        for n in [0, 20, 60] {
            let v = CReal::parse(text, b).unwrap().approx(n, b).unwrap();
            assert!(contains(&v, value, b), "{text} n={n}: {v}");
        }
    }
    // monotone functions: endpoints of interval arguments
    let x = IntervalQ::new(q("1/3"), q("1/2"), b).unwrap();
    let ex = elementary::exp(&x, 40, b).unwrap();
    // exp(1/3) = 1.3956124..., exp(1/2) = 1.6487212...
    assert!(between(
        &IntervalQ::point(ex.lo().clone(), b).unwrap(),
        "1395612/1000000",
        "1395613/1000000",
        b
    ));
    assert!(between(
        &IntervalQ::point(ex.hi().clone(), b).unwrap(),
        "1648721/1000000",
        "1648722/1000000",
        b
    ));
    let s = elementary::trig(&IntervalQ::new(q("-10"), q("10"), b).unwrap(), 20, false, b).unwrap();
    assert_eq!(
        (s.lo().to_string(), s.hi().to_string()),
        ("-1/1".to_string(), "1/1".to_string())
    );
    // domains: proved non-positive log is an error, an undecided sign is unknown
    let err = |t: &str| {
        CReal::parse(t, &mut budget(4096))
            .unwrap()
            .approx(8, &mut budget(4096))
            .unwrap_err()
            .0
    };
    assert_eq!(
        err("c 0 ; log 0"),
        "computable real logarithm of a non-positive value"
    );
    assert_eq!(
        err("c -1/2 ; log 0"),
        "computable real logarithm of a non-positive value"
    );
    assert!(err("c 2 ; sqrt 0 ; c 2 ; sqrt 2 ; sub 1 3 ; log 4").starts_with("UnknownWithinBudget"));
    // precision bounds the magnitude: exp(20000) needs more than 4096 bits
    let e = err("c 20000 ; exp 0");
    assert!(
        e.starts_with("UnknownWithinBudget") && e.contains("precision"),
        "{e}"
    );
    // grammar: pi takes no operands; elementary nodes compose with complex numbers
    assert_eq!(
        CReal::parse("pi ; sin 0 ; exp 1", b).unwrap().to_string(),
        "pi ; sin 0 ; exp 1"
    );
    assert!(
        CReal::parse("pi 0", b).is_err()
            && CReal::parse("exp", b).is_err()
            && CReal::parse("c 1 ; log 0 0", b).is_err()
    );
    let z = dlm_core::math::ccomplex::CComplex::parse("pi ; cos 0 | pi ; sin 0", b).unwrap();
    let (re, im) = z.approx(30, b).unwrap();
    assert!(contains(&re, "-1", b) && contains(&im, "0", b));
}

// ---------- ordinary DLM std.creal, interpreter and native C ----------

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

#[test]
fn source_matches_host_and_native_c() {
    let mut source = String::from(
        r#"module test.elementary
import std.creal
theory App extends StdCReal {
 fn show(r: Result<IntervalQCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn approx(b: ExactBudget, t: Text, n: Nat) -> Text = match creal_parse(b, t) { err(e) => e, ok(x) => show(creal_approx(field(x, budget), field(x, value), n)) }
"#,
    );
    let mut expected_lines = Vec::new();
    for (n, text, value, src) in rows() {
        if !src {
            continue;
        }
        source +=
            &format!(" print_line(approx(ExactBudget(1024, 1000000000000000), \"{text}\", {n}))\n");
        expected_lines.push(expected(value).to_string());
    }
    source +=
        " print_line(approx(ExactBudget(1024, 1000000000000000), \"c 3000 ; exp 0\", 4))\n}\n";
    expected_lines.push("UNKNOWN".into());
    assert_eq!(expected_lines.len(), 33);
    let dir = std::env::temp_dir().join(format!("dlm-elementary-{}", std::process::id()));
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
            assert!(got.starts_with("UnknownWithinBudget"), "#{i}: {got}");
        } else {
            assert_eq!(got, want, "#{i}");
        }
    }
    std::fs::remove_dir_all(dir).unwrap();
}
