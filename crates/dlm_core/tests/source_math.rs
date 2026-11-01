//! Differential validation of ordinary .dlm libraries against the host APIs
//! and the C backend. No mathematical library builtin bypass is involved.
use dlm_core::math::{f2::Matrix, topology::SimplicialComplex};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Program, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;
fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn scratch(name: &str) -> PathBuf {
    let p = std::env::temp_dir().join(format!("dlm-source-math-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&p).unwrap();
    p
}
fn load(source: &str, name: &str) -> (Program, PathBuf) {
    let dir = scratch(name);
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let checked = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(checked.ok(), "{:?}", checked.diagnostics);
    (p, dir)
}
fn native(p: &Program, dir: &Path) -> String {
    let ir = lower_program(p, CheckPolicy::trusted_only()).unwrap_or_else(|e| panic!("{e:?}"));
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8(out.stdout).unwrap().trim_end().into()
}
fn source_matrix(m: &Matrix) -> String {
    let data = if m.entries().is_empty() {
        "empty_list(Bool)".into()
    } else {
        format!(
            "list({})",
            m.entries()
                .iter()
                .map(|&b| if b == 1 { "true" } else { "false" })
                .collect::<Vec<_>>()
                .join(", ")
        )
    };
    format!("F2Matrix({}, {}, {data})", m.rows(), m.cols())
}
fn display_matrix(m: &Matrix) -> String {
    format!(
        "ok(F2Matrix{{rows: {}, cols: {}, entries: [{}]}})",
        m.rows(),
        m.cols(),
        m.entries()
            .iter()
            .map(|&b| if b == 1 { "true" } else { "false" })
            .collect::<Vec<_>>()
            .join(", ")
    )
}
#[test]
fn source_matrices_match_host_on_all_binary_two_by_three_inputs_and_native() {
    let mut source = String::from(
        "module test.matrix\nimport std.matrix_f2\ntheory App extends StdMatrixF2 {\n",
    );
    let mut expected = Vec::new();
    for bits in 0..64 {
        let a = Matrix::new(2, 3, (0..6).map(|i| ((bits >> i) & 1) as u8).collect()).unwrap();
        source+=&format!("    let a{bits} = {}\n    print_decimal(unwrap_or(mat_rank(a{bits}), 999))\n    print_symbolic(mat_kernel(a{bits}))\n    print_symbolic(mat_image(a{bits}))\n    print_symbolic(mat_transpose(a{bits}))\n    print_symbolic(mat_mul(a{bits}, unwrap_or(mat_transpose(a{bits}), a{bits})))\n",source_matrix(&a));
        expected.push(a.rank().to_string());
        expected.push(display_matrix(&a.kernel()));
        expected.push(display_matrix(&a.image()));
        expected.push(display_matrix(&a.transpose()));
        expected.push(display_matrix(&a.mul(&a.transpose()).unwrap()));
    }
    let left = Matrix::new(2, 2, vec![1, 1, 0, 1]).unwrap();
    let right = Matrix::new(2, 2, vec![0, 1, 1, 0]).unwrap();
    source += &format!(
        "    let left = {}\n    let right = {}\n",
        source_matrix(&left),
        source_matrix(&right)
    );
    source += "    print_symbolic(mat_add(left, right))\n    print_symbolic(mat_tensor(left, right))\n    print_symbolic(mat_columns(left, list(1, 0, 1)))\n    print_symbolic(mat_is_zero(left))\n    print_symbolic(mat_equal(left, right))\n    print_symbolic(mat_equal(left, left))\n";
    expected.push(display_matrix(&left.add(&right).unwrap()));
    expected.push(display_matrix(&left.tensor(&right).unwrap()));
    expected.push(display_matrix(&left.select_columns(&[1, 0, 1]).unwrap()));
    expected.extend(["false".into(), "false".into(), "true".into()]);
    source += "}\n";
    let (p, dir) = load(&source, "matrices");
    let actual = Runtime::new().run_program(&p).unwrap().output;
    assert_eq!(actual, expected);
    assert_eq!(native(&p, &dir), expected.join("\n"));
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn source_homology_matches_host_for_circle_disk_sphere_and_rp2() {
    let mut source=String::from("module test.homology\nimport std.homology_f2\ntheory App extends StdHomologyF2 {\n    fn dim(h: Result<F2Homology, Text>) -> Nat = match h { ok(g) => field(g, betti), err(_) => 999 }\n");
    let mut expected = Vec::new();
    let examples = [
        "circle.simp",
        "disk.simp",
        "sphere.simp",
        "projective_plane.simp",
        "torus.simp",
    ];
    for (case, name) in examples.iter().enumerate() {
        let c = SimplicialComplex::parse(
            &std::fs::read_to_string(repo().join("examples/math").join(name)).unwrap(),
        )
        .unwrap()
        .chains()
        .unwrap();
        for (n, down) in c.boundaries().iter().enumerate() {
            let top = Matrix::zero(down.cols(), 0).unwrap();
            let up = c.boundaries().get(n + 1).unwrap_or(&top);
            source+=&format!("    let d{case}_{n} = {}\n    let u{case}_{n} = {}\n    print_decimal(dim(homology_f2(d{case}_{n}, u{case}_{n})))\n    print_decimal(dim(cohomology_f2(d{case}_{n}, u{case}_{n})))\n",source_matrix(down),source_matrix(up));
            expected.push(c.homology(n).unwrap().betti.to_string());
            expected.push(c.cohomology(n).unwrap().betti.to_string());
        }
    }
    source += "}\n";
    let (p, dir) = load(&source, "homology");
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    assert_eq!(native(&p, &dir), expected.join("\n"));
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn source_matrix_boundaries_and_failures_are_explicit_in_both_backends() {
    let source = r#"module test.errors
import std.homology_f2
theory App extends StdHomologyF2 {
    let empty = F2Matrix(0, 0, empty_list(Bool))
    print_symbolic(mat_kernel(F2Matrix(0, 3, empty_list(Bool))))
    print_symbolic(mat_rank(F2Matrix(3, 0, empty_list(Bool))))
    print_symbolic(mat_identity(0))
    print_symbolic(mat_add(empty, empty))
    print_symbolic(mat_tensor(empty, empty))
    print_symbolic(mat_hstack(F2Matrix(2, 0, empty_list(Bool)), F2Matrix(2, 0, empty_list(Bool))))
    print_symbolic(mat_columns(F2Matrix(0, 3, empty_list(Bool)), list(2, 0)))
    print_symbolic(chain_homology_f2(empty_list(F2Matrix)))
    print_symbolic(mat_rank(F2Matrix(2, 2, list(true))))
    print_symbolic(mat_identity(33))
    print_symbolic(mat_new(340282366920938463463374607431768211455, 2, empty_list(Bool)))
    print_symbolic(mat_at(F2Matrix(1, 1, list(true)), 340282366920938463463374607431768211455, 0))
    print_symbolic(mat_columns(F2Matrix(0, 3, empty_list(Bool)), list(3)))
    print_symbolic(mat_mul(empty, F2Matrix(1, 1, list(true))))
    print_symbolic(homology_f2(F2Matrix(1, 1, list(true)), F2Matrix(1, 1, list(true))))
    print_symbolic(chain_homology_f2(list(F2Matrix(1, 1, list(true)))))
    print_symbolic(mat_add(empty, F2Matrix(1, 0, empty_list(Bool))))
    print_symbolic(mat_hstack(F2Matrix(0, 32, empty_list(Bool)), F2Matrix(0, 1, empty_list(Bool))))
    print_symbolic(mat_tensor(F2Matrix(32, 0, empty_list(Bool)), F2Matrix(2, 0, empty_list(Bool))))
    print_symbolic(mat_extend_basis(empty, F2Matrix(1, 0, empty_list(Bool))))
}
"#;
    let (p, dir) = load(source, "errors");
    let actual = Runtime::new().run_program(&p).unwrap().output;
    assert_eq!(&actual[0..8],&[
        "ok(F2Matrix{rows: 3, cols: 3, entries: [true, false, false, false, true, false, false, false, true]})",
        "ok(0)","ok(F2Matrix{rows: 0, cols: 0, entries: []})","ok(F2Matrix{rows: 0, cols: 0, entries: []})","ok(F2Matrix{rows: 0, cols: 0, entries: []})","ok(F2Matrix{rows: 2, cols: 0, entries: []})","ok(F2Matrix{rows: 0, cols: 2, entries: []})","ok([])"]);
    assert!(
        actual[8..].iter().all(|s| s.starts_with("err(")),
        "{actual:?}"
    );
    assert_eq!(native(&p, &dir), actual.join("\n"));
    std::fs::remove_dir_all(dir).unwrap();
}
