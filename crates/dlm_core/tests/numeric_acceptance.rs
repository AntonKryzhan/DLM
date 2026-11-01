//! v0.108 — acceptance of the numeric section (N16): the evidence of every
//! criterion exists and every fixture has an independent generator; one
//! cross-family regression over all numeric source modules agrees with the
//! host (values, or error categories where host and source word refusals
//! differently) in the interpreter and native C; native memory is checked by
//! the dense trace and, when the toolchain has them, ASan/UBSan/LSan; the same
//! program is cross-built with MinGW-w64 and run under Wine when available.
use dlm_core::math::acceptance::{CRITERIA, REQUIRE_SANITIZERS, REQUIRE_WINDOWS};
use dlm_core::math::ccomplex::CComplex;
use dlm_core::math::complex_q::ComplexQ;
use dlm_core::math::contract::classify;
use dlm_core::math::convert::{self, RoundMode};
use dlm_core::math::creal::CReal;
use dlm_core::math::exact::{ExactBudget, Integer, Natural, Rational};
use dlm_core::math::interval_q::IntervalQ;
use dlm_core::math::portable;
use dlm_core::math::scalar_witness as sw;
use dlm_core::math::MathResult;
use dlm_core::{
    build_c_executable_with_flags, default_c_compiler, emit_c, load_program_with_paths,
    lower_program, CheckPolicy, Checker, Program, Runtime,
};
use std::fmt::Display;
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

#[test]
fn every_criterion_has_existing_consistent_evidence() {
    let ids: Vec<_> = CRITERIA.iter().map(|c| c.id).collect();
    assert_eq!(
        ids,
        (1..=16).map(|i| format!("N{i:02}")).collect::<Vec<_>>()
    );
    let tools: Vec<String> = std::fs::read_dir(repo().join("tools"))
        .unwrap()
        .filter_map(|e| e.ok())
        .filter(|e| e.path().extension().is_some_and(|x| x == "py"))
        .map(|e| std::fs::read_to_string(e.path()).unwrap())
        .collect();
    let tests_dir = repo().join("crates/dlm_core/tests");
    let test_texts: Vec<String> = std::fs::read_dir(&tests_dir)
        .unwrap()
        .filter_map(|e| e.ok())
        .filter(|e| e.path().extension().is_some_and(|x| x == "rs"))
        .map(|e| std::fs::read_to_string(e.path()).unwrap())
        .collect();
    for c in CRITERIA {
        assert!(!c.title.is_empty() && !c.tests.is_empty(), "{}", c.id);
        for t in c.tests {
            assert!(tests_dir.join(t).is_file(), "{}: test {t}", c.id);
        }
        for f in c.fixtures {
            let path = tests_dir.join("fixtures").join(f);
            assert!(
                std::fs::metadata(&path)
                    .map(|m| m.len() > 0)
                    .unwrap_or(false),
                "{}: fixture {f}",
                c.id
            );
            assert!(
                tools.iter().any(|t| t.contains(f)),
                "{}: no generator writes {f}",
                c.id
            );
            assert!(
                test_texts.iter().any(|t| t.contains(f)),
                "{}: no test reads {f}",
                c.id
            );
        }
        for m in c.modules {
            assert!(
                repo().join(format!("lib/std/{m}.dlm")).is_file(),
                "{}: module {m}",
                c.id
            );
        }
        for d in c.docs {
            assert!(repo().join("docs").join(d).is_file(), "{}: doc {d}", c.id);
        }
    }
    // every committed numeric fixture belongs to a criterion (matrices belong to the algebra section)
    for entry in std::fs::read_dir(tests_dir.join("fixtures")).unwrap() {
        let name = entry.unwrap().file_name().into_string().unwrap();
        if name.starts_with("rational_matrices") {
            continue;
        }
        assert!(
            CRITERIA.iter().any(|c| c.fixtures.contains(&name.as_str())),
            "fixture {name} has no criterion"
        );
    }
}

fn host<T: Display>(r: MathResult<T>) -> String {
    match r {
        Ok(v) => format!("ok:{v}"),
        Err(e) => format!(
            "err:{}",
            classify(&e.0).map_or("unclassified", |c| c.name())
        ),
    }
}
fn budget() -> ExactBudget {
    ExactBudget::new(512, 1_000_000_000_000_000).unwrap()
}
fn q(t: &str) -> Rational {
    Rational::parse(t, &mut budget()).unwrap()
}

type HostCase = Box<dyn Fn(&mut ExactBudget) -> String>;

/// `(DLM expression, host computation)`; both print `ok:<value>` or `err:<category>`.
fn cases() -> Vec<(String, HostCase)> {
    let quotient = sw::produce_quotient(&q("22/7"), &q("-3"), &mut budget()).unwrap();
    let tampered = quotient.replacen("22/7", "23/7", 1);
    let mut out: Vec<(String, HostCase)> = vec![
        ("qt(bigrat_add(b, q(\"1/3\"), q(\"-5/7\")))".into(), Box::new(|b| host(q("1/3").checked_add(&q("-5/7"), b)))),
        ("qt(bigrat_mul(b, q(\"-22/7\"), q(\"7/11\")))".into(), Box::new(|b| host(q("-22/7").checked_mul(&q("7/11"), b)))),
        ("qt(bigrat_div(b, q(\"22/7\"), q(\"0\")))".into(), Box::new(|b| host(q("22/7").checked_div(&q("0"), b)))),
        ("qt(bigrat_parse(b, \"1/x\"))".into(), Box::new(|b| host(Rational::parse("1/x", b)))),
        ("it(big_pow(b, z(\"3\"), 400))".into(), Box::new(|b| host(Natural::from_u128(3).checked_pow(400, b)))),
        ("it(big_pow(b, z(\"-3\"), 101))".into(), Box::new(|b| host(Integer::from_i128(-3).checked_pow(101, b)))),
        ("it(big_gcd(b, z(\"123456789012345678901234567890\"), z(\"987654321098765432109876543210\")))".into(),
            Box::new(|b| host(Natural::parse("123456789012345678901234567890", b).unwrap().checked_gcd(&Natural::parse("987654321098765432109876543210", b).unwrap(), b)))),
        ("sq(big_sqrt_rem(b, z(\"-4\")))".into(), Box::new(|b| host(Integer::from_i128(-4).checked_sqrt_rem(b).map(|r| r.0)))),
        ("sq(big_sqrt_rem(b, z(\"1000000000000000000000000000001\")))".into(), Box::new(|b| host(Integer::parse("1000000000000000000000000000001", b).unwrap().checked_sqrt_rem(b).map(|r| r.0)))),
        ("ivt(match ivq_parse(b, \"[2/1,3/1]\") { err(x) => err(x, IntervalQCalc), ok(v) => ivq_sqrt(field(v, budget), field(v, value), 20) })".into(),
            Box::new(|b| host(IntervalQ::parse("[2/1,3/1]", b).and_then(|v| v.checked_sqrt(20, b))))),
        ("ivt(match ivq_parse(b, \"[-1/1,1/1]\") { err(x) => err(x, IntervalQCalc), ok(v) => ivq_inv(field(v, budget), field(v, value)) })".into(),
            Box::new(|b| host(IntervalQ::parse("[-1/1,1/1]", b).and_then(|v| v.checked_inv(b))))),
        ("ivt(ivq_parse(b, \"[1/1,0/1]\"))".into(), Box::new(|b| host(IntervalQ::parse("[1/1,0/1]", b)))),
        ("tt(cv_decimal(b, q(\"22/7\"), 10, 3))".into(), Box::new(|b| host(convert::to_decimal(&q("22/7"), 10, RoundMode::HalfEven, b)))),
        ("it(cv_round(b, q(\"-7/2\"), 3))".into(), Box::new(|b| host(convert::round_rational(&q("-7/2"), RoundMode::HalfEven, b)))),
        ("qt(cv_parse_decimal(b, \"1e5000\"))".into(), Box::new(|b| host(convert::parse_decimal("1e5000", b)))),
        ("it(cv_integer_exact(b, q(\"1/3\")))".into(), Box::new(|b| host(convert::to_integer_exact(&q("1/3"), b)))),
        ("tt(cv_creal_decimal(b, cx(\"pi\"), 8, 0, 64))".into(), Box::new(|b| host(CReal::parse("pi", b).and_then(|x| convert::creal_decimal(&x, 8, RoundMode::Floor, 64, b))))),
        ("it(cv_creal_round(b, cx(\"c 2 ; sqrt 0 ; sub 1 1\"), 0, 32))".into(),
            Box::new(|b| host(CReal::parse("c 2 ; sqrt 0 ; sub 1 1", b).and_then(|x| convert::creal_round(&x, RoundMode::Floor, 32, b))))),
        ("tt(pn_check(b, \"dlm1:rat:-3/4\"))".into(), Box::new(|b| host(portable::parse("dlm1:rat:-3/4", b).map(|v| portable::format(&v))))),
        ("tt(pn_check(b, \"dlm1:rat:6/8\"))".into(), Box::new(|b| host(portable::parse("dlm1:rat:6/8", b).map(|v| portable::format(&v))))),
        (format!("tt(sw_check(b, \"{quotient}\"))"), {
            let w = quotient.clone();
            Box::new(move |b| host(sw::check(&w, b).map(|c| format!("{} {}", c.trust.name(), c.kind))))
        }),
        (format!("tt(sw_check(b, \"{tampered}\"))"), {
            let w = tampered.clone();
            Box::new(move |b| host(sw::check(&w, b).map(|c| format!("{} {}", c.trust.name(), c.kind))))
        }),
        ("ca(\"c 1/1 | c 2/1 ; sqrt 0\", 8)".into(),
            Box::new(|b| host(CComplex::parse("c 1/1 | c 2/1 ; sqrt 0", b).and_then(|z| z.approx(8, b)).map(|(r, i)| format!("{r};{i}"))))),
        ("ca(\"c 1/1 | c 0/1\", 5000)".into(), Box::new(|b| host(CComplex::parse("c 1/1 | c 0/1", b).and_then(|z| z.approx(5000, b)).map(|(r, i)| format!("{r};{i}"))))),
        ("zt(dz_mul(b, dz(\"-123456789123456789\"), dz(\"987654321987654321\")))".into(),
            Box::new(|_| format!("ok:{}", -123456789123456789i128 * 987654321987654321i128))),
        ("dd(dz_div_rem(b, dz(\"-17\"), dz(\"5\")))".into(), Box::new(|_| format!("ok:{} {}", (-17i128).div_euclid(5), (-17i128).rem_euclid(5)))),
        ("dd(dz_div_rem(b, dz(\"17\"), dz(\"0\")))".into(), Box::new(|b| host(q("17").checked_div(&q("0"), b)))),
        ("zt(dz_pow(b, dz(\"3\"), 400))".into(), Box::new(|b| host(Natural::from_u128(3).checked_pow(400, b)))),
        ("zt(dz_parse(b, \"12a\"))".into(), Box::new(|b| host(Natural::parse("12a", b)))),
        ("ap(ExactBudget(512, 10), \"c 2 ; sqrt 0\", 32)".into(),
            Box::new(|_| host(ExactBudget::new(512, 10).and_then(|mut b| CReal::parse("c 2 ; sqrt 0", &mut b).and_then(|x| x.approx(32, &mut b)))))),
        ("ap(b, \"c 2 ; sqrt 0\", 5000)".into(), Box::new(|b| host(CReal::parse("c 2 ; sqrt 0", b).and_then(|x| x.approx(5000, b))))),
        ("ap(b, \"c 2/1 ; sqrt\", 8)".into(), Box::new(|b| host(CReal::parse("c 2/1 ; sqrt", b).and_then(|x| x.approx(8, b))))),
    ];
    for (program, n) in [
        ("c 2 ; sqrt 0", 32),
        ("c -1 ; sqrt 0", 8),
        ("c 1 ; exp 0", 24),
        ("c 2 ; log 0", 24),
        ("c 0 ; log 0", 8),
        ("pi", 16),
        ("c 1 ; sin 0", 16),
        ("pi ; cos 0", 16),
        ("c 3 ; inv 0 ; c 3 ; mul 1 2", 12),
    ] {
        out.push((
            format!("ap(b, \"{program}\", {n})"),
            Box::new(move |b| host(CReal::parse(program, b).and_then(|x| x.approx(n, b)))),
        ));
    }
    out
}

const HEADER: &str = r#"module test.numeric_acceptance
import std.dense
theory App extends StdDense {
 let b = ExactBudget(512, 1000000000000000)
 fn e(x: Text) -> Text = concat("err:", nm_category(x))
 fn tt(r: Result<BigTextCalc, Text>) -> Text = match r { err(x) => e(x), ok(v) => concat("ok:", field(v, value)) }
 fn qt(r: Result<BigRationalCalc, Text>) -> Text = match r { err(x) => e(x), ok(v) => tt(bigrat_text(field(v, budget), field(v, value))) }
 fn it(r: Result<BigIntCalc, Text>) -> Text = match r { err(x) => e(x), ok(v) => tt(big_text(field(v, budget), field(v, value))) }
 fn ivt(r: Result<IntervalQCalc, Text>) -> Text = match r { err(x) => e(x), ok(v) => tt(ivq_text(field(v, budget), field(v, value))) }
 fn sq(r: Result<BigSquareRoot, Text>) -> Text = match r { err(x) => e(x), ok(s) => it(ok(BigIntCalc(field(s, root), field(s, budget)), Text)) }
 fn q(t: Text) -> BigRational = match bigrat_parse(b, t) { err(x) => BigRational(false, empty_list(Nat), list(1)), ok(v) => field(v, value) }
 fn z(t: Text) -> BigInt = match big_parse(b, t) { err(x) => BigInt(false, empty_list(Nat)), ok(v) => field(v, value) }
 fn cx(t: Text) -> CReal = match creal_parse(b, t) { err(y) => CReal(empty_list(CRealNode)), ok(v) => field(v, value) }
 fn ap(bb: ExactBudget, s: Text, n: Nat) -> Text = match creal_parse(bb, s) { err(y) => e(y), ok(c) => ivt(creal_approx(field(c, budget), field(c, value), n)) }
 fn ca(s: Text, n: Nat) -> Text = match cc_parse(b, s) { err(y) => e(y), ok(c) => match cc_approx(field(c, budget), field(c, value), n) { err(y) => e(y), ok(a) =>
    match ivq_text(field(a, budget), field(a, re)) { err(y) => e(y), ok(r) => match ivq_text(field(r, budget), field(a, im)) { err(y) => e(y), ok(i) =>
        concat("ok:", concat(field(r, value), concat(";", field(i, value)))) } } } }
 fn dz(t: Text) -> DInt = match dz_parse(b, t) { err(y) => DInt(false, bn_from_nat(0)), ok(v) => field(v, value) }
 fn zt(r: Result<DIntCalc, Text>) -> Text = match r { err(y) => e(y), ok(v) => tt(dz_text(field(v, budget), field(v, value))) }
 fn dd(r: Result<DDivision, Text>) -> Text = match r { err(y) => e(y), ok(d) => match dz_text(field(d, budget), field(d, quotient)) { err(y) => e(y), ok(t) =>
    match dz_text(field(t, budget), field(d, remainder)) { err(y) => e(y), ok(u) => concat("ok:", concat(field(t, value), concat(" ", field(u, value)))) } } }
"#;

const COMPLEX_Q: &str = r#"module test.numeric_acceptance_q
import std.complex_q
theory App extends StdComplexQ {
 let b = ExactBudget(512, 1000000000000000)
 fn c(r: Int, i: Int) -> ComplexQ = match complexq_from_ints(b, r, i) { err(x) => ComplexQ(BigRational(false, empty_list(Nat), list(1)), BigRational(false, empty_list(Nat), list(1))), ok(v) => field(v, value) }
 fn t(r: Result<ComplexQCalc, Text>) -> Text = match r { err(x) => concat("err:", x), ok(v) => match complexq_text(field(v, budget), field(v, value)) { err(x) => concat("err:", x), ok(s) => concat("ok:", field(s, value)) } }
 print_line(t(complexq_div(b, c(int(1), int(2)), c(int(3), int(0) - int(4)))))
 print_line(t(complexq_mul(b, c(int(1), int(2)), c(int(3), int(0) - int(4)))))
 print_line(t(complexq_div(b, c(int(1), int(2)), c(int(0), int(0)))))
 print_line(t(complexq_inv(b, c(int(0), int(1)))))
}
"#;

struct Built {
    dir: PathBuf,
    program: Program,
    c: String,
}

fn build(name: &str, source: &str) -> (Built, Vec<String>) {
    let dir = std::env::temp_dir().join(format!("dlm-{name}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let program = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&program).unwrap().output;
    let c = emit_c(&lower_program(&program, CheckPolicy::trusted_only()).unwrap());
    (Built { dir, program, c }, interpreted)
}

fn native(
    built: &Built,
    compiler: &str,
    flags: &[&str],
    tag: &str,
    runner: Option<&str>,
    envs: &[(&str, &str)],
) -> Option<(Vec<String>, String)> {
    let exe = built.dir.join(format!("main-{tag}"));
    let exe = match build_c_executable_with_flags(
        &built.c,
        &built.dir.join(format!("main-{tag}.c")),
        &exe,
        compiler,
        flags,
    ) {
        Ok(exe) => exe,
        Err(e) => {
            eprintln!("{tag}: build unavailable: {}", e.message);
            return None;
        }
    };
    let mut cmd = match runner {
        Some(r) => {
            let mut c = Command::new(r);
            c.arg(&exe);
            c
        }
        None => Command::new(&exe),
    };
    cmd.env("WINEDEBUG", "-all");
    for (k, v) in envs {
        cmd.env(k, v);
    }
    let output = cmd.output().ok()?;
    assert!(
        output.status.success(),
        "{tag}: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let out = String::from_utf8(output.stdout).unwrap().replace('\r', "");
    Some((
        out.lines().map(str::to_string).collect(),
        String::from_utf8_lossy(&output.stderr).into_owned(),
    ))
}

fn require(var: &str) -> bool {
    std::env::var(var).is_ok_and(|v| v == "1")
}

fn which(candidates: &[&str]) -> Option<String> {
    candidates
        .iter()
        .find(|c| {
            Command::new(c)
                .arg("--version")
                .output()
                .is_ok_and(|o| o.status.success())
        })
        .map(|c| c.to_string())
}

/// Probe the toolchain/runtime independently of the program under test. Once
/// supported, a failed sanitized build or run of that program must fail the test.
fn sanitizer_support(dir: &Path, compiler: &str) -> Option<bool> {
    let source = "#include <stdlib.h>\nint main(int argc, char **argv) { volatile unsigned char *p = malloc((size_t)argc); if (!p) return 2; p[0] = 1; int n = p[0]; free((void *)p); return n - 1; }\n";
    let exe = match build_c_executable_with_flags(
        source,
        &dir.join("sanitizer-probe.c"),
        &dir.join("sanitizer-probe"),
        compiler,
        &[
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-O1",
        ],
    ) {
        Ok(exe) => exe,
        Err(e) => {
            eprintln!("sanitizers unavailable: {}", e.message);
            return None;
        }
    };
    let run = |leaks: bool| {
        let options = if leaks {
            "detect_leaks=1:abort_on_error=1"
        } else {
            "detect_leaks=0:abort_on_error=1"
        };
        match Command::new(&exe)
            .env("ASAN_OPTIONS", options)
            .env("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
            .output()
        {
            Ok(out) if out.status.success() && out.stderr.is_empty() => true,
            Ok(out) => {
                eprintln!(
                    "sanitizer runtime probe ({options}): {}",
                    String::from_utf8_lossy(&out.stderr)
                );
                false
            }
            Err(e) => {
                eprintln!("sanitizer runtime probe ({options}): {e}");
                false
            }
        }
    };
    if !run(false) {
        return None;
    }
    Some(run(true))
}

#[test]
fn cross_family_regression_agrees_everywhere() {
    let cases = cases();
    let mut source = HEADER.to_string();
    let mut expected = Vec::new();
    for (expr, host_case) in &cases {
        source += &format!(" print_line({expr})\n");
        expected.push(host_case(&mut budget()));
    }
    source += "}\n";
    let (built, interpreted) = build("numeric-acceptance", &source);
    for (i, (got, want)) in interpreted.iter().zip(&expected).enumerate() {
        assert_eq!(got, want, "#{i}: {}", cases[i].0);
    }
    assert_eq!(interpreted.len(), expected.len());
    assert!(
        expected.iter().any(|v| v.starts_with("err:domain"))
            && expected.iter().any(|v| v.starts_with("err:budget"))
    );
    for cat in ["invalid_input", "limit", "unrepresentable", "undecided"] {
        assert!(
            expected.iter().any(|v| *v == format!("err:{cat}")),
            "no {cat} case"
        );
    }
    // native C with memory traces
    let (out, trace) = native(
        &built,
        &default_c_compiler(),
        &[],
        "native",
        None,
        &[("DLM_DENSE_TRACE", "1"), ("DLM_OWNERSHIP_TRACE", "1")],
    )
    .expect("native build");
    assert_eq!(out, interpreted);
    assert!(
        trace.contains("dlm dense:") && trace.contains("0 released at exit"),
        "{trace}"
    );
    // sanitizers (ASan, UBSan, LSan) when the toolchain provides them
    let compiler = default_c_compiler();
    if let Some(leaks) = sanitizer_support(&built.dir, &compiler) {
        assert!(
            leaks || !require(REQUIRE_SANITIZERS),
            "LeakSanitizer required but its runtime is unavailable"
        );
        let options = if leaks {
            "detect_leaks=1:abort_on_error=1"
        } else {
            "detect_leaks=0:abort_on_error=1"
        };
        let (out, stderr) = native(
            &built,
            &compiler,
            &[
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-O1",
            ],
            "asan",
            None,
            &[
                ("ASAN_OPTIONS", options),
                ("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1"),
            ],
        )
        .expect("sanitized build and execution after a successful runtime probe");
        assert_eq!(out, interpreted);
        assert!(
            !stderr.contains("runtime error") && !stderr.contains("Sanitizer"),
            "{stderr}"
        );
    } else {
        assert!(!require(REQUIRE_SANITIZERS), "sanitizers required");
    }
    // MinGW-w64 cross build under Wine
    let wine = ["/usr/lib/wine/wine64", "wine64", "wine"]
        .iter()
        .find(|w| Path::new(w).is_file() || which(&[w]).is_some())
        .map(|w| w.to_string());
    match (which(&["x86_64-w64-mingw32-gcc"]), wine) {
        (Some(cc), Some(wine)) => match native(
            &built,
            &cc,
            &["-Wl,--stack,1073741824"],
            "win",
            Some(&wine),
            &[],
        ) {
            Some((out, _)) => assert_eq!(out, interpreted),
            None => assert!(!require(REQUIRE_WINDOWS), "windows build required"),
        },
        _ => assert!(!require(REQUIRE_WINDOWS), "MinGW-w64 and Wine required"),
    }
    let _ = &built.program;
    std::fs::remove_dir_all(&built.dir).unwrap();
}

#[test]
fn exact_complex_q_agrees_with_the_host() {
    let b = &mut budget();
    let c = |r: i128, i: i128| ComplexQ::from_integers(r, i, &mut budget()).unwrap();
    let show = |r: MathResult<ComplexQ>| match r {
        Ok(z) => format!("ok:{};{}", z.real(), z.imag()),
        Err(e) => format!("err:{}", e.0),
    };
    let expected = vec![
        show(c(1, 2).checked_div(&c(3, -4), b)),
        show(c(1, 2).checked_mul(&c(3, -4), b)),
        show(c(1, 2).checked_div(&c(0, 0), b)),
        show(c(0, 1).reciprocal(b)),
    ];
    let (built, interpreted) = build("numeric-acceptance-q", COMPLEX_Q);
    assert_eq!(interpreted, expected);
    let (out, _) =
        native(&built, &default_c_compiler(), &[], "native", None, &[]).expect("native build");
    assert_eq!(out, interpreted);
    std::fs::remove_dir_all(&built.dir).unwrap();
}
