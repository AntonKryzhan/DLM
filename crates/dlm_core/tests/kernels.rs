//! v0.123 — sparse Krylov solvers (`la_pcg`, `la_bicgstab`, `la_gmres`),
//! exact geometric predicates, Delaunay triangulation and k-d trees
//! (`geo_*`, `kd_*`), 2-D FFT / STFT / wavelets / IIR / SHA-256 (`fft2_*`,
//! `sig_*`) and the libraries `std.signal`, `std.statistics`, `std.spatial`.
//! Interpreter and native C print identical lines; kernels are checked
//! against independent references.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use dlm_core::{geometry as geo, linalg, sparse, spectral};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-kernels-{}-{name}", std::process::id()));
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

const BUILTINS: &str = r#"    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn nvl(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    let rp = nvl(list(0, 2, 5, 8, 10))
    let ci = nvl(list(0, 1, 0, 1, 2, 1, 2, 3, 2, 3))
    let vs = fvl(list(4.0, -1.0, -1.0, 4.0, -1.0, -1.0, 4.0, -1.0, -1.0, 4.0))
    let b = fvl(list(1.0, 2.0, 3.0, 4.0))
    print_line(of(la_pcg(rp, ci, vs, b, fl_lit("1e-12"), 100)))
    print_line(of(la_bicgstab(rp, ci, vs, b, fl_lit("1e-12"), 100)))
    print_line(of(la_gmres(rp, ci, vs, b, fl_lit("1e-12"), 2, 100)))
    print_line(of(la_gmres(nvl(list(0, 2, 4)), nvl(list(0, 1, 0, 1)), fvl(list(1.0, 2.0, 3.0, 1.0)), fvl(list(5.0, 5.0)), fl_lit("1e-12"), 5, 50)))
    print_line(of(la_bicgstab(nvl(list(0, 2, 4)), nvl(list(0, 1, 0, 1)), fvl(list(1.0, 2.0, 3.0, 1.0)), fvl(list(5.0, 5.0)), fl_lit("1e-12"), 50)))
    print_line(of(la_pcg(nvl(list(0, 1)), nvl(list(0)), fvl(list(-1.0)), fvl(list(1.0)), fl_lit("1e-12"), 10)))
    print_line(on(geo_orient2d(fvl(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0)))))
    print_line(on(geo_orient2d(fvl(list(0.0, 0.0, 1.0, 1.0, 2.0, 2.0)))))
    print_line(on(geo_orient2d(fvl(list(0.5, 0.5, 12.0, 12.0, 24.0, 24.0000000000001)))))
    print_line(on(geo_orient2d(fvl(list(0.1, 0.1, 0.30000000000000004, 0.30000000000000004, 0.7, 0.7)))))
    print_line(on(geo_incircle(fvl(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0)))))
    print_line(on(geo_incircle(fvl(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.5, 0.5)))))
    print_line(on(geo_incircle(fvl(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 2.0, 2.0)))))
    print_line(on(geo_orient3d(fvl(list(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)))))
    print_line(on(geo_orient3d(fvl(list(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.3, 0.3, 0.0)))))
    print_line(on(geo_orient2d(fvl(list(1.0, 2.0)))))
    print_line(op(geo_delaunay(fvl(list(0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.5, 0.5)))))
    print_line(op(geo_delaunay(fvl(list(0.0, 0.0, 2.0, 0.0, 1.0, 3.0, 1.0, 1.0, 3.0, 2.0, 0.0, 0.0)))))
    print_line(op(geo_delaunay(fvl(list(0.0, 0.0, 1.0, 1.0, 2.0, 2.0)))))
    print_line(op(geo_delaunay(fvl(list(0.0, 0.0, 1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 1.5, 1.0)))))
    print_line(op(geo_delaunay(fvl(list(0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0)))))
    let pts = fvl(list(0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 5.0, 5.0, 2.0, 2.0, 1.0, 1.0))
    print_line(op(kd_knn(pts, fvl(list(0.9, 0.9, 4.0, 4.0)), 2, 3)))
    print_line(op(kd_radius(pts, 2, fvl(list(0.0, 0.0)), 1.0)))
    print_line(of(fft2_forward(fvl(list(1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0)), 2, 2)))
    print_line(of(fft2_inverse(unwrap_or(fft2_forward(fvl(list(1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0)), 2, 2), fv_empty()), 2, 2)))
    print_line(of(sig_stft(fvl(list(1.0, 0.0, -1.0, 0.0, 1.0, 0.0, -1.0, 0.0)), fvl(list(1.0, 1.0, 1.0, 1.0)), 2)))
    print_line(of(sig_dwt(fvl(list(4.0, 6.0, 10.0, 12.0, 8.0, 6.0, 5.0, 5.0)), 3, 0)))
    print_line(of(sig_idwt(unwrap_or(sig_dwt(fvl(list(4.0, 6.0, 10.0, 12.0, 8.0, 6.0, 5.0, 5.0)), 2, 1), fv_empty()), 2, 1)))
    print_line(of(sig_dwt(fvl(list(1.0, 2.0, 3.0)), 1, 0)))
    print_line(of(sig_iir(fvl(list(0.5)), fvl(list(1.0, -0.5)), fvl(list(1.0, 0.0, 0.0, 0.0)))))
    print_line(sig_sha256("abc"))
    print_line(sig_sha256(""))
"#;

#[test]
fn kernel_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.kernels\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &[
        "fvec[0.4880382775119617, 0.952153110047847, 1.3205741626794258, 1.3301435406698563]",
        "fvec[0.4880382775119617, 0.952153110047847, 1.3205741626794258, 1.3301435406698563]",
        "fvec[0.4880382775119617, 0.9521531100478469, 1.3205741626794258, 1.3301435406698567]",
        "fvec[0.4880382775119617, 0.9521531100478469, 1.3205741626794258, 1.3301435406698567]",
        "fvec[0.48803827751191886, 0.9521531100477152, 1.3205741626792507, 1.3301435406697095]",
        "fvec[0.48803827751191886, 0.9521531100477152, 1.3205741626792507, 1.3301435406697095]",
        "fvec[1.0000000000000002, 2.0000000000000004]",
        "fvec[1.0000000000000002, 2.0000000000000004]",
        "fvec[1.0000000000000002, 1.9999999999999996]",
        "fvec[1.0000000000000002, 1.9999999999999996]",
        "none",
        "1",
        "0",
        "1",
        "0",
        "0",
        "1",
        "2",
        "2",
        "0",
        "none",
        "nvec[0, 1, 4, 0, 4, 3, 1, 2, 4, 2, 3, 4]",
        "nvec[0, 1, 4, 0, 4, 3, 1, 2, 4, 2, 3, 4]",
        "nvec[0, 1, 3, 0, 3, 2, 1, 4, 3, 2, 3, 4]",
        "nvec[0, 1, 3, 0, 3, 2, 1, 4, 3, 2, 3, 4]",
        "nvec[]",
        "nvec[]",
        "nvec[0, 1, 4, 1, 2, 4, 2, 3, 4]",
        "nvec[0, 1, 4, 1, 2, 4, 2, 3, 4]",
        "nvec[0, 1, 3, 1, 2, 3]",
        "nvec[0, 1, 3, 1, 2, 3]",
        "nvec[5, 1, 2, 3, 4, 5]",
        "nvec[5, 1, 2, 3, 4, 5]",
        "nvec[0, 1, 2]",
        "nvec[0, 1, 2]",
        "fvec[10.0, 0.0, -2.0, 0.0, -4.0, 0.0, 0.0, 0.0]",
        "fvec[10.0, 0.0, -2.0, 0.0, -4.0, 0.0, 0.0, 0.0]",
        "fvec[1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0]",
        "fvec[1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0]",
        "fvec[0.0, 4.0, 0.0, 0.0, 4.0, 0.0, 0.0, 4.0, 0.0]",
        "fvec[0.0, 4.0, 0.0, 0.0, 4.0, 0.0, 0.0, 4.0, 0.0]",
        "fvec[19.798989873223338, 2.8284271247461916, -6.000000000000001, 2.0, -1.4142135623730951, -1.4142135623730951, 1.4142135623730951, 0.0]",
        "fvec[19.798989873223338, 2.8284271247461916, -6.000000000000001, 2.0, -1.4142135623730951, -1.4142135623730951, 1.4142135623730951, 0.0]",
        "fvec[4.0, 5.999999999999999, 10.000000000000002, 12.000000000000005, 8.000000000000002, 6.000000000000002, 5.000000000000002, 5.000000000000002]",
        "fvec[4.0, 5.999999999999999, 10.000000000000002, 12.000000000000005, 8.000000000000002, 6.000000000000002, 5.000000000000002, 5.000000000000002]",
        "none",
        "fvec[0.5, 0.25, 0.125, 0.0625]",
        "fvec[0.5, 0.25, 0.125, 0.0625]",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    ];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn t(x: Float) -> Text = fl_to_text(x)
    fn r6(x: Float) -> Text = fl_to_text(fl_div(fl_round(fl_mul(x, 1000000.0)), 1000000.0))
    fn o(x: Option<Float>) -> Text = match x { none => "none", some(y) => r6(y) }
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => join(map(fv_to_list(v), r6), " ") }
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn mt(x: Option<Matrix>) -> Text = match x { none => "none", some(m) => join(map(fv_to_list(field(m, data)), r6), " ") }
    fn gauss(v: FloatVec) -> Float = let a = unwrap_or(fv_get(v, 0), 0.0) in let b = unwrap_or(fv_get(v, 1), 0.0) in fl_mul(-0.5, fl_add(fl_mul(fl_sub(a, 1.0), fl_sub(a, 1.0)), fl_mul(fl_sub(b, 2.0), fl_sub(b, 2.0))))
    let lap = unwrap_or(laplacian_2d(4), Csr(0, 0, nv_empty(), nv_empty(), fv_empty()))
    let rhs = unwrap_or(fv_new(16, 1.0), fv_empty())
    print_line(o(match preconditioned_cg(lap, rhs, fl_lit("1e-12"), 200) { none => none(Float), some(x) => csr_residual(lap, x, rhs) }))
    print_line(o(match bicgstab(lap, rhs, fl_lit("1e-12"), 200) { none => none(Float), some(x) => csr_residual(lap, x, rhs) }))
    print_line(o(match gmres(lap, rhs, fl_lit("1e-12"), 8, 400) { none => none(Float), some(x) => csr_residual(lap, x, rhs) }))
    print_line(of(preconditioned_cg(lap, rhs, fl_lit("1e-12"), 200)))
    let eq = equilibrate(lap, rhs)
    print_line(of(some(field(fst(eq), values))))
    print_line(of(some(hann(5))))
    print_line(of(some(fir_lowpass(7, 0.1))))
    let lp = lowpass(1000.0, 48000.0, 0.7071)
    print_line(r6(gain(lp, 10.0, 48000.0)))
    print_line(r6(gain(lp, 1000.0, 48000.0)))
    print_line(r6(gain(lp, 20000.0, 48000.0)))
    print_line(of(some(filter(lp, fvl(list(1.0, 0.0, 0.0, 0.0, 0.0))))))
    print_line(of(spectrogram(fvl(list(0.0, 1.0, 0.0, -1.0, 0.0, 1.0, 0.0, -1.0, 0.0, 1.0)), 4, 2)))
    print_line(of(wavelet_denoise(fvl(list(1.0, 1.1, 0.9, 1.0, 5.0, 5.1, 4.9, 5.0)), 2, 0, 0.3)))
    print_line(of(spectrum_2d(fvl(list(1.0, 2.0, 3.0, 4.0)), 2, 2)))
    print_line(of(some(moving_average(fvl(list(3.0, 6.0, 9.0, 12.0)), 2))))
    print_line(digest("DLM"))
    let data = Matrix(5, 2, fvl(list(2.5, 2.4, 0.5, 0.7, 2.2, 2.9, 1.9, 2.2, 3.1, 3.0)))
    print_line(of(some(column_means(data))))
    print_line(mt(covariance_matrix(data)))
    print_line(mt(correlation_matrix(data)))
    print_line(match pca(data) { none => "none", some(p) => concat(of(some(field(p, values))), concat(" | ", of(some(explained_variance(p))))) })
    print_line(match mvn_samples(3, 2000, fvl(list(1.0, -1.0)), Matrix(2, 2, fvl(list(1.0, 0.5, 0.5, 2.0)))) { none => "none", some(s) => mt(covariance_matrix(s)) })
    print_line(o(mahalanobis2(fvl(list(1.0, 1.0)), fvl(list(0.0, 0.0)), Matrix(2, 2, fvl(list(2.0, 0.0, 0.0, 0.5))))))
    let bp = beta_update(BetaPosterior(1.0, 1.0), 7, 3)
    print_line(concat(r6(beta_mean(bp)), concat(" ", concat(r6(beta_variance(bp)), concat(" ", r6(beta_cdf(bp, 0.5)))))))
    let np = normal_update(NormalPosterior(0.0, 100.0), fvl(list(4.8, 5.2, 5.0, 4.9, 5.1)), 0.25)
    print_line(concat(r6(field(np, mean)), concat(" ", r6(field(np, variance)))))
    print_line(match credible_interval(np, 0.95) { none => "none", some(ci) => concat(r6(fst(ci)), concat(" ", r6(snd(ci)))) })
    let ch = metropolis(gauss, fvl(list(0.0, 0.0)), 1.0, 3000, 300, 11)
    print_line(of(some(chain_mean(ch))))
    print_line(r6(acceptance_rate(ch)))
    let ar = fvl(list(1.0, 0.8, 0.3, -0.2, -0.5, -0.3, 0.1, 0.4, 0.5, 0.2, -0.1, -0.4, -0.3, 0.0, 0.3))
    print_line(of(some(acf(ar, 3))))
    print_line(match ar_fit(ar, 2) { none => "none", some(m) => concat(of(some(field(m, coefficients))), concat(" ", concat(r6(field(m, noise)), concat(" | ", of(some(ar_forecast(m, ar, 3))))))) })
    print_line(of(some(difference(fvl(list(1.0, 4.0, 9.0, 16.0))))))
    print_line(of(some(exponential_smoothing(fvl(list(1.0, 1.0, 1.0, 1.0)), 0.5))))
    let lb = ljung_box(ar, 3)
    print_line(concat(r6(field(lb, statistic)), concat(" ", r6(field(lb, p_value)))))
    let ps = list(v2(0.0, 0.0), v2(1.0, 0.0), v2(2.0, 0.0), v2(0.0, 1.0), v2(1.0, 1.0), v2(2.0, 1.0), v2(0.5, 2.0), v2(1.5, 2.0))
    let m = unwrap_or(delaunay_mesh(ps), Mesh(fv_empty(), nv_empty()))
    print_line(print_symbolic(field(m, triangles)))
    print_line(nat_to_text(triangle_count(m)))
    print_line(r6(mesh_area(m)))
    print_line(nat_to_text(mesh_edge_count(m)))
    print_line(nat_to_text(boundary_edge_count(m)))
    print_line(int_to_text(mesh_euler(m)))
    print_line(if_then_else(is_delaunay(m), "delaunay", "not delaunay"))
    print_line(match exact_orientation(v2(0.1, 0.1), v2(0.30000000000000004, 0.30000000000000004), v2(0.7, 0.7)) { none => "none", some(s) => nat_to_text(s) })
    print_line(print_symbolic(unwrap_or(nearest(ps, v2(1.1, 0.9), 3), nv_empty())))
    print_line(print_symbolic(unwrap_or(within(ps, v2(1.0, 1.0), 1.0), nv_empty())))
"#;

#[test]
fn signal_statistics_spatial_agree_between_interpreter_and_native() {
    let out = run_both("library", &format!("module test.kernels_lib\nimport std.spatial\ntheory App extends StdSpatial {{\n{LIBRARY}}}\n"));
    assert_eq!(out.len(), 45, "{out:?}");
    // lines that depend only on correctly rounded arithmetic are compared exactly; the rest (libm) are parity-checked above
    let want: &[&str] = &[
        "0.0",
        "0.0",
        "0.0",
        "0.833333 1.166667 1.166667 0.833333 1.166667 1.666667 1.666667 1.166667 1.166667 1.666667 1.666667 1.166667 0.833333 1.166667 1.166667 0.833333",
        "1.0 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 1.0 -0.25 -0.25 -0.25 1.0",
        "0.0 0.5 1.0 0.5 0.0",
        "0.013497 0.078451 0.240862 0.334379 0.240862 0.078451 0.013497",
        "1.0",
        "0.7071",
        "0.000308",
        "0.003916 0.014941 0.027785 0.038024 0.045936",
        "0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625 0.5625",
        "1.0 1.0 1.0 1.0 5.0 5.0 5.0 5.0",
        "100.0 4.0 16.0 0.0",
        "1.5 4.5 7.5 10.5",
        "d40c2dcb29f13e786bdfa460c787a9593b6e054a0c944d4612bcb62fad2b3749",
        "2.04 2.24",
        "0.938 0.8405 0.8405 0.853",
        "1.0 0.939641 0.939641 1.0",
        "1.737074 0.053926 | 0.96989 0.03011",
        "0.984925 0.470085 0.470085 1.953916",
        "2.5",
        "0.666667 0.017094 0.113281",
        "4.997501 0.049975",
        "4.55935 5.435653",
        "0.960418 2.088274",
        "0.556667",
        "1.0 0.593787 -0.063905 -0.542604",
        "0.975775 -0.643308 0.068409 | 0.372836 0.250916 0.085093",
        "3.0 5.0 7.0",
        "0.5 0.75 0.875 0.9375",
        "12.75855 0.005189",
        "nvec[0, 1, 3, 1, 2, 4, 1, 4, 3, 2, 5, 4, 3, 4, 6, 4, 5, 7, 4, 7, 6]",
        "nvec[0, 1, 3, 1, 2, 4, 1, 4, 3, 2, 5, 4, 3, 4, 6, 4, 5, 7, 4, 7, 6]",
        "7",
        "3.5",
        "14",
        "7",
        "1",
        "delaunay",
        "0",
        "nvec[4, 5, 1]",
        "nvec[4, 5, 1]",
        "nvec[1, 3, 4, 5]",
        "nvec[1, 3, 4, 5]",
    ];
    for i in [
        0usize, 1, 2, 3, 4, 12, 14, 15, 16, 17, 18, 19, 21, 23, 27, 28, 29, 30, 32, 33, 34, 35, 36,
        37, 38, 39, 40, 41, 42, 43, 44,
    ] {
        if i < want.len() {
            assert_eq!(out[i], want[i], "#{i}");
        }
    }
}

fn lcg(s: &mut u64) -> f64 {
    *s = s
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    ((*s >> 11) as f64) / 9007199254740992.0
}

#[test]
fn exact_predicates_and_delaunay_are_robust() {
    // near-degenerate orientation: exact sign versus naive float evaluation
    let p = [0.5, 0.5, 12.0, 12.0, 24.0, 24.000000000000004];
    assert_eq!(geo::orient2d(&p), Some(1));
    // (k, k) points are exactly collinear whatever their rounding
    assert_eq!(
        geo::orient2d(&[0.1, 0.1, 0.2, 0.2, 0.30000000000000004, 0.30000000000000004]),
        Some(0)
    );
    // grid points (many cocircular quadruples) and random points: every triangle is ccw and has an empty circumcircle
    let mut s = 3u64;
    for case in 0..3 {
        let pts: Vec<f64> = if case == 0 {
            (0..36)
                .flat_map(|i| [(i % 6) as f64, (i / 6) as f64])
                .collect()
        } else {
            (0..80 * case)
                .flat_map(|_| [lcg(&mut s) * 10.0, lcg(&mut s) * 10.0])
                .collect()
        };
        let tri = geo::delaunay(&pts).unwrap();
        let n = pts.len() / 2;
        let at = |i: u128| (pts[2 * i as usize], pts[2 * i as usize + 1]);
        let mut area = 0.0;
        for t in tri.chunks(3) {
            let (a, b, c) = (at(t[0]), at(t[1]), at(t[2]));
            assert_eq!(geo::orient2d(&[a.0, a.1, b.0, b.1, c.0, c.1]), Some(1));
            area += ((b.0 - a.0) * (c.1 - a.1) - (b.1 - a.1) * (c.0 - a.0)) / 2.0;
            for v in 0..n as u128 {
                let d = at(v);
                assert_ne!(
                    geo::incircle(&[a.0, a.1, b.0, b.1, c.0, c.1, d.0, d.1]),
                    Some(1),
                    "case {case}"
                );
            }
        }
        if case == 0 {
            assert!((area - 25.0).abs() < 1e-9 && tri.len() / 3 == 50);
        }
    }
    assert!(geo::orient2d(&[1e300, 0.0, 0.0, 0.0, 0.0, 1.0]).is_none());
}

#[test]
fn knn_matches_brute_force() {
    let mut s = 9u64;
    let pts: Vec<f64> = (0..600).map(|_| (lcg(&mut s) * 8.0).floor()).collect();
    let qs: Vec<f64> = (0..60).map(|_| lcg(&mut s) * 8.0).collect();
    let got = geo::knn(&pts, &qs, 3, 5).unwrap();
    for (qi, q) in qs.chunks(3).enumerate() {
        let mut all: Vec<(f64, usize)> = pts
            .chunks(3)
            .enumerate()
            .map(|(i, p)| {
                let mut d = 0.0;
                for k in 0..3 {
                    let t = p[k] - q[k];
                    d += t * t;
                }
                (d, i)
            })
            .collect();
        all.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap().then(a.1.cmp(&b.1)));
        let want: Vec<u128> = all[..5].iter().map(|e| e.1 as u128).collect();
        assert_eq!(&got[qi * 5..qi * 5 + 5], want.as_slice());
        let r = geo::radius(&pts, 3, q, 2.0).unwrap();
        let want_r: Vec<u128> = all
            .iter()
            .filter(|e| e.0 <= 4.0)
            .map(|e| e.1 as u128)
            .collect::<std::collections::BTreeSet<_>>()
            .into_iter()
            .collect();
        assert_eq!(r, want_r);
    }
}

#[test]
fn krylov_solvers_and_signal_kernels() {
    // nonsymmetric convection–diffusion matrix: BiCGSTAB and GMRES reach a small residual
    let n = 50usize;
    let (mut rp, mut ci, mut vs) = (vec![0u128], Vec::new(), Vec::new());
    for i in 0..n {
        if i > 0 {
            ci.push(i as u128 - 1);
            vs.push(-1.3);
        }
        ci.push(i as u128);
        vs.push(3.0);
        if i + 1 < n {
            ci.push(i as u128 + 1);
            vs.push(-0.7);
        }
        rp.push(ci.len() as u128);
    }
    let b: Vec<f64> = (0..n).map(|i| (i % 7) as f64).collect();
    for x in [
        sparse::bicgstab(&rp, &ci, &vs, &b, 1e-10, 500).unwrap(),
        sparse::gmres(&rp, &ci, &vs, &b, 1e-10, 10, 1000).unwrap(),
    ] {
        let ax = linalg::csr_matvec(&rp, &ci, &vs, &x).unwrap();
        let res: f64 = ax
            .iter()
            .zip(&b)
            .map(|(p, q)| (p - q) * (p - q))
            .sum::<f64>()
            .sqrt();
        assert!(res < 1e-9, "{res}");
    }
    // wavelets are orthogonal and invertible
    let mut s = 1u64;
    let x: Vec<f64> = (0..64).map(|_| lcg(&mut s)).collect();
    for kind in 0..2u128 {
        let c = spectral::dwt(&x, 3, kind).unwrap();
        let e1: f64 = x.iter().map(|v| v * v).sum();
        let e2: f64 = c.iter().map(|v| v * v).sum();
        assert!((e1 - e2).abs() < 1e-12 * e1);
        let back = spectral::idwt(&c, 3, kind).unwrap();
        assert!(back.iter().zip(&x).all(|(a, b)| (a - b).abs() < 1e-12));
    }
    assert!(spectral::dwt(&x[..6], 2, 0).is_none());
    // 2-D FFT round trip and STFT of a pure tone
    let img: Vec<f64> = (0..2 * 8 * 4).map(|_| lcg(&mut s)).collect();
    let back = spectral::fft2(&spectral::fft2(&img, 8, 4, false).unwrap(), 8, 4, true).unwrap();
    assert!(back.iter().zip(&img).all(|(a, b)| (a - b).abs() < 1e-12));
    let tone: Vec<f64> = (0..64)
        .map(|t| (std::f64::consts::TAU * 4.0 * t as f64 / 16.0).cos())
        .collect();
    let spec = spectral::stft(&tone, &[1.0; 16], 16).unwrap();
    assert_eq!(spec.len(), 4 * 9);
    for f in 0..4 {
        let row = &spec[f * 9..f * 9 + 9];
        let peak = (0..9)
            .max_by(|a, b| row[*a].partial_cmp(&row[*b]).unwrap())
            .unwrap();
        assert_eq!(peak, 4);
    }
    assert_eq!(
        spectral::sha256(b"The quick brown fox jumps over the lazy dog"),
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"
    );
}
