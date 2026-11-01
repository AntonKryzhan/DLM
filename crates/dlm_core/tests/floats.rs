//! v0.114 — machine floats: IEEE-754 binary64 `Float` and `FloatVec`
//! builtins, float literals, and `std.numerics`. The interpreter and native C
//! agree bit-for-bit on the printed results (`docs/FLOATS.md`).
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    parse_module, CheckPolicy, Checker, Program, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-floats-{}-{name}", std::process::id()));
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
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const BASIC: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn v(x: Option<FloatVec>) -> FloatVec = unwrap_or(x, fv_empty())
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => t(y) }
    let a = 1.5
    let b = -0.25
    print_line(t(fl_add(a, b)))
    print_line(t(fl_div(1.0, 3.0)))
    print_line(t(fl_mul(0.1, 3.0)))
    print_line(t(fl_sqrt(2.0)))
    print_line(t(fl_exp(1.0)))
    print_line(t(fl_log(10.0)))
    print_line(t(fl_sin(1.0)))
    print_line(t(fl_pow(2.0, 0.5)))
    print_line(t(fl_div(1.0, 0.0)))
    print_line(t(fl_div(0.0, 0.0)))
    print_line(t(fl_lit("1e300")))
    print_line(t(fl_mul(fl_lit("1e300"), fl_lit("1e10"))))
    print_line(t(fl_lit("123456789012345678")))
    print_line(t(fl_lit("0.000001234")))
    print_line(t(fl_of_nat(340282366920938463463374607431768211455)))
    print_line(t(fl_of_int(int(0) - int(7))))
    print_line(t(fl_round(2.5)))
    print_line(t(fl_round(-2.5)))
    print_line(t(fl_min(-0.0, 0.0)))
    print_line(nat_to_text(fl_bits(1.0)))
    print_line(match fl_to_int(fl_lit("-3.99")) { none => "none", some(i) => int_to_text(i) })
    print_line(o(fl_parse("1.")))
    print_line(print_symbolic(v(fv_from_list(list(1.0, 2.0, 3.5)))))
    print_line(o(fv_dot(v(fv_from_list(list(1.0, 2.0, 3.0))), v(fv_from_list(list(4.0, 5.0, 6.0))))))
    print_line(t(fv_norm(v(fv_from_list(list(3.0, 4.0))))))
    print_line(print_symbolic(v(fv_matmul(v(fv_from_list(list(1.0, 2.0, 3.0, 4.0))), v(fv_from_list(list(5.0, 6.0, 7.0, 8.0))), 2, 2, 2))))
    print_line(print_symbolic(v(fv_transpose(v(fv_from_list(list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0))), 2, 3))))
    print_line(o(fv_max(v(fv_from_list(list(1.0, 9.5, -2.0))))))
    print_line(print_symbolic(v(fv_axpy(2.0, v(fv_new(3, 1.0)), v(fv_new(3, 0.5))))))
    print_line(if_then_else(is_none(fv_add(v(fv_new(2, 1.0)), v(fv_new(3, 1.0)))), "mismatch", "?"))
    print_line(if_then_else(equal(fl_lit("nan"), fl_lit("nan")), "nan-eq", "nan-ne"))
"#;

#[test]
fn float_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "basic",
        &format!("module test.floats\ntheory App {{\n{BASIC}}}\n"),
    );
    let sin1 = 1.0f64.sin();
    let want = [
        "1.25".to_string(),
        "0.3333333333333333".into(),
        "0.30000000000000004".into(),
        "1.4142135623730951".into(),
        dlm_core::float::text(1.0f64.exp()),
        dlm_core::float::text(10.0f64.ln()),
        dlm_core::float::text(sin1),
        dlm_core::float::text(2.0f64.powf(0.5)),
        "inf".into(),
        "nan".into(),
        "1.0e300".into(),
        "inf".into(),
        "1.2345678901234568e17".into(),
        "1.234e-6".into(),
        "3.402823669209385e38".into(),
        "-7.0".into(),
        "3.0".into(),
        "-3.0".into(),
        "-0.0".into(),
        "4607182418800017408".into(),
        "-3".into(),
        "none".into(),
        "fvec[1.0, 2.0, 3.5]".into(),
        "fvec[1.0, 2.0, 3.5]".into(),
        "32.0".into(),
        "5.0".into(),
        "fvec[19.0, 22.0, 43.0, 50.0]".into(),
        "fvec[19.0, 22.0, 43.0, 50.0]".into(),
        "fvec[1.0, 4.0, 2.0, 5.0, 3.0, 6.0]".into(),
        "fvec[1.0, 4.0, 2.0, 5.0, 3.0, 6.0]".into(),
        "9.5".into(),
        "fvec[2.5, 2.5, 2.5]".into(),
        "fvec[2.5, 2.5, 2.5]".into(),
        "mismatch".into(),
        "nan-eq".into(),
    ];
    assert_eq!(out, want);
}

const NUMERICS: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => t(y) }
    fn v(x: Option<FloatVec>) -> FloatVec = unwrap_or(x, fv_empty())
    fn sq2(x: Float) -> Float = fl_sub(fl_mul(x, x), 2.0)
    fn dsq2(x: Float) -> Float = fl_mul(2.0, x)
    fn decay(t: Float, y: Float) -> Float = fl_neg(y)
    fn osc(t: Float, y: FloatVec) -> FloatVec = v(fv_from_list(list(unwrap_or(fv_get(y, 1), 0.0), fl_neg(unwrap_or(fv_get(y, 0), 0.0)))))
    fn sinf(x: Float) -> Float = fl_sin(x)
    fn expf(x: Float) -> Float = fl_exp(x)
    fn bowl(x: Float) -> Float = fl_add(fl_mul(fl_sub(x, 1.5), fl_sub(x, 1.5)), 2.0)
    let data = v(fv_from_list(list(2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0)))
    let ys = v(fv_from_list(list(3.1, 4.9, 7.2, 8.8, 11.1)))
    let xs = v(fv_from_list(list(1.0, 2.0, 3.0, 4.0, 5.0)))
    print_line(o(bisection(sq2, 0.0, 2.0, fl_lit("1e-12"), 100)))
    print_line(o(newton(sq2, dsq2, 1.0, fl_lit("1e-15"), 50)))
    print_line(o(secant(sq2, 1.0, 2.0, fl_lit("1e-15"), 50)))
    print_line(t(derivative(sinf, 0.0, fl_lit("1e-5"))))
    print_line(t(simpson(sinf, 0.0, 3.141592653589793, 100)))
    print_line(t(trapezoid(expf, 0.0, 1.0, 1000)))
    print_line(t(rk4(decay, 0.0, 1.0, 0.01, 100)))
    print_line(print_symbolic(rk4_vec(osc, 0.0, v(fv_from_list(list(1.0, 0.0))), 0.01, 314)))
    print_line(t(golden_min(bowl, 0.0, 4.0, fl_lit("1e-9"), 200)))
    print_line(t(gradient_descent(fun(x: Float, fl_mul(2.0, fl_sub(x, 3.0))), 0.0, 0.1, fl_lit("1e-12"), 1000)))
    print_line(t(horner(v(fv_from_list(list(1.0, -3.0, 2.0))), 5.0)))
    print_line(o(interpolate(xs, ys, 2.5)))
    print_line(o(mean(data)))
    print_line(o(stddev(data)))
    print_line(o(median(data)))
    print_line(o(correlation(xs, ys)))
    print_line(match linear_fit(xs, ys) { none => "none", some(f) => concat(t(field(f, slope)), concat(" ", t(field(f, intercept)))) })
    print_line(t(field(rng_uniform(42), value)))
    print_line(t(field(rng_normal(7, 0.0, 1.0), value)))
    print_line(o(mean(rng_samples(1, 1000))))
    print_line(print_symbolic(v(solve_linear(v(fv_from_list(list(2.0, 1.0, -1.0, -3.0, -1.0, 2.0, -2.0, 1.0, 2.0))), v(fv_from_list(list(8.0, -11.0, -3.0))), 3))))
    print_line(if_then_else(is_none(solve_linear(v(fv_from_list(list(1.0, 2.0, 2.0, 4.0))), v(fv_from_list(list(1.0, 1.0))), 2)), "singular", "?"))
"#;

#[test]
fn std_numerics_agree_between_interpreter_and_native() {
    let out = run_both(
        "numerics",
        &format!("module test.numerics\nimport std.numerics\ntheory App extends StdNumerics {{\n{NUMERICS}}}\n"),
    );
    assert_eq!(out.len(), 24, "{out:?}");
    let f = |i: usize| out[i].parse::<f64>().unwrap_or(f64::NAN);
    let near = |i: usize, want: f64, tol: f64| {
        assert!((f(i) - want).abs() <= tol, "#{i}: {} vs {want}", out[i])
    };
    let r2 = 2.0f64.sqrt();
    near(0, r2, 1e-11);
    near(1, r2, 1e-15);
    near(2, r2, 1e-15);
    near(3, 1.0, 1e-9);
    near(4, 2.0, 1e-7);
    near(5, std::f64::consts::E - 1.0, 1e-6);
    near(6, (-1.0f64).exp(), 1e-9);
    assert!(
        out[7].starts_with("fvec[-0.99999") && out[7] == out[8],
        "{}",
        out[7]
    );
    near(9, 1.5, 1e-6);
    near(10, 3.0, 1e-9);
    assert_eq!(out[11], "12.0");
    assert_eq!(out[12], "6.050000000000001");
    assert_eq!(out[13], "5.0");
    near(14, (32.0f64 / 7.0).sqrt(), 1e-15);
    assert_eq!(out[15], "4.5");
    near(16, 0.99865, 1e-4);
    assert_eq!(out[17], "1.9899999999999998 1.0500000000000016");
    let s = 42u128 * 6364136223846793005 + 1442695040888963407;
    let s = (s % (1u128 << 64)) as u64;
    assert_eq!(
        out[18],
        dlm_core::float::text((s >> 11) as f64 / 9007199254740992.0)
    );
    assert!(f(19).abs() < 6.0);
    near(20, 0.5, 0.05);
    assert_eq!(
        out[21],
        "fvec[2.0, 3.0000000000000004, -0.9999999999999999]"
    );
    assert_eq!(out[21], out[22]);
    assert_eq!(out[23], "singular");
}

#[test]
fn canonical_text_round_trips_and_parses() {
    use dlm_core::float::{parse, text, valid_literal};
    for x in [
        0.1,
        1.0 / 3.0,
        1e300,
        -1.234e-6,
        5e-324,
        f64::MAX,
        123456789012345680.0,
        0.0,
        -0.0,
        2.5,
    ] {
        let t = text(x);
        assert_eq!(parse(&t).map(f64::to_bits), Some(x.to_bits()), "{t}");
    }
    for bad in ["", "1.", ".5", "1e", "--1", "1.0x", "nan1"] {
        assert!(!valid_literal(bad) || parse(bad).is_none(), "{bad}");
    }
}

fn type_errors(body: &str) -> Vec<String> {
    let src = format!("module test.floats\ntheory App {{\n{body}\n}}\n");
    let p = Program::single(parse_module(&src).unwrap_or_else(|d| panic!("{d:?}")));
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    report
        .diagnostics
        .iter()
        .map(|d| d.message.clone())
        .collect()
}

#[test]
fn floats_are_typed() {
    for bad in [
        "fn f() -> Float = fl_add(1, 2.0)",
        "fn f() -> Nat = fl_mul(2.0, 3.0)",
        "fn f() -> Bool = fl_lt(1.0, int(1))",
        "fn f(v: FloatVec) -> Option<Float> = fv_get(v, 1.0)",
        "fn f(v: FloatVec) -> FloatVec = fv_scale(2.0, v)",
        "fn f() -> Float = add(1.0, 2.0)",
    ] {
        assert!(!type_errors(bad).is_empty(), "accepted: {bad}");
    }
    for ok in [
        "fn f(x: Float) -> Float = fl_add(fl_mul(x, x), -1.5)",
        "fn f(v: FloatVec) -> Float = unwrap_or(fv_get(v, 0), 0.0)",
        "fn f(x: Float) -> Bool = equal(x, 2.0)",
    ] {
        let e = type_errors(ok);
        assert!(e.is_empty(), "{ok}\n{e:?}");
    }
}
