//! Boundary regressions for the cumulative 0.114–0.118 review.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use dlm_core::{float, graph, linalg as la, number, spectral};
use std::path::Path;
use std::process::Command;

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-regress118-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let p = load_program_with_paths(&path, &[root.join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native: Vec<_> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn integer_and_vector_endpoints() {
    let bound = 2f64.powi(127);
    assert_eq!(float::to_int(-bound), Some(i128::MIN));
    assert_eq!(float::to_int(bound), None);
    assert_eq!(float::to_int(f64::from_bits((-bound).to_bits() + 1)), None);
    assert!(float::to_int(f64::from_bits(bound.to_bits() - 1)).is_some());
    assert_eq!(graph::range(u128::MAX, 1), Some(vec![u128::MAX]));
    assert_eq!(graph::range(u128::MAX, 2), None);
    assert_eq!(graph::range(u128::MAX, 0), Some(vec![]));
}

#[test]
fn empty_shapes_do_not_loop_over_unrepresented_dimensions() {
    assert_eq!(float::matmul(&[], &[], u128::MAX, 0, 0), Some(vec![]));
    assert_eq!(float::transpose(&[], u128::MAX, 0), Some(vec![]));
    assert!(la::svd_values(&[], 0, u128::MAX).is_none());
    assert!(la::qr(&[], 0, (1 << 24) + 1).is_none());
}

#[test]
fn linalg_rejects_nonfinite_data_and_tolerances() {
    for x in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert!(la::lu(&[x], 1).is_none());
        assert!(la::lu_perm(&[x], 1).is_none());
        assert!(la::det(&[x], 1).is_none());
        assert!(la::inverse(&[x], 1).is_none());
        assert!(la::trace(&[x], 1).is_none());
        assert!(float::solve(&[x], &[1.0], 1).is_none());
    }
    for tol in [-1.0, f64::NAN, f64::INFINITY] {
        assert!(la::rank(&[0.0], 1, 1, tol).is_none());
        assert!(la::cg(&[0, 1], &[0], &[1.0], &[1.0], tol, 10).is_none());
    }
    assert!(la::cg(&[0, 1], &[0], &[1.0], &[1e308], 1e-10, 10).is_none());
    assert!(la::inverse(&[1e-320], 1).is_none());
}

#[test]
fn scaled_factorizations_preserve_mathematical_identities() {
    for scale in [1e200, 1e-200] {
        let (e, v) = la::eigh(&[scale, scale, scale, scale], 2).unwrap();
        assert!((e[0] / scale).abs() < 1e-14);
        assert!((e[1] / scale - 2.0).abs() < 1e-14);
        for j in 0..2 {
            for i in 0..2 {
                assert!((v[j] + v[2 + j] - (e[j] / scale) * v[i * 2 + j]).abs() < 1e-14);
            }
        }
        let s = la::svd_values(&[3.0 * scale, 4.0 * scale], 1, 2).unwrap();
        assert_eq!(s.len(), 1);
        assert!((s[0] / scale - 5.0).abs() < 1e-14);
        let (q, r) = la::qr(&[3.0 * scale, 4.0 * scale], 2, 1).unwrap();
        assert!((q[0] * (r[0] / scale) - 3.0).abs() < 1e-14);
        assert!((q[1] * (r[0] / scale) - 4.0).abs() < 1e-14);
    }
    assert_eq!(la::svd_values(&[3.0, 4.0], 1, 2).unwrap().len(), 1);
}

#[test]
fn native_float_and_linalg_boundaries() {
    let out = run_both(
        "native",
        r#"module test.review118_native
theory App {
    fn check(b: Bool) -> Text = if_then_else(b, "ok", "FAIL")
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    print_line(check(fl_eq(fl_lit("inf"), fl_div(1.0, 0.0))))
    print_line(check(fl_eq(fl_lit("-inf"), fl_div(-1.0, 0.0))))
    print_line(check(equal(nat_to_text(fl_bits(fl_lit("nan"))), "9221120237041090560")))
    print_line(check(equal(nat_to_text(fl_bits(fl_lit("bad"))), "9221120237041090560")))
    print_line(check(is_none(fl_parse("inf"))))
    print_line(check(is_some(fl_to_int(fl_neg(fl_pow(2.0, 127.0))))))
    print_line(check(is_none(fl_to_int(fl_pow(2.0, 127.0)))))
    print_line(check(is_some(nv_range(340282366920938463463374607431768211455, 1))))
    print_line(check(is_none(nv_range(340282366920938463463374607431768211455, 2))))
    print_line(check(is_some(fv_matmul(fv_empty(), fv_empty(), 340282366920938463463374607431768211455, 0, 0))))
    print_line(check(is_some(fv_transpose(fv_empty(), 340282366920938463463374607431768211455, 0))))
    print_line(check(is_none(la_lu(fv(list(fl_lit("inf"))), 1))))
    print_line(check(is_none(la_lu_perm(fv(list(fl_lit("inf"))), 1))))
    print_line(check(is_none(la_det(fv(list(fl_lit("nan"))), 1))))
    print_line(check(is_none(la_inverse(fv(list(fl_lit("inf"))), 1))))
    print_line(check(is_none(la_inverse(fv(list(fl_lit("1e-320"))), 1))))
    print_line(check(is_none(la_trace(fv(list(fl_lit("inf"))), 1))))
    print_line(check(is_none(fv_solve(fv(list(fl_lit("inf"))), fv(list(1.0)), 1))))
    print_line(check(is_none(la_rank(fv(list(0.0)), 1, 1, -1.0))))
    print_line(check(is_none(la_cg(nv(list(0, 1)), nv(list(0)), fv(list(1.0)), fv(list(fl_lit("1e308"))), 0.0001, 10))))
    print_line(check(is_none(la_cg(nv(list(0, 1)), nv(list(0)), fv(list(1.0)), fv(list(1.0)), fl_lit("inf"), 10))))
    print_line(check(is_none(la_qr_q(fv_empty(), 0, 16777216))))
    print_line(check(is_none(la_svd_values(fv_empty(), 0, 340282366920938463463374607431768211455))))
    let s = unwrap_or(la_svd_values(fv(list(3.0, 4.0)), 1, 2), fv_empty())
    print_line(check(and(nat_eq(fv_len(s), 1), fl_lt(fl_abs(fl_sub(unwrap_or(fv_get(s, 0), 0.0), 5.0)), 0.00001))))
    let e = unwrap_or(la_eigh_values(fv(list(fl_lit("1e200"), fl_lit("1e200"), fl_lit("1e200"), fl_lit("1e200"))), 2), fv_empty())
    print_line(check(fl_lt(fl_abs(fl_sub(fl_div(unwrap_or(fv_get(e, 1), 0.0), fl_lit("1e200")), 2.0)), 0.00001)))
    let r = unwrap_or(la_qr_r(fv(list(fl_lit("3e-200"), fl_lit("4e-200"))), 2, 1), fv_empty())
    print_line(check(fl_lt(fl_abs(fl_sub(fl_abs(fl_div(unwrap_or(fv_get(r, 0), 0.0), fl_lit("1e-200"))), 5.0)), 0.00001)))
}
"#,
    );
    assert!(!out.is_empty() && out.iter().all(|s| s == "ok"), "{out:?}");
}

#[test]
fn standard_library_invalid_inputs_and_zero_cycles() {
    let out = run_both(
        "library",
        r#"module test.review118_library
import std.linalg
theory App extends StdLinalg {
    fn check(b: Bool) -> Text = if_then_else(b, "ok", "FAIL")
    let g = add_edge(add_edge(add_edge(graph(3), 1, 2, 0.0), 2, 1, 0.0), 0, 1, 1.0)
    print_line(check(match shortest_path(g, 0, 2) { none => false, some(p) => equal(field(p, vertices), list(0, 1, 2)) }))
    print_line(check(not(has_negative_cycle(g, 3))))
    print_line(check(not(is_valid(Graph(1, unwrap_or(nv_from_list(list(0)), nv_empty()), unwrap_or(nv_from_list(list(0)), nv_empty()), fv_empty())))))
    print_line(check(is_none(csr(0, 1, list(Triplet(0, 0, 1.0))))))
    print_line(check(is_none(csr(1, 0, list(Triplet(0, 0, 1.0))))))
    print_line(check(is_none(matmul(Matrix(2, 0, fv_empty()), Matrix(1, 0, fv_empty())))))
    print_line(check(is_none(trace(Matrix(0, 2, fv_empty())))))
    print_line(check(is_none(row(Matrix(0, 0, fv_empty()), 0))))
    print_line(check(is_none(col(Matrix(0, 0, fv_empty()), 0))))
    print_line(check(is_none(get(Matrix(1, 2, unwrap_or(fv_new(2, 0.0), fv_empty())), 340282366920938463463374607431768211455, 0))))
    print_line(check(is_none(zeros(340282366920938463463374607431768211455, 2))))
    print_line(check(is_none(matrix(340282366920938463463374607431768211455, 2, list(1.0)))))
    print_line(check(not(fl_sign_change(fl_lit("1e-200"), fl_lit("1e-200")))))
    print_line(check(fl_is_finite(fl_mid(fl_lit("1e308"), fl_lit("1e308")))))
    print_line(check(is_none(bisection(fun(x: Float, 1.0), 1.0, 0.0, 0.001, 10))))
    print_line(check(fl_eq(unwrap_or(bisection(fun(x: Float, x), 0.0, 1.0, 0.001, 0), -1.0), 0.0)))
    print_line(check(is_none(bisection(fun(x: Float, fl_lit("1e-200")), 0.0, 1.0, 0.001, 20))))
    print_line(check(equal(nat_to_text(rng_next(340282366920938463463374607431768211455)), nat_to_text(rng_next(18446744073709551615)))))
}
"#,
    );
    assert!(!out.is_empty() && out.iter().all(|s| s == "ok"), "{out:?}");
}

#[test]
fn analysis_and_distribution_edge_cases() {
    let out = run_both(
        "analysis",
        r#"module test.review118_analysis
import std.analysis
theory App extends StdAnalysis {
    fn check(b: Bool) -> Text = if_then_else(b, "ok", "FAIL")
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    print_line(check(fl_eq(field(d_pow(d_var(0.0), 0.0), d), 0.0)))
    print_line(check(fl_lt(fl_abs(fl_sub(unwrap_or(ode_solve(fun(t: Float, y: Float, y), 1.0, fl_exp(1.0), 0.0, 0.000000001, 1000), 0.0), 1.0)), 0.00001)))
    print_line(check(is_none(ode_solve(fun(t: Float, y: Float, y), 0.0, 1.0, 1.0, -1.0, 10))))
    print_line(check(is_none(natural_spline(fv(list(0.0, 0.0)), fv(list(1.0, 2.0))))))
    print_line(check(is_none(natural_spline(fv(list(1.0, 0.0)), fv(list(1.0, 2.0))))))
    print_line(check(is_none(spline_eval(Spline(fv_empty(), fv_empty(), fv_empty()), 0.0))))
    print_line(check(fl_lt(fl_abs(fl_add(gamma(-0.5), fl_mul(2.0, sqrt_pi()))), 0.00001)))
    print_line(check(fl_eq(gamma_pdf(0.0, 1.0, 2.0), 0.5)))
    print_line(check(fl_eq(gamma_pdf(0.0, 0.5, 2.0), fl_inf())))
    print_line(check(fl_eq(gamma_p(2.0, fl_inf()), 1.0)))
    print_line(check(not(point_in_triangle(Vec2(10.0, 0.0), Vec2(0.0, 0.0), Vec2(1.0, 0.0), Vec2(2.0, 0.0)))))
    print_line(check(point_in_triangle(Vec2(1.5, 0.0), Vec2(0.0, 0.0), Vec2(1.0, 0.0), Vec2(2.0, 0.0))))
}
"#,
    );
    assert!(!out.is_empty() && out.iter().all(|s| s == "ok"), "{out:?}");
}

#[test]
fn fft_matches_independent_direct_transform() {
    for n in [1usize, 3, 4, 7, 16] {
        let x: Vec<_> = (0..2 * n)
            .map(|i| ((i * 17 % 23) as f64) / 8.0 - 1.0)
            .collect();
        let actual = spectral::fft(&x, false).unwrap();
        for k in 0..n {
            let (mut re, mut im) = (0.0, 0.0);
            for j in 0..n {
                let a = -2.0 * std::f64::consts::PI * (j * k) as f64 / n as f64;
                re += x[2 * j] * a.cos() - x[2 * j + 1] * a.sin();
                im += x[2 * j] * a.sin() + x[2 * j + 1] * a.cos();
            }
            assert!((actual[2 * k] - re).abs() < 1e-11);
            assert!((actual[2 * k + 1] - im).abs() < 1e-11);
        }
    }
}

#[test]
fn simplex_matches_bounded_vertex_enumeration() {
    // Two-variable bounded LPs: enumerate all boundary intersections independently.
    for seed in 0..160 {
        let c = [
            ((seed * 7 % 13) as f64) - 5.0,
            ((seed * 11 % 17) as f64) - 6.0,
        ];
        let a = [
            1.0,
            0.0,
            0.0,
            1.0,
            (seed % 7) as f64 - 3.0,
            (seed % 5) as f64 - 2.0,
        ];
        let b = [4.0, 5.0, (seed % 11) as f64 - 4.0];
        let lines = [
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (a[0], a[1], b[0]),
            (a[2], a[3], b[1]),
            (a[4], a[5], b[2]),
        ];
        let mut best: Option<f64> = None;
        for i in 0..lines.len() {
            for j in 0..i {
                let (p, q, r) = lines[i];
                let (s, t, u) = lines[j];
                let det = p * t - q * s;
                if det == 0.0 {
                    continue;
                }
                let (x, y) = ((r * t - q * u) / det, (p * u - r * s) / det);
                if x >= -1e-9
                    && y >= -1e-9
                    && (0..3).all(|k| a[2 * k] * x + a[2 * k + 1] * y <= b[k] + 1e-9)
                {
                    let val = c[0] * x + c[1] * y;
                    best = Some(best.map_or(val, |old| old.max(val)));
                }
            }
        }
        let (status, x) = spectral::simplex(&c, &a, &b, 3, 2);
        match best {
            None => assert_eq!(status, 1, "seed {seed}"),
            Some(want) => {
                assert_eq!(status, 0, "seed {seed}");
                assert!(x.iter().all(|v| *v >= -1e-8));
                assert!((0..3).all(|k| a[2 * k] * x[0] + a[2 * k + 1] * x[1] <= b[k] + 1e-8));
                assert!(
                    (c[0] * x[0] + c[1] * x[1] - want).abs() < 1e-7,
                    "seed {seed}"
                );
            }
        }
    }
}

#[test]
fn finite_field_and_permutation_oracles() {
    for p in [2u128, 3, 5, 7, 11, 13, 17, 19] {
        for a in 0..p {
            let root = number::sqrt_mod(a, p);
            let exists = (0..p).any(|x| x * x % p == a);
            assert_eq!(root.is_some(), exists);
            if let Some(x) = root {
                assert_eq!(x * x % p, a);
            }
        }
    }
    let p = [1, 2, 0, 4, 3];
    let q = number::pm_inverse(&p).unwrap();
    assert_eq!(number::pm_compose(&p, &q), Some(vec![0, 1, 2, 3, 4]));
}
