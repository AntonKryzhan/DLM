//! Differential source/interpreter/host/C checks; no math builtins are introduced.
use dlm_core::math::{
    prime::{Matrix, PrimeField},
    rational::Rational,
    topology::SimplicialComplex,
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
        std::env::temp_dir().join(format!("dlm-source-numeric-{}-{name}", std::process::id()));
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
fn q(negative: bool, n: u128, d: u128) -> Rational {
    Rational::new(negative, n, d).unwrap()
}
fn source_q(a: Rational) -> String {
    format!(
        "Rational({}, {}, {})",
        a.negative(),
        a.numerator(),
        a.denominator()
    )
}
fn nat_list(xs: &[u8]) -> String {
    if xs.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!(
            "list({})",
            xs.iter().map(u8::to_string).collect::<Vec<_>>().join(", ")
        )
    }
}
fn source_m(a: &Matrix) -> String {
    format!(
        "FpMatrix({}, {}, {}, {})",
        a.field().modulus(),
        a.rows(),
        a.cols(),
        nat_list(a.entries())
    )
}
fn entries(a: &Matrix) -> String {
    format!(
        "[{}]",
        a.entries()
            .iter()
            .map(u8::to_string)
            .collect::<Vec<_>>()
            .join(", ")
    )
}
fn host_m(p: u16, r: usize, c: usize, data: &[u8]) -> Matrix {
    Matrix::new(PrimeField::new(p).unwrap(), r, c, data.to_vec()).unwrap()
}
#[test]
fn source_rationals_match_host_with_declared_profile() {
    let aa = [
        q(true, 2, 1),
        q(true, 1, 1),
        q(false, 0, 1),
        q(false, 1, 2),
        q(false, 2, 3),
        q(false, 4294967295, 1),
    ];
    let bb = [
        q(true, 1, 1),
        q(false, 0, 1),
        q(false, 1, 3),
        q(false, 1, 2),
        q(true, 2, 3),
        q(false, 1, 1),
        q(false, 2, 1),
    ];
    let mut pairs: Vec<_> = aa
        .iter()
        .flat_map(|&a| bb.iter().map(move |&b| (a, b)))
        .collect();
    pairs.extend([
        (q(false, 4294967295, 2), q(false, 4294967295, 2)),
        (
            q(false, 4294967295, 4294967294),
            q(false, 4294967294, 4294967295),
        ),
    ]);
    assert_eq!(pairs.len(), 44);
    let mut source=String::from("module test.rationals\nimport std.rational\ntheory App extends StdRational {\n fn show(r: Result<Rational, Text>) -> Text = match r { err(e) => \"ERR\", ok(a) => unwrap_or(rat_text(a), \"ERR\") }\n");
    let mut expected = Vec::new();
    for (a, b) in pairs {
        for (op, result) in [
            ("rat_add", a.checked_add(b)),
            ("rat_sub", a.checked_sub(b)),
            ("rat_mul", a.checked_mul(b)),
            ("rat_div", a.checked_div(b)),
        ] {
            source += &format!(
                " print_line(show({op}({}, {})))\n",
                source_q(a),
                source_q(b)
            );
            expected.push(match result {
                Ok(v) if v.numerator() <= 4294967295 && v.denominator() <= 4294967295 => {
                    v.to_string()
                }
                _ => "ERR".into(),
            });
        }
        source += &format!(
            " print_symbolic(rat_cmp({}, {}))\n",
            source_q(a),
            source_q(b)
        );
        expected.push(format!("ok({})", a.cmp(&b) as i32));
    }
    source+=" print_line(show(rat_new(true, 0, 340282366920938463463374607431768211455)))\n print_line(show(rat_new(false, 8589934590, 2)))\n print_line(show(rat_new(true, 6, 8)))\n}\n";
    expected.extend(["0/1".into(), "4294967295/1".into(), "-3/4".into()]);
    check(&source, &expected, "rationals");
}
#[test]
fn source_prime_matrices_match_host_and_c() {
    let mut source=String::from("module test.prime_matrices\nimport std.matrix_fp\ntheory App extends StdMatrixFp {\n fn values(m: Result<FpMatrix, Text>) -> List<Nat> = match m { ok(a) => field(a, entries), err(e) => list(999) }\n");
    let mut expected = Vec::new();
    let mut matrices = Vec::new();
    for mut code in 0..81 {
        let data: Vec<_> = (0..4)
            .map(|_| {
                let d = code % 3;
                code /= 3;
                d as u8
            })
            .collect();
        matrices.push(host_m(3, 2, 2, &data));
    }
    matrices.extend([
        host_m(5, 1, 2, &[2, 1]),
        host_m(251, 2, 2, &[250, 2, 3, 249]),
        host_m(3, 0, 3, &[]),
        host_m(5, 3, 0, &[]),
    ]);
    for a in matrices {
        let sm = source_m(&a);
        source += &format!(" print_decimal(unwrap_or(pmat_rank({sm}), 999))\n");
        expected.push(a.rank().to_string());
        for (op, v) in [
            ("pmat_kernel", a.kernel()),
            ("pmat_image", a.image()),
            ("pmat_transpose", a.transpose()),
        ] {
            source += &format!(" print_symbolic(values({op}({sm})))\n");
            expected.push(entries(&v));
        }
        let identity = Matrix::identity(a.field(), a.cols()).unwrap();
        source += &format!(
            " print_symbolic(values(pmat_mul({sm}, {})))\n",
            source_m(&identity)
        );
        expected.push(entries(&a));
    }
    let a = host_m(5, 2, 2, &[1, 2, 3, 4]);
    let b = host_m(5, 2, 2, &[4, 3, 2, 1]);
    for (call, v) in [
        (
            format!("pmat_add({}, {})", source_m(&a), source_m(&b)),
            a.add(&b).unwrap(),
        ),
        (
            format!("pmat_mul({}, {})", source_m(&a), source_m(&b)),
            a.mul(&b).unwrap(),
        ),
        (
            format!("pmat_columns({}, list(1, 0))", source_m(&a)),
            a.select_columns(&[1, 0]).unwrap(),
        ),
        (
            format!("pmat_hstack({}, {})", source_m(&a), source_m(&b)),
            a.hstack(&b).unwrap(),
        ),
        (
            format!("pmat_extend_basis({}, {})", source_m(&a), source_m(&b)),
            a.extend_basis(&b).unwrap(),
        ),
    ] {
        source += &format!(" print_symbolic(values({call}))\n");
        expected.push(entries(&v));
    }
    source += "}\n";
    check(&source, &expected, "matrices");
}
#[test]
fn source_oriented_homology_and_cohomology_match_host() {
    let mut source=String::from("module test.prime_homology\nimport std.homology_fp\ntheory App extends StdHomologyFp {\n fn dimension(h: Result<FpHomology, Text>) -> Nat = match h { err(e) => 999, ok(v) => field(v, betti) }\n fn cycles(h: Result<FpHomology, Text>) -> List<Nat> = match h { err(e) => list(999), ok(v) => field(field(v, cycles), entries) }\n");
    let mut expected = Vec::new();
    for p in [2, 3] {
        for name in ["circle", "disk", "sphere", "projective_plane"] {
            let s = SimplicialComplex::parse(
                &std::fs::read_to_string(repo().join(format!("examples/math/{name}.simp")))
                    .unwrap(),
            )
            .unwrap();
            let c = s.chains_over(PrimeField::new(p).unwrap()).unwrap();
            for n in 0..c.dimensions().len() {
                let down = &c.boundaries()[n];
                let up = if n + 1 < c.dimensions().len() {
                    c.boundaries()[n + 1].clone()
                } else {
                    Matrix::zero(c.field(), c.dimensions()[n], 0).unwrap()
                };
                for (op, h) in [
                    ("homology_fp", c.homology(n).unwrap()),
                    ("cohomology_fp", c.cohomology(n).unwrap()),
                ] {
                    let call = format!("{op}({}, {})", source_m(down), source_m(&up));
                    source += &format!(
                        " print_decimal(dimension({call}))\n print_symbolic(cycles({call}))\n"
                    );
                    expected.push(h.betti.to_string());
                    expected.push(entries(&h.representatives));
                }
            }
        }
    }
    source += "}\n";
    check(&source, &expected, "homology");
}
#[test]
fn invalid_raw_numbers_fields_shapes_and_huge_nats_are_rejected() {
    let rational = r#"module test.bad_rationals
import std.rational
theory App extends StdRational {
 let bad = Rational(false, 2, 4)
 let q = Rational(false, 1, 2)
 print_symbolic(is_err(rat_add(bad, q)))
 print_symbolic(is_err(rat_mul(q, bad)))
 print_symbolic(is_err(rat_cmp(bad, q)))
 print_symbolic(is_err(rat_inv(Rational(true, 0, 1))))
 print_symbolic(is_err(rat_div(q, Rational(false, 0, 1))))
 print_symbolic(is_err(rat_new(false, 1, 0)))
 print_symbolic(is_err(rat_new(false, 4294967296, 1)))
 print_symbolic(is_err(rat_text(Rational(false, 340282366920938463463374607431768211455, 1))))
 print_symbolic(not(rat_equal(bad, bad)))
}
"#;
    check(rational, &vec!["true".into(); 9], "bad-rationals");
    let matrices = r#"module test.bad_prime_matrices
import std.homology_fp
theory App extends StdHomologyFp {
 let a = FpMatrix(3, 1, 1, list(1))
 let b = FpMatrix(5, 1, 1, list(1))
 let bad = FpMatrix(3, 1, 1, list(3))
 print_symbolic(is_err(pmat_add(a, b)))
 print_symbolic(is_err(pmat_mul(a, b)))
 print_symbolic(is_err(pmat_hstack(a, b)))
 print_symbolic(is_err(pmat_extend_basis(a, b)))
 print_symbolic(is_err(pmat_rank(bad)))
 print_symbolic(is_err(pmat_identity(9, 2)))
 print_symbolic(is_err(pmat_zero(3, 340282366920938463463374607431768211455, 0)))
 print_symbolic(is_err(pmat_at(a, 340282366920938463463374607431768211455, 0)))
 print_symbolic(is_err(pmat_columns(a, list(1))))
 print_symbolic(is_err(pmat_new(3, 2, 2, list(1))))
 print_symbolic(is_err(pmat_new(340282366920938463463374607431768211455, 0, 0, empty_list(Nat))))
 print_symbolic(is_err(homology_fp(a, a)))
 print_symbolic(is_err(homology_fp(FpMatrix(3, 0, 0, empty_list(Nat)), FpMatrix(5, 0, 0, empty_list(Nat)))))
 print_symbolic(is_err(cohomology_fp(a, b)))
 print_symbolic(is_err(pmat_rank(FpMatrix(4, 0, 0, empty_list(Nat)))))
}
"#;
    check(matrices, &vec!["true".into(); 15], "bad-matrices");
}
