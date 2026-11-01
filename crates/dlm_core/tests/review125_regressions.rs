//! Independent boundary regressions for the full 0.125 code review.
use dlm_core::{build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, CheckPolicy, Checker, Runtime};
use dlm_core::{geometry, sparse, spectral};
use std::path::Path;
use std::process::Command;

fn run_both(name: &str, body: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-review125-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let work = dir.join("data");
    let source = format!("module review125\nimport std.files\ntheory T extends StdFiles {{\n{body}\n}}\n").replace("{D}", work.to_str().unwrap());
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let p = load_program_with_paths(&path, &[root.join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let setup = || {
        std::fs::create_dir_all(&work).unwrap();
        std::fs::write(work.join("item"), "DATA").unwrap();
        std::fs::write(work.join("item.dlm-tmp"), "KEEP").unwrap();
    };
    setup();
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join("main"), &default_c_compiler()).unwrap();
    setup();
    let output = Command::new(exe).output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    let native: Vec<_> = String::from_utf8(output.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn sparse_solvers_reject_invalid_tolerances_and_false_convergence() {
    let (rp, ix, a) = (&[0, 1][..], &[0][..], &[1.0][..]);
    for tol in [-1.0, f64::NAN, f64::INFINITY] {
        assert!(sparse::pcg(rp, ix, a, &[1.0], tol, 2).is_none());
        assert!(sparse::bicgstab(rp, ix, a, &[1.0], tol, 2).is_none());
        assert!(sparse::gmres(rp, ix, a, &[1.0], tol, 2, 2).is_none());
    }
    for b in [1e308, 1e-200] {
        for x in [dlm_core::linalg::cg(rp, ix, a, &[b], 0.0, 2), sparse::pcg(rp, ix, a, &[b], 0.0, 2), sparse::bicgstab(rp, ix, a, &[b], 0.0, 2), sparse::gmres(rp, ix, a, &[b], 0.0, 2, 2)] {
            if let Some(x) = x { assert_eq!(x, vec![b]); }
        }
    }
    assert!(sparse::pcg(&[0, 2], &[0, 0], &[1e308, 1e308], &[1.0], 0.0, 2).is_none());
    let n = 20000;
    assert!(sparse::gmres(&vec![0; n + 1], &[], &[], &vec![1.0; n], 1e-10, 1024, 1).is_none());
}

#[test]
fn empty_fft_shapes_are_bounded_without_allocating_or_iterating() {
    let max = dlm_core::float::VEC_MAX as u128;
    for inverse in [false, true] {
        assert_eq!(spectral::fft2(&[], 0, max, inverse), Some(vec![]));
        assert_eq!(spectral::fft2(&[], max, 0, inverse), Some(vec![]));
        assert_eq!(spectral::fft2(&[], 0, max + 1, inverse), None);
        assert_eq!(spectral::fft2(&[], usize::MAX as u128, 0, inverse), None);
        assert_eq!(spectral::fft2(&[], 0, u128::MAX, inverse), None);
    }
}

#[test]
fn spatial_distances_do_not_collapse_at_float_extremes() {
    for s in [1e200, 1e-200, f64::from_bits(1)] {
        let points = [2.0 * s, s];
        assert_eq!(geometry::knn(&points, &[0.0], 1, 1), Some(vec![1]));
        assert_eq!(geometry::radius(&points, 1, &[0.0], s), Some(vec![1]));
    }
    assert_eq!(geometry::knn(&[-1e308, 0.0], &[1e308], 1, 1), Some(vec![1]));
    assert_eq!(geometry::radius(&[-1e308, 0.0], 1, &[1e308], 1e308), Some(vec![1]));
}

#[test]
fn native_numerical_boundaries_match_interpreter() {
    let out = run_both("numeric", r#"
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn show(b: Bool) -> Text = if_then_else(b, "true", "false")
    print_line(show(is_none(la_pcg(nv(list(0,1)), nv(list(0)), fv(list(1.0)), fv(list(fl_lit("1e308"))), 0.0, 2))))
    print_line(show(is_none(la_bicgstab(nv(list(0,1)), nv(list(0)), fv(list(1.0)), fv(list(fl_lit("1e308"))), 0.0, 2))))
    print_line(show(is_none(la_gmres(nv(list(0,1)), nv(list(0)), fv(list(1.0)), fv(list(1.0)), fl_lit("inf"), 2, 2))))
    print_line(show(is_some(fft2_forward(fv_empty(), 16777216, 0))))
    print_line(show(is_none(fft2_forward(fv_empty(), 18446744073709551615, 0))))
    print_line(show(equal(unwrap_or(kd_knn(fv(list(fl_lit("2e200"),fl_lit("1e200"))), fv(list(0.0)), 1, 1), nv_empty()), nv(list(1)))))
    print_line(show(equal(unwrap_or(kd_radius(fv(list(fl_lit("2e-200"),fl_lit("1e-200"))), 1, fv(list(0.0)), fl_lit("1e-200")), nv_empty()), nv(list(1)))))
    print_line(show(equal(unwrap_or(kd_knn(fv(list(fl_lit("-1e308"),0.0)), fv(list(fl_lit("1e308"))), 1, 1), nv_empty()), nv(list(1)))))
"#);
    assert_eq!(out, vec!["true"; 8]);
}

#[test]
fn file_copy_and_atomic_write_preserve_other_files() {
    let out = run_both("files", r#"
    fn number(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn text(r: Result<Text, Text>) -> Text = match r { ok(t) => t, err(e) => e }
    print_line(number(file_copy("{D}/item", "{D}/item")))
    print_line(text(read_text("{D}/item")))
    print_line(number(file_write_atomic("{D}/item", "NEXT")))
    print_line(text(read_text("{D}/item")))
    print_line(text(read_text("{D}/item.dlm-tmp")))
"#);
    assert_eq!(out, ["4", "DATA", "4", "NEXT", "KEEP"]);
}

#[test]
fn enums_cannot_forge_invalid_values_or_ignore_payload_arity() {
    for body in [
        "enum E { A, B }\n let bad = E(none(Bool), none(Bool))",
        "enum E { A, B }\n let bad = E(some(true), some(false))",
        "enum E { A, B }\n let bad = E(some(false), none(Bool))",
        "enum E { E, A }",
        "enum E { A(x: Nat, y: Nat), B }\n fn f(e: E) -> Nat = match e { A(x) => x, _ => 0 }",
        "enum E { A(x: Nat, y: Nat), B }\n fn f(e: E) -> Nat = match e { A(_,_,_) => 1, _ => 0 }",
        "enum E { A(x: Nat, y: Nat), B }\n fn f(e: E) -> Nat = match e { A(x,x) => x, B => 0 }",
        "record R(v_A: Option<Nat>)\n fn f(r: R) -> Nat = match r { A(_) => 1, _ => 0 }",
    ] {
        let source = format!("module bad\ntheory T {{\n{body}\n}}\n");
        if let Ok(module) = dlm_core::parse_module(&source) {
            let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&dlm_core::Program::single(module));
            assert!(!report.ok(), "accepted: {body}");
        }
    }
    let out = run_both("enums", r#"
    enum E { A(x: Nat, y: Nat), B }
    fn sum(e: E) -> Nat = match e { A(x,y) => x + y, B => 0 }
    print_line(nat_to_text(sum(A(2,3))))
    print_line(nat_to_text(sum(B())))
"#);
    assert_eq!(out, ["5", "0"]);
}

#[test]
fn solid_angles_are_invariant_under_extreme_scaling() {
    let triangle = [1.0,0.0,0.0, 0.0,1.0,0.0, 0.0,0.0,1.0];
    let expected = 0.125; // an octant of the sphere: (pi / 2) / (4 pi).
    for s in [1.0, 1e200, 1e-200] {
        let t: Vec<_> = triangle.iter().map(|v| v * s).collect();
        let w = dlm_core::bvh::winding(&t, &[0.0,0.0,0.0]).unwrap();
        assert!((w[0] - expected).abs() < 1e-15, "{s}: {w:?}");
    }
}

#[test]
fn mcmc_consumes_independent_random_draws_for_every_dimension() {
    let body = r#"
    fn flat(x: FloatVec) -> Float = 0.0
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    print_line(nat_to_text(field(metropolis(flat, fv(list(0.0)), 1.0, 2, 1, 7), state)))
    print_line(nat_to_text(field(metropolis(flat, fv(list(0.0,0.0,0.0)), 1.0, 2, 1, 7), state)))
    print_line(fl_to_text(unwrap_or(fv_get(unwrap_or(bvh_winding(fv(list(fl_lit("1e200"),0.0,0.0,0.0,fl_lit("1e200"),0.0,0.0,0.0,fl_lit("1e200"))), fv(list(0.0,0.0,0.0))), fv_empty()), 0), 0.0)))
"#;
    // std.files does not extend the numerical theories, so qualify the import
    // through a helper module assembled separately below.
    let dir = std::env::temp_dir().join(format!("dlm-review125-mcmc-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, format!("module mcmc\nimport std.statistics\ntheory T extends StdStatistics {{\n{body}\n}}\n")).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let p = load_program_with_paths(&path, &[root.join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let output = Runtime::new().run_program(&p).unwrap().output;
    let advance = |n| { let mut s = 7u64; for _ in 0..n { s = s.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407); } s.to_string() };
    assert_eq!(output, [advance(9), advance(21), "0.125".into()]);
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join("main"), &default_c_compiler()).unwrap();
    let native = Command::new(exe).output().unwrap();
    assert!(native.status.success(), "{}", String::from_utf8_lossy(&native.stderr));
    assert_eq!(String::from_utf8(native.stdout).unwrap(), output.join("\n") + "\n");
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn filesystem_errors_preserve_embedded_nul() {
    let out = run_both("nul", r#"
    fn number(r: Result<Nat, Text>) -> Text = match r { ok(n) => nat_to_text(n), err(e) => e }
    fn names(r: Result<List<Text>, Text>) -> Text = match r { ok(xs) => join(xs, ","), err(e) => e }
    print_line(number(file_size_of("{D}/item\0suffix")))
    print_line(number(file_mtime("{D}/item\0suffix")))
    print_line(names(file_list_dir("{D}/item\0suffix")))
    print_line(names(file_walk("{D}/item\0suffix")))
    print_line(number(file_make_dir("{D}/item\0suffix")))
    print_line(number(file_remove_dir("{D}/item\0suffix")))
    print_line(number(file_rename("{D}/item\0suffix", "{D}/other")))
    print_line(number(file_copy("{D}/item", "{D}/item\0suffix")))
    print_line(number(file_write_atomic("{D}/item\0suffix", "bad")))
"#);
    assert_eq!(out.len(), 9);
    assert!(out.iter().all(|s| s.contains('\0') && s.contains("os error")));
}

#[test]
fn ray_intersections_survive_large_and_small_mesh_scales() {
    for scale in [1.0, 1e200, 1e-200] {
        let t = [0.0,0.0,0.0, scale,0.0,0.0, 0.0,scale,0.0];
        let ray = [scale * 0.25, scale * 0.25, scale];
        let hit = dlm_core::bvh::raycast(&t, &ray, &[0.0,0.0,-1.0]).unwrap();
        assert_eq!(hit[0], 0.0);
        assert!((hit[1] / scale - 1.0).abs() < 1e-14, "{scale}: {hit:?}");
    }
    let out = run_both("rays", r#"
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    let s = fl_lit("1e200")
    let h = unwrap_or(bvh_raycast(fv(list(0.0,0.0,0.0,s,0.0,0.0,0.0,s,0.0)), fv(list(fl_mul(s,0.25),fl_mul(s,0.25),s)), fv(list(0.0,0.0,-1.0))), fv_empty())
    print_line(fl_to_text(fl_div(unwrap_or(fv_get(h,1),0.0),s)))
    print_line(fl_to_text(unwrap_or(fv_get(h,0),-1.0)))
"#);
    assert_eq!(out, ["1.0", "0.0"]);
}
