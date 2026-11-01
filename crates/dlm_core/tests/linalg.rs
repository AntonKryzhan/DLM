//! v0.116 — numerical linear algebra (`la_*`, `std.linalg`): LU, inverse,
//! Cholesky, QR, least squares, symmetric eigenproblems, singular values,
//! CSR products and conjugate gradients. Interpreter and native C agree on
//! every printed line; results are checked against independent identities.
use dlm_core::linalg as la;
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
    let dir = std::env::temp_dir().join(format!("dlm-linalg-{}-{name}", std::process::id()));
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

const BUILTINS: &str = r#"    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(v) => fl_to_text(v) }
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    let a = fv(list(2.0, 1.0, 1.0, 1.0, 3.0, 2.0, 1.0, 0.0, 0.0))
    let s = fv(list(4.0, 1.0, 2.0, 1.0, 3.0, 0.0, 2.0, 0.0, 5.0))
    let r = fv(list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0, 11.0, 12.0, 13.0))
    print_line(of(la_lu(a, 3)))
    print_line(op(la_lu_perm(a, 3)))
    print_line(o(la_det(a, 3)))
    print_line(o(la_det(fv(list(1.0, 2.0, 2.0, 4.0)), 2)))
    print_line(of(la_inverse(a, 3)))
    print_line(of(la_inverse(fv(list(1.0, 2.0, 2.0, 4.0)), 2)))
    print_line(of(la_cholesky(s, 3)))
    print_line(of(la_cholesky(a, 3)))
    print_line(of(la_qr_q(r, 4, 3)))
    print_line(of(la_qr_r(r, 4, 3)))
    print_line(of(la_lstsq(fv(list(1.0, 1.0, 1.0, 2.0, 1.0, 3.0, 1.0, 4.0)), fv(list(6.0, 5.0, 7.0, 10.0)), 4, 2)))
    print_line(of(la_eigh_values(s, 3)))
    print_line(of(la_eigh_vectors(s, 3)))
    print_line(of(la_eigh_values(a, 3)))
    print_line(of(la_svd_values(r, 4, 3)))
    print_line(on(la_rank(r, 4, 3, fl_lit("1e-9"))))
    print_line(on(la_rank(fv(list(1.0, 2.0, 2.0, 4.0)), 2, 2, fl_lit("1e-9"))))
    print_line(of(la_identity(2)))
    print_line(o(la_trace(s, 3)))
    print_line(of(la_csr_matvec(nv(list(0, 2, 3, 5)), nv(list(0, 1, 1, 0, 2)), fv(list(4.0, 1.0, 3.0, 1.0, 2.0)), fv(list(1.0, 2.0, 3.0)))))
    print_line(of(la_cg(nv(list(0, 2, 5, 7)), nv(list(0, 1, 0, 1, 2, 1, 2)), fv(list(4.0, 1.0, 1.0, 3.0, 1.0, 1.0, 2.0)), fv(list(1.0, 2.0, 3.0)), fl_lit("1e-12"), 50)))
    print_line(of(la_cg(nv(list(0, 1)), nv(list(0)), fv(list(-1.0)), fv(list(1.0)), fl_lit("1e-12"), 50)))
    print_line(of(la_qr_q(fv(list(1.0, 2.0)), 1, 2)))
"#;

#[test]
fn linalg_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.linalg\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &["fvec[2.0, 1.0, 1.0, 0.5, 2.5, 1.5, 0.5, -0.2, -0.19999999999999996]", "fvec[2.0, 1.0, 1.0, 0.5, 2.5, 1.5, 0.5, -0.2, -0.19999999999999996]", "nvec[0, 1, 2]", "nvec[0, 1, 2]", "-0.9999999999999998", "0.0", "fvec[0.0, 0.0, 1.0, -2.0000000000000004, 1.0000000000000002, 3.000000000000001, 3.0000000000000004, -1.0000000000000002, -5.000000000000001]", "fvec[0.0, 0.0, 1.0, -2.0000000000000004, 1.0000000000000002, 3.000000000000001, 3.0000000000000004, -1.0000000000000002, -5.000000000000001]", "none", "fvec[2.0, 0.0, 0.0, 0.5, 1.6583123951777, 0.0, 1.0, -0.30151134457776363, 1.9771421064483223]", "fvec[2.0, 0.0, 0.0, 0.5, 1.6583123951777, 0.0, 1.0, -0.30151134457776363, 1.9771421064483223]", "none", "fvec[-0.07312724241271318, -0.8104029016718266, 0.16665221600841218, -0.29250896965085227, -0.4694407052367293, 0.2472903850447406, -0.5118906968889915, -0.12847850880163125, -0.8493887138493255, -0.8043996665398437, 0.32613775311183313, 0.43544611279617307]", "fvec[-0.07312724241271318, -0.8104029016718266, 0.16665221600841218, -0.29250896965085227, -0.4694407052367293, 0.2472903850447406, -0.5118906968889915, -0.12847850880163125, -0.8493887138493255, -0.8043996665398437, 0.32613775311183313, 0.43544611279617307]", "fvec[-13.674794331177344, -15.356720906669743, -17.550538179051134, 0.0, -1.0821843625983536, -2.292847233998341, 0.0, 0.0, -0.8493887138493247]", "fvec[-13.674794331177344, -15.356720906669743, -17.550538179051134, 0.0, -1.0821843625983536, -2.292847233998341, 0.0, 0.0, -0.8493887138493247]", "fvec[3.499999999999999, 1.4000000000000004]", "fvec[3.499999999999999, 1.4000000000000004]", "fvec[1.854897308799577, 3.476023602918134, 6.6690790882822855]", "fvec[1.854897308799577, 3.476023602918134, 6.6690790882822855]", "fvec[-0.6793130619863367, 0.37436195478307144, 0.6311789687764832, 0.5932333119173845, 0.7864356987513789, 0.1720265367929081, 0.43198148275855314, -0.49129626351156835, 0.7563200248659911]", "fvec[-0.6793130619863367, 0.37436195478307144, 0.6311789687764832, 0.5932333119173845, 0.7864356987513789, 0.1720265367929081, 0.43198148275855314, -0.49129626351156835, 0.7563200248659911]", "none", "fvec[27.121812433594304, 1.5213437095413933, 0.30463688937580935]", "fvec[27.121812433594304, 1.5213437095413933, 0.30463688937580935]", "3", "1", "fvec[1.0, 0.0, 0.0, 1.0]", "fvec[1.0, 0.0, 0.0, 1.0]", "12.0", "fvec[6.0, 6.0, 7.0]", "fvec[6.0, 6.0, 7.0]", "fvec[0.22222222222222224, 0.11111111111111119, 1.4444444444444444]", "fvec[0.22222222222222224, 0.11111111111111119, 1.4444444444444444]", "none", "none"];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => t(y) }
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(y) => nat_to_text(y) }
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn om(x: Option<Matrix>) -> Text = match x { none => "none", some(m) => print_symbolic(field(m, data)) }
    fn mk(r: Nat, c: Nat, xs: List<Float>) -> Matrix = unwrap_or(matrix(r, c, xs), Matrix(0, 0, fv_empty()))
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    let a = mk(2, 2, list(4.0, 7.0, 2.0, 6.0))
    let s = mk(3, 3, list(4.0, 1.0, 2.0, 1.0, 3.0, 0.0, 2.0, 0.0, 5.0))
    print_line(o(det(a)))
    print_line(om(inverse(a)))
    print_line(o(max_abs_diff(unwrap_or(matmul(a, unwrap_or(inverse(a), a)), a), unwrap_or(identity(2), a))))
    print_line(of(solve(a, fvl(list(1.0, 2.0)))))
    print_line(om(cholesky(s)))
    print_line(match eigh(s) { none => "none", some(e) => print_symbolic(field(e, values)) })
    print_line(o(dominant_eigenvalue(s, 200)))
    print_line(o(condition(a)))
    print_line(o(spectral_norm(a)))
    print_line(on(rank(mk(3, 3, list(1.0, 2.0, 3.0, 2.0, 4.0, 6.0, 1.0, 0.0, 1.0)), fl_lit("1e-9"))))
    print_line(match qr(mk(3, 2, list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0))) { none => "none", some(f) => o(max_abs_diff(unwrap_or(matmul(field(f, q), field(f, r)), a), mk(3, 2, list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0)))) })
    print_line(of(least_squares(mk(4, 2, list(1.0, 1.0, 1.0, 2.0, 1.0, 3.0, 1.0, 4.0)), fvl(list(6.0, 5.0, 7.0, 10.0)))))
    print_line(om(matrix_power(mk(2, 2, list(1.0, 1.0, 1.0, 0.0)), 10)))
    print_line(om(some(transpose(mk(2, 3, list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0))))))
    print_line(of(col(mk(2, 3, list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0)), 1)))
    print_line(match lu(a) { none => "none", some(f) => print_symbolic(field(f, perm)) })
    let lap = unwrap_or(laplacian_1d(5), Csr(0, 0, nv_empty(), nv_empty(), fv_empty()))
    print_line(nat_to_text(csr_nnz(lap)))
    print_line(of(csr_matvec(lap, fvl(list(1.0, 2.0, 3.0, 4.0, 5.0)))))
    print_line(of(conjugate_gradient(lap, fvl(list(1.0, 0.0, 0.0, 0.0, 1.0)), fl_lit("1e-12"), 100)))
    print_line(if_then_else(is_none(matrix(2, 2, list(1.0))), "shape", "?"))
"#;

#[test]
fn std_linalg_agrees_between_interpreter_and_native() {
    let out = run_both("library", &format!("module test.linalg_lib\nimport std.linalg\ntheory App extends StdLinalg {{\n{LIBRARY}}}\n"));
    let want: &[&str] = &["10.0", "fvec[0.6000000000000001, -0.7000000000000001, -0.2, 0.4]", "fvec[0.6000000000000001, -0.7000000000000001, -0.2, 0.4]", "2.220446049250313e-16", "fvec[-0.8, 0.6]", "fvec[-0.8, 0.6]", "fvec[2.0, 0.0, 0.0, 0.5, 1.6583123951777, 0.0, 1.0, -0.30151134457776363, 1.9771421064483223]", "fvec[2.0, 0.0, 0.0, 0.5, 1.6583123951777, 0.0, 1.0, -0.30151134457776363, 1.9771421064483223]", "fvec[1.854897308799577, 3.476023602918134, 6.6690790882822855]", "fvec[1.854897308799577, 3.476023602918134, 6.6690790882822855]", "6.669079088282286", "10.403882032022077", "10.199942172395918", "2", "2.6645352591003757e-15", "fvec[3.499999999999999, 1.4000000000000004]", "fvec[3.499999999999999, 1.4000000000000004]", "fvec[89.0, 55.0, 55.0, 34.0]", "fvec[89.0, 55.0, 55.0, 34.0]", "fvec[1.0, 4.0, 2.0, 5.0, 3.0, 6.0]", "fvec[1.0, 4.0, 2.0, 5.0, 3.0, 6.0]", "fvec[2.0, 5.0]", "fvec[2.0, 5.0]", "nvec[0, 1]", "nvec[0, 1]", "13", "fvec[0.0, 0.0, 0.0, 0.0, 6.0]", "fvec[0.0, 0.0, 0.0, 0.0, 6.0]", "fvec[1.0, 1.0, 1.0, 1.0, 1.0]", "fvec[1.0, 1.0, 1.0, 1.0, 1.0]", "shape"];
    assert_eq!(out, want);
}

fn matmul(a: &[f64], b: &[f64], n: usize, m: usize, k: usize) -> Vec<f64> {
    let mut c = vec![0.0; n * k];
    for i in 0..n {
        for j in 0..k {
            c[i * k + j] = (0..m).map(|l| a[i * m + l] * b[l * k + j]).sum();
        }
    }
    c
}

fn close(a: &[f64], b: &[f64], tol: f64) -> bool {
    a.len() == b.len() && a.iter().zip(b).all(|(x, y)| (x - y).abs() <= tol)
}

#[test]
fn factorizations_satisfy_their_identities() {
    // a deterministic pseudo-random 6×6 matrix and an SPD matrix built from it
    let mut s = 12345u64;
    let a: Vec<f64> = (0..36)
        .map(|_| {
            s = s
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (s >> 11) as f64 / 9007199254740992.0 - 0.5
        })
        .collect();
    let mut at = vec![0.0; 36];
    for i in 0..6 {
        for j in 0..6 {
            at[j * 6 + i] = a[i * 6 + j];
        }
    }
    let mut spd = matmul(&a, &at, 6, 6, 6);
    for i in 0..6 {
        spd[i * 6 + i] += 1.0;
    }
    let inv = la::inverse(&a, 6).unwrap();
    let id = la::identity(6).unwrap();
    assert!(close(&matmul(&a, &inv, 6, 6, 6), &id, 1e-10));
    let l = la::cholesky(&spd, 6).unwrap();
    let mut lt = vec![0.0; 36];
    for i in 0..6 {
        for j in 0..6 {
            lt[j * 6 + i] = l[i * 6 + j];
        }
    }
    assert!(close(&matmul(&l, &lt, 6, 6, 6), &spd, 1e-12));
    let (q, r) = la::qr(&a, 6, 6).unwrap();
    assert!(close(&matmul(&q, &r, 6, 6, 6), &a, 1e-13));
    let (values, vectors) = la::eigh(&spd, 6).unwrap();
    for j in 0..6 {
        let v: Vec<f64> = (0..6).map(|k| vectors[k * 6 + j]).collect();
        let av = matmul(&spd, &v, 6, 6, 1);
        assert!(close(
            &av,
            &v.iter().map(|x| x * values[j]).collect::<Vec<_>>(),
            1e-12
        ));
    }
    assert!(values.windows(2).all(|w| w[0] <= w[1]));
    // singular values of A are the square roots of the eigenvalues of A·Aᵀ
    let sv = la::svd_values(&a, 6, 6).unwrap();
    let (ev, _) = la::eigh(&matmul(&a, &at, 6, 6, 6), 6).unwrap();
    let mut roots: Vec<f64> = ev.iter().map(|x| x.max(0.0).sqrt()).collect();
    roots.reverse();
    assert!(close(&sv, &roots, 1e-12));
    let d = la::det(&a, 6).unwrap();
    let prod: f64 = sv.iter().product();
    assert!((d.abs() - prod).abs() <= 1e-12);
    assert_eq!(la::det(&[1.0, 2.0, 2.0, 4.0], 2), Some(0.0));
    assert!(la::inverse(&[1.0, 2.0, 2.0, 4.0], 2).is_none());
    assert!(la::cholesky(&[1.0, 2.0, 2.0, 1.0], 2).is_none());
    assert!(la::eigh(&[1.0, 2.0, 3.0, 1.0], 2).is_none());
    assert!(la::qr(&[1.0, 2.0], 1, 2).is_none());
    assert_eq!(la::rank(&[1.0, 2.0, 2.0, 4.0], 2, 2, 1e-9), Some(1));
}
