//! Ordinary bounded rational matrices agree across DLM, host and compiled C.
use dlm_core::math::{
    exact::{ExactBudget, Rational},
    rational_matrix::Matrix,
};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;
fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn check(source: &str, expected: &[String], name: &str) {
    let dir =
        std::env::temp_dir().join(format!("dlm-source-qmatrix-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "interpreter: {name}"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap_or_else(|e| panic!("{e:?}"));
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
    assert_eq!(
        String::from_utf8(out.stdout).unwrap().trim_end(),
        expected.join("\n"),
        "C: {name}"
    );
    std::fs::remove_dir_all(dir).unwrap();
}
fn q(a: &Rational) -> String {
    format!(
        "Rational({}, {}, {})",
        a.negative(),
        a.numerator(),
        a.denominator()
    )
}
fn matrix(a: &Matrix) -> String {
    let values = if a.entries().is_empty() {
        "empty_list(Rational)".into()
    } else {
        format!(
            "list({})",
            a.entries().iter().map(q).collect::<Vec<_>>().join(", ")
        )
    };
    format!("QMatrix({}, {}, {values})", a.rows(), a.cols())
}
fn values(a: &Matrix) -> String {
    format!(
        "[{}]",
        a.entries()
            .iter()
            .map(|a| format!("\"{a}\""))
            .collect::<Vec<_>>()
            .join(", ")
    )
}
fn b() -> ExactBudget {
    ExactBudget::default()
}
#[test]
fn exhaustive_two_by_two_source_rref_rank_kernel_and_image_match_host_and_c() {
    let mut source=String::from("module test.qmatrices\nimport std.matrix_q\ntheory App extends StdMatrixQ {\n fn values(m: Result<QMatrix, Text>) -> List<Text> = match m { err(e) => list(e), ok(a) => map(field(a, entries), fun(q: Rational, unwrap_or(rat_text(q), \"ERR\"))) }\n fn reduced(m: Result<QReduced, Text>) -> Result<QMatrix, Text> = match m { err(e) => err(e, QMatrix), ok(a) => ok(field(a, matrix), Text) }\n");
    let mut expected = Vec::new();
    let mut matrices = Vec::new();
    for mut code in 0..81 {
        let data = (0..4)
            .map(|_| {
                let n = code % 3;
                code /= 3;
                Rational::from_integer(n - 1)
            })
            .collect();
        matrices.push(Matrix::new(2, 2, data, &mut b()).unwrap());
    }
    matrices.extend([
        Matrix::parse("1 2\n1/2 1/3", &mut b()).unwrap(),
        Matrix::zero(0, 3, &mut b()).unwrap(),
        Matrix::zero(3, 0, &mut b()).unwrap(),
    ]);
    for a in matrices {
        let m = matrix(&a);
        source += &format!(" print_decimal(unwrap_or(qmat_rank({m}), 999))\n");
        expected.push(a.rank(&mut b()).unwrap().to_string());
        for (op, v) in [
            ("qmat_kernel", a.kernel(&mut b()).unwrap()),
            ("qmat_image", a.image(&mut b()).unwrap()),
        ] {
            source += &format!(" print_symbolic(values({op}({m})))\n");
            expected.push(values(&v));
        }
        source += &format!(" print_symbolic(values(reduced(qmat_rref({m}))))\n");
        expected.push(values(&a.rref(&mut b()).unwrap().0));
    }
    source += "}\n";
    check(&source, &expected, "exhaustive");
}
#[test]
fn source_add_multiply_transpose_columns_and_constructors_match_host_and_c() {
    let a = Matrix::parse("2 2\n1/2 -1/3\n2/5 7/8", &mut b()).unwrap();
    let c = Matrix::parse("2 2\n-3/4 2/3\n1/5 -2", &mut b()).unwrap();
    let calls = [
        (
            format!("qmat_add({}, {})", matrix(&a), matrix(&c)),
            a.add(&c, &mut b()).unwrap(),
        ),
        (
            format!("qmat_mul({}, {})", matrix(&a), matrix(&c)),
            a.mul(&c, &mut b()).unwrap(),
        ),
        (
            format!("qmat_transpose({})", matrix(&a)),
            a.transpose(&mut b()).unwrap(),
        ),
        (
            format!("qmat_columns({}, list(1, 0))", matrix(&a)),
            a.select_columns(&[1, 0], &mut b()).unwrap(),
        ),
        (
            "qmat_identity(0)".into(),
            Matrix::identity(0, &mut b()).unwrap(),
        ),
        (
            "qmat_zero(0, 3)".into(),
            Matrix::zero(0, 3, &mut b()).unwrap(),
        ),
        (
            "qmat_identity(2)".into(),
            Matrix::identity(2, &mut b()).unwrap(),
        ),
    ];
    let mut source=String::from("module test.qops\nimport std.matrix_q\ntheory App extends StdMatrixQ {\n fn values(m: Result<QMatrix, Text>) -> List<Text> = match m { err(e) => list(e), ok(a) => map(field(a, entries), fun(q: Rational, unwrap_or(rat_text(q), \"ERR\"))) }\n");
    let mut expected = Vec::new();
    for (call, a) in calls {
        source += &format!(" print_symbolic(values({call}))\n");
        expected.push(values(&a));
    }
    source += "}\n";
    check(&source, &expected, "operations");
}
#[test]
fn raw_shapes_entries_and_scalar_profile_errors_propagate_in_both_backends() {
    let source = r#"module test.bad_qmatrices
import std.matrix_q
theory App extends StdMatrixQ {
 let bad = QMatrix(1, 1, list(Rational(false, 2, 4)))
 let a = QMatrix(1, 1, list(Rational(false, 4294967295, 1)))
 let two = QMatrix(1, 1, list(Rational(false, 2, 1)))
 print_symbolic(is_err(qmat_rank(bad)))
 print_symbolic(is_err(qmat_kernel(bad)))
 print_symbolic(is_err(qmat_mul(a, two)))
 print_symbolic(is_err(qmat_add(a, two)))
 print_symbolic(is_err(qmat_new(2, 2, empty_list(Rational))))
 print_symbolic(is_err(qmat_zero(340282366920938463463374607431768211455, 0)))
 print_symbolic(is_err(qmat_identity(9)))
 print_symbolic(is_err(qmat_at(a, 340282366920938463463374607431768211455, 0)))
 print_symbolic(is_err(qmat_columns(a, list(1))))
 print_symbolic(is_err(qmat_mul(a, QMatrix(0, 1, empty_list(Rational)))))
 print_symbolic(is_err(qmat_add(a, QMatrix(0, 0, empty_list(Rational)))))
 print_symbolic(not(qmat_is_zero(bad)))
 print_symbolic(is_err(qmat_rref(QMatrix(1, 2, list(Rational(false, 1, 4294967295), Rational(false, 4294967295, 1))))))
}
"#;
    check(source, &vec!["true".into(); 13], "errors");
}
