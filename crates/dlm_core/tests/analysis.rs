//! v0.117 — FFT, convolution and linear programming (`fft_*`, `lp_*`),
//! `std.analysis` (automatic differentiation, quadrature, adaptive ODEs,
//! splines, polynomials and roots) and `std.optimize` (Nelder–Mead, BFGS,
//! Levenberg–Marquardt, linear programs). Interpreter and native C agree on
//! every line; values match references within `1e-8` (libm may differ by
//! platform in the last bits).
use dlm_core::spectral;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-analysis-{}-{name}", std::process::id()));
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

/// Same text outside numbers, numbers equal within `tol` (relative to max(1, |x|)).
fn approx_line(got: &str, want: &str, tol: f64) -> bool {
    let split = |s: &str| -> (String, Vec<f64>) {
        let (mut skeleton, mut nums, mut cur) = (String::new(), Vec::new(), String::new());
        let flush = |cur: &mut String, skeleton: &mut String, nums: &mut Vec<f64>| {
            if !cur.is_empty() {
                match cur.parse::<f64>() {
                    Ok(x) => {
                        nums.push(x);
                        skeleton.push('#');
                    }
                    Err(_) => skeleton.push_str(cur),
                }
                cur.clear();
            }
        };
        for ch in s.chars() {
            if ch.is_ascii_digit() || ch == '.' || ch == '-' || ch == 'e' {
                cur.push(ch);
            } else {
                flush(&mut cur, &mut skeleton, &mut nums);
                skeleton.push(ch);
            }
        }
        flush(&mut cur, &mut skeleton, &mut nums);
        (skeleton, nums)
    };
    let ((a, x), (b, y)) = (split(got), split(want));
    a == b
        && x.len() == y.len()
        && x.iter()
            .zip(&y)
            .all(|(p, q)| (p - q).abs() <= tol * q.abs().max(1.0))
}

const PROGRAM: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => t(y) }
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn mn(m: Minimum) -> Text = concat(print_symbolic(field(m, x)), concat(" ", t(field(m, value))))
    fn ad1(x: Dual) -> Dual = d_mul(d_sin(x), d_exp(x))
    fn cubic(x: Dual) -> Dual = d_sub(d_mul(x, d_mul(x, x)), d_const(2.0))
    fn sq(v: FloatVec) -> Float = let a = unwrap_or(fv_get(v, 0), 0.0) in let b = unwrap_or(fv_get(v, 1), 0.0) in fl_add(fl_mul(fl_sub(a, 1.0), fl_sub(a, 1.0)), fl_mul(10.0, fl_mul(fl_add(b, 2.0), fl_add(b, 2.0))))
    fn rosen(v: FloatVec) -> Float = let a = unwrap_or(fv_get(v, 0), 0.0) in let b = unwrap_or(fv_get(v, 1), 0.0) in
        fl_add(fl_mul(100.0, fl_mul(fl_sub(b, fl_mul(a, a)), fl_sub(b, fl_mul(a, a)))), fl_mul(fl_sub(1.0, a), fl_sub(1.0, a)))
    fn expo(p: FloatVec) -> FloatVec = let a = unwrap_or(fv_get(p, 0), 0.0) in let k = unwrap_or(fv_get(p, 1), 0.0) in
        fvl(map(list(0.0, 1.0, 2.0, 3.0, 4.0), fun(x: Float, fl_sub(fl_mul(a, fl_exp(fl_mul(k, x))), fl_mul(2.0, fl_exp(fl_mul(-0.5, x)))))))
    fn sinf(x: Float) -> Float = fl_sin(x)
    fn decay(t: Float, y: Float) -> Float = fl_neg(y)
    fn rx(c: Cx) -> Text = concat(t(fl_div(fl_round(fl_mul(field(c, re), 1000000.0)), 1000000.0)), concat(" ", t(fl_div(fl_round(fl_mul(field(c, im), 1000000.0)), 1000000.0))))
    print_line(t(ad_derivative(ad1, 1.0)))
    print_line(o(ad_newton(cubic, 1.0, fl_lit("1e-15"), 50)))
    print_line(print_symbolic(gradient(sq, fvl(list(0.0, 0.0)), fl_lit("1e-6"))))
    print_line(print_symbolic(field(jacobian(expo, fvl(list(1.0, 0.0)), fl_lit("1e-6")), data)))
    print_line(t(integrate(sinf, 0.0, 3.141592653589793, 4)))
    print_line(t(integrate_adaptive(sinf, 0.0, 3.141592653589793, fl_lit("1e-12"), 30)))
    print_line(o(ode_solve(decay, 0.0, 1.0, 1.0, fl_lit("1e-10"), 10000)))
    let sp = natural_spline(fvl(list(0.0, 1.0, 2.0, 3.0)), fvl(list(0.0, 1.0, 0.0, 1.0)))
    print_line(match sp { none => "none", some(s) => concat(print_symbolic(field(s, ms)), concat(" ", o(spline_eval(s, 1.5)))) })
    print_line(t(poly_eval(poly(list(1.0, -3.0, 2.0)), 5.0)))
    print_line(print_symbolic(poly_mul(poly(list(1.0, 1.0)), poly(list(-1.0, 1.0)))))
    print_line(print_symbolic(poly_derivative(poly(list(1.0, 2.0, 3.0)))))
    print_line(print_symbolic(poly_integral(poly(list(1.0, 2.0, 3.0)))))
    print_line(print_symbolic(poly_add(poly(list(1.0)), poly(list(0.0, 2.0)))))
    print_line(join(map(poly_roots(poly(list(-6.0, 11.0, -6.0, 1.0)), 200), rx), " | "))
    print_line(join(map(poly_roots(poly(list(1.0, 0.0, 1.0)), 200), rx), " | "))
    print_line(of(power_spectrum(fvl(list(1.0, 0.0, -1.0, 0.0)))))
    print_line(of(convolve(fvl(list(1.0, 2.0)), fvl(list(3.0, 4.0, 5.0)))))
    print_line(of(fft_forward(fvl(list(1.0, 0.0, 2.0, 0.0, 3.0, 0.0)))))
    print_line(mn(nelder_mead(sq, fvl(list(0.0, 0.0)), 0.5, fl_lit("1e-12"), 500)))
    print_line(mn(bfgs_numeric(rosen, fvl(list(-1.2, 1.0)), fl_lit("1e-7"), fl_lit("1e-8"), 200)))
    print_line(mn(levenberg_marquardt(expo, fvl(list(1.0, 0.0)), fl_lit("1e-20"), 100)))
    let lp = linear_program(fvl(list(3.0, 5.0)), unwrap_or(matrix(3, 2, list(1.0, 0.0, 0.0, 2.0, 3.0, 2.0)), Matrix(0, 0, fv_empty())), fvl(list(4.0, 12.0, 18.0)))
    print_line(concat(nat_to_text(field(lp, status)), concat(" ", concat(print_symbolic(field(lp, x)), concat(" ", t(field(lp, value)))))))
    print_line(nat_to_text(lp_status(fvl(list(1.0)), fvl(list(1.0, -1.0)), fvl(list(1.0, -2.0)), 2, 1)))
    print_line(nat_to_text(lp_status(fvl(list(1.0)), fvl(list(-1.0)), fvl(list(1.0)), 1, 1)))
    print_line(of(lp_solve(fvl(list(1.0, 1.0)), fvl(list(-1.0, -1.0, 1.0, 0.0)), fvl(list(-2.0, 3.0)), 2, 2)))
"#;

#[test]
fn analysis_and_optimization_agree_between_interpreter_and_native() {
    let out = run_both("program", &format!("module test.analysis\nimport std.optimize\ntheory App extends StdOptimize {{\n{PROGRAM}}}\n"));
    let want: &[&str] = &[
        "3.7560492270947274",
        "1.2599210498948732",
        "fvec[-1.999999998503199, 39.99999999848569]",
        "fvec[-1.999999998503199, 39.99999999848569]",
        "fvec[1.0000000000287557, 0.0, 0.9999999999732445, 0.9999999999732445, 0.9999999999732445, 2.000000000002, 1.0000000000287557, 2.9999999999752447, 0.9999999999732445, 3.999999999948489]",
        "fvec[1.0000000000287557, 0.0, 0.9999999999732445, 0.9999999999732445, 0.9999999999732445, 2.000000000002, 1.0000000000287557, 2.9999999999752447, 0.9999999999732445, 3.999999999948489]",
        "2.0000000000000724",
        "2.0",
        "0.36787944118380284",
        "fvec[0.0, -4.0, 4.0, 0.0]",
        "fvec[0.0, -4.0, 4.0, 0.0] 0.5",
        "36.0",
        "fvec[-1.0, 0.0, 1.0]",
        "fvec[-1.0, 0.0, 1.0]",
        "fvec[2.0, 6.0]",
        "fvec[2.0, 6.0]",
        "fvec[0.0, 1.0, 1.0, 1.0]",
        "fvec[0.0, 1.0, 1.0, 1.0]",
        "fvec[1.0, 2.0]",
        "fvec[1.0, 2.0]",
        "3.0 0.0 | 2.0 0.0 | 1.0 0.0",
        "0.0 1.0 | 0.0 -1.0",
        "fvec[0.0, 4.0, 0.0, 4.0]",
        "fvec[0.0, 4.0, 0.0, 4.0]",
        "fvec[3.0, 10.0, 13.0, 10.0]",
        "fvec[3.0, 10.0, 13.0, 10.0]",
        "fvec[6.0, 0.0, -1.5000000000000009, 0.8660254037844377, -1.5000000000000002, -0.8660254037844393]",
        "fvec[6.0, 0.0, -1.5000000000000009, 0.8660254037844377, -1.5000000000000002, -0.8660254037844393]",
        "fvec[1.0, -2.0]",
        "fvec[1.0, -2.0] 0.0",
        "fvec[1.0000000005304717, 1.0000000010537151]",
        "fvec[1.0000000005304717, 1.0000000010537151] 2.8662490263865593e-19",
        "fvec[1.9999999999999976, -0.4999999999999986]",
        "fvec[1.9999999999999976, -0.4999999999999986] 1.0381532772224956e-29",
        "fvec[2.0, 6.0]",
        "0 fvec[2.0, 6.0] 36.0",
        "1",
        "2",
        "none",
    ];
    assert_eq!(out.len(), want.len(), "{out:?}");
    for (i, (g, w)) in out.iter().zip(want).enumerate() {
        assert!(approx_line(g, w, 1e-8), "#{i}: {g} vs {w}");
    }
}

#[test]
fn spectral_kernels() {
    // FFT round trip on power-of-two and direct lengths, Parseval
    for n in [1usize, 2, 8, 64, 3, 5, 12] {
        let x: Vec<f64> = (0..2 * n)
            .map(|i| ((i * 7 + 3) % 11) as f64 - 5.0)
            .collect();
        let f = spectral::fft(&x, false).unwrap();
        let back = spectral::fft(&f, true).unwrap();
        assert!(
            back.iter().zip(&x).all(|(a, b)| (a - b).abs() < 1e-11),
            "{n}"
        );
        let e1: f64 = x.iter().map(|v| v * v).sum();
        let e2: f64 = f.iter().map(|v| v * v).sum::<f64>() / n as f64;
        assert!((e1 - e2).abs() < 1e-9 * e1.max(1.0), "{n}");
    }
    assert!(spectral::fft(&[1.0, 2.0, 3.0], false).is_none());
    assert!(spectral::fft(&vec![0.0; 2 * (spectral::DFT_MAX + 1)], false).is_none());
    let c = spectral::convolve(&[1.0, 2.0, 3.0], &[0.0, 1.0, 0.5]).unwrap();
    assert!(c
        .iter()
        .zip(&[0.0, 1.0, 2.5, 4.0, 1.5])
        .all(|(a, b)| (a - b).abs() < 1e-12));
    assert_eq!(spectral::convolve(&[], &[1.0]), Some(vec![]));
    // LP: optimal, infeasible, unbounded, malformed; phase one with a >= constraint
    let (s, x) = spectral::simplex(
        &[3.0, 5.0],
        &[1.0, 0.0, 0.0, 2.0, 3.0, 2.0],
        &[4.0, 12.0, 18.0],
        3,
        2,
    );
    assert_eq!(s, 0);
    assert!((x[0] - 2.0).abs() < 1e-12 && (x[1] - 6.0).abs() < 1e-12);
    let (s, x) = spectral::simplex(
        &[-1.0, -1.0],
        &[-1.0, -2.0, -3.0, -1.0],
        &[-4.0, -6.0],
        2,
        2,
    );
    assert_eq!(s, 0);
    assert!(
        (x[0] - 1.6).abs() < 1e-12 && (x[1] - 1.2).abs() < 1e-12,
        "{x:?}"
    );
    assert_eq!(
        spectral::simplex(&[1.0], &[1.0, -1.0], &[1.0, -2.0], 2, 1).0,
        1
    );
    assert_eq!(spectral::simplex(&[1.0], &[-1.0], &[1.0], 1, 1).0, 2);
    assert_eq!(spectral::simplex(&[1.0], &[1.0], &[1.0, 2.0], 1, 1).0, 3);
    assert_eq!(spectral::simplex(&[f64::NAN], &[1.0], &[1.0], 1, 1).0, 3);
}
