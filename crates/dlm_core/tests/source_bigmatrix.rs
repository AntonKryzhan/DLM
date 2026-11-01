//! Source BigRational matrices with sequential budgets: host/interpreter/C parity.
use dlm_core::math::{
    exact::{ExactBudget, Natural, Rational},
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
    let dir = std::env::temp_dir().join(format!(
        "dlm-source-bigmatrix-{}-{name}",
        std::process::id()
    ));
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
fn b() -> ExactBudget {
    ExactBudget::new(4096, 100_000_000).unwrap()
}
fn words(n: &Natural) -> String {
    let mut n = n.clone();
    let mut out = Vec::new();
    let radix = Natural::from_u128(65536);
    while !n.is_zero() {
        let (q, r) = n.div_rem(&radix, &mut b()).unwrap();
        out.push(r.to_string());
        n = q;
    }
    if out.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!("list({})", out.join(", "))
    }
}
fn q(q: &Rational) -> String {
    format!(
        "BigRational({}, {}, {})",
        q.negative(),
        words(q.numerator()),
        words(q.denominator())
    )
}
fn matrix(a: &Matrix) -> String {
    let entries = if a.entries().is_empty() {
        "empty_list(BigRational)".into()
    } else {
        format!(
            "list({})",
            a.entries().iter().map(q).collect::<Vec<_>>().join(", ")
        )
    };
    format!("BigMatrix({}, {}, {entries})", a.rows(), a.cols())
}
fn m(r: usize, c: usize, data: &[i128]) -> Matrix {
    Matrix::new(
        r,
        c,
        data.iter().map(|&n| Rational::from_integer(n)).collect(),
        &mut b(),
    )
    .unwrap()
}
fn text(a: &Matrix) -> String {
    let mut s = format!("{} {}\n", a.rows(), a.cols());
    if a.cols() > 0 {
        for row in a.entries().chunks(a.cols()) {
            s += &row
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(" ");
            s.push('\n');
        }
    }
    format!("\"{s}\"")
}
fn header() -> String {
    r#"module test.bigmatrix
import std.matrix_bigq
theory App extends StdMatrixBigQ {
 let b = ExactBudget(1024, 1000000000000000)
 fn show(r: Result<BigMatrixCalc, Text>) -> Text = match r { err(e) => concat("ERR:", e), ok(v) => match bmat_text(field(v, budget), field(v, value)) { err(e) => concat("ERR:", e), ok(t) => field(t, value) } }
 fn scalar(r: Result<BigRationalCalc, Text>) -> Text = match r { err(e) => concat("ERR:", e), ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => concat("ERR:", e), ok(t) => field(t, value) } }
 fn reduced(r: Result<BigMatrixReduced, Text>) -> Result<BigMatrixCalc, Text> = match r { err(e) => err(e, BigMatrixCalc), ok(v) => ok(BigMatrixCalc(field(v, matrix), field(v, budget)), Text) }
 fn rank(r: Result<BigMatrixRank, Text>) -> Nat = match r { err(e) => 999, ok(v) => field(v, rank) }
 fn particular(r: Result<BigMatrixSolution, Text>) -> Result<BigMatrixCalc, Text> = match r { err(e) => err(e, BigMatrixCalc), ok(v) => ok(BigMatrixCalc(field(v, particular), field(v, budget)), Text) }
 fn homogeneous(r: Result<BigMatrixSolution, Text>) -> Result<BigMatrixCalc, Text> = match r { err(e) => err(e, BigMatrixCalc), ok(v) => ok(BigMatrixCalc(field(v, kernel), field(v, budget)), Text) }
"#.into()
}
#[test]
fn all_small_two_by_two_source_ranks_rref_kernels_and_determinants_match_c() {
    let mut source = header();
    let mut expected = Vec::new();
    for mut code in 0..81 {
        let data: Vec<_> = (0..4)
            .map(|_| {
                let v = code % 3;
                code /= 3;
                v as i128 - 1
            })
            .collect();
        let a = m(2, 2, &data);
        let sm = matrix(&a);
        source+=&format!(" print_decimal(rank(bmat_rank(b, {sm})))\n print_symbolic(show(reduced(bmat_rref(b, {sm}))))\n print_symbolic(show(bmat_kernel(b, {sm})))\n print_line(scalar(bmat_det(b, {sm})))\n");
        expected.extend([
            a.rank(&mut b()).unwrap().to_string(),
            text(&a.rref(&mut b()).unwrap().0),
            text(&a.kernel(&mut b()).unwrap()),
            a.determinant(&mut b()).unwrap().to_string(),
        ]);
    }
    source += "}\n";
    check(&source, &expected, "exhaustive");
}
#[test]
fn independent_fraction_matrix_fixtures_match_source_and_native_c() {
    let mut source = header();
    let mut expected = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    let mut count = 0;
    for line in include_str!("fixtures/rational_matrices093.tsv")
        .lines()
        .filter(|s| !s.starts_with('#'))
    {
        let v: Vec<_> = line.split('\t').collect();
        let rows = v[0].parse::<usize>().unwrap();
        let cols = v[1].parse::<usize>().unwrap();
        if rows > 4 || cols > 4 || !seen.insert((rows, cols)) {
            continue;
        }
        let a = Matrix::parse(
            &format!("{rows} {cols}\n{}", v[2].replace(',', " ")),
            &mut b(),
        )
        .unwrap();
        let reduced = Matrix::parse(
            &format!("{rows} {cols}\n{}", v[3].replace(',', " ")),
            &mut b(),
        )
        .unwrap();
        let rank = if v[4].is_empty() {
            0
        } else {
            v[4].split(',').count()
        };
        let kernel = Matrix::parse(
            &format!("{} {}\n{}", cols, cols - rank, v[6].replace(',', " ")),
            &mut b(),
        )
        .unwrap();
        let sm = matrix(&a);
        source+=&format!(" print_symbolic(show(reduced(bmat_rref(b, {sm}))))\n print_symbolic(show(bmat_kernel(b, {sm})))\n");
        expected.extend([text(&reduced), text(&kernel)]);
        if rows == cols {
            source += &format!(" print_line(scalar(bmat_det(b, {sm})))\n");
            expected.push(v[5].to_string());
        }
        count += 1;
    }
    assert_eq!(count, 9);
    source += "}\n";
    check(&source, &expected, "fraction-oracle");
}
#[test]
fn exact_source_matrix_arithmetic_columns_and_wide_inverse() {
    let a = Matrix::parse("2 2\n1/2 -1/3\n2/5 7/8", &mut b()).unwrap();
    let c = Matrix::parse("2 2\n-3/4 2/3\n1/5 -2", &mut b()).unwrap();
    let large = Matrix::parse(
        "2 2\n340282366920938463463374607431768211456 1\n1 0",
        &mut b(),
    )
    .unwrap();
    let calls = [
        (
            format!("bmat_add(b, {}, {})", matrix(&a), matrix(&c)),
            a.add(&c, &mut b()).unwrap(),
        ),
        (
            format!("bmat_mul(b, {}, {})", matrix(&a), matrix(&c)),
            a.mul(&c, &mut b()).unwrap(),
        ),
        (
            format!("bmat_transpose(b, {})", matrix(&a)),
            a.transpose(&mut b()).unwrap(),
        ),
        (
            format!("bmat_columns(b, {}, list(1, 0))", matrix(&a)),
            a.select_columns(&[1, 0], &mut b()).unwrap(),
        ),
        (
            format!("bmat_image(b, {})", matrix(&a)),
            a.image(&mut b()).unwrap(),
        ),
        (
            format!("bmat_inverse(b, {})", matrix(&large)),
            large.inverse(&mut b()).unwrap(),
        ),
    ];
    let mut source = header();
    let mut expected = Vec::new();
    for (call, v) in calls {
        source += &format!(" print_symbolic(show({call}))\n");
        expected.push(text(&v));
    }
    let mut high = Natural::from_u128(1);
    for _ in 0..1023 {
        high = high.checked_add(&high, &mut b()).unwrap();
    }
    let high = Matrix::new(
        1,
        1,
        vec![Rational::new(false, high, Natural::from_u128(1), &mut b()).unwrap()],
        &mut b(),
    )
    .unwrap();
    source += &format!(
        " print_line(scalar(bmat_det(b, {})))\n print_symbolic(show(bmat_inverse(b, {})))\n",
        matrix(&high),
        matrix(&high)
    );
    expected.push(high.entries()[0].to_string());
    expected.push(text(&high.inverse(&mut b()).unwrap()));
    source += &format!(" print_line(scalar(bmat_det(b, {})))\n", matrix(&large));
    expected.push("-1/1".into());
    source += "}\n";
    check(&source, &expected, "operations-wide");
}
#[test]
fn source_general_solution_multiple_rhs_and_full_kernel() {
    let mut source = header();
    let mut expected = Vec::new();
    for (a, rhs) in [
        (m(2, 3, &[1, 2, 3, 2, 4, 6]), m(2, 2, &[7, 1, 14, 2])),
        (
            Matrix::parse("2 2\n1 1/2\n1/3 1", &mut b()).unwrap(),
            m(2, 2, &[1, 0, 2, 1]),
        ),
    ] {
        let s = a.solve(&rhs, &mut b()).unwrap();
        let call = format!("bmat_solve(b, {}, {})", matrix(&a), matrix(&rhs));
        source+=&format!(" print_symbolic(show(particular({call})))\n print_symbolic(show(homogeneous({call})))\n");
        expected.extend([text(&s.particular), text(&s.kernel)]);
    }
    source += "}\n";
    check(&source, &expected, "solve");
}
#[test]
fn source_empty_shapes_maximum_augmentation_and_identity() {
    let mut source = header();
    let mut expected = Vec::new();
    for n in [0, 4] {
        let a = Matrix::identity(n, &mut b()).unwrap();
        source+=&format!(" print_symbolic(show(bmat_identity(b, {n})))\n print_symbolic(show(bmat_inverse(b, {})))\n",matrix(&a));
        expected.extend([text(&a), text(&a)]);
    }
    for (r, c) in [(0, 4), (4, 0), (0, 0)] {
        let a = Matrix::zero(r, c, &mut b()).unwrap();
        source+=&format!(" print_symbolic(show(bmat_zero(b, {r}, {c})))\n print_symbolic(show(bmat_kernel(b, {})))\n",matrix(&a));
        expected.extend([text(&a), text(&a.kernel(&mut b()).unwrap())]);
    }
    let a = m(0, 4, &[]);
    let rhs = m(0, 3, &[]);
    let s = a.solve(&rhs, &mut b()).unwrap();
    source+=&format!(" print_symbolic(show(particular(bmat_solve(b, {}, {}))))\n print_symbolic(show(homogeneous(bmat_solve(b, {}, {}))))\n",matrix(&a),matrix(&rhs),matrix(&a),matrix(&rhs));
    expected.extend([text(&s.particular), text(&s.kernel)]);
    source += "}\n";
    check(&source, &expected, "empty-max");
}
#[test]
fn raw_records_precision_work_and_budget_sequencing_are_enforced() {
    let source = header()
        + r#"
 let one = BigRational(false, list(1), list(1))
 let zero = BigRational(false, empty_list(Nat), list(1))
 let a = BigMatrix(2, 2, list(one, one, one, one))
 let small = ExactBudget(128, 350000000)
 let fallback = BigMatrixCalc(BigMatrix(0, 0, empty_list(BigRational)), b)
 let product = unwrap_or(bmat_mul(small, a, a), fallback)
 print_decimal(field(field(product, budget), work))
 print_decimal(field(small, work))
 print_symbolic(is_err(bmat_add(ExactBudget(128, 125000000), a, a)))
 print_symbolic(is_err(bmat_new(b, 5, 0, empty_list(BigRational))))
 print_symbolic(is_err(bmat_new(b, 340282366920938463463374607431768211455, 1, empty_list(BigRational))))
 print_symbolic(is_err(bmat_new(b, 2, 2, list(one))))
 print_symbolic(is_err(bmat_check(ExactBudget(0, 100), BigMatrix(0, 0, empty_list(BigRational)))))
 print_symbolic(is_err(bmat_new(b, 1, 1, list(BigRational(false, list(2), list(4))))))
 print_symbolic(is_err(bmat_new(b, 1, 1, list(BigRational(false, list(65536), list(1))))))
 print_symbolic(is_err(bmat_new(b, 1, 1, list(BigRational(true, empty_list(Nat), list(1))))))
 print_symbolic(is_err(bmat_new(ExactBudget(1, 1000000000000000), 1, 1, list(BigRational(false, list(2), list(1))))))
 print_symbolic(is_err(bmat_mul(ExactBudget(1, 1000000000000000), a, a)))
 print_symbolic(is_err(bmat_at(b, a, 340282366920938463463374607431768211455, 0)))
 print_symbolic(is_err(bmat_columns(b, a, list(2))))
 print_symbolic(is_err(bmat_inverse(b, a)))
 print_symbolic(is_err(bmat_solve(b, a, BigMatrix(2, 1, list(one, zero)))))
 print_symbolic(is_err(bmat_mul(b, a, BigMatrix(1, 1, list(one)))))
 print_symbolic(is_err(bmat_det(b, BigMatrix(1, 2, list(one, one)))))
}
"#;
    let mut expected = vec!["50000000".into(), "350000000".into()];
    expected.extend(vec!["true".into(); 16]);
    check(&source, &expected, "validation-budget");
}
#[test]
fn private_matrix_helpers_cannot_be_called_or_replaced() {
    let dir = std::env::temp_dir().join(format!("dlm-bigmatrix-private-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    for(body,message)in[(" print_symbolic(bm_one())\n","private"),(" fn bm_shape(a: BigMatrix) -> Bool = true\n print_symbolic(bmat_check(exact_default_budget(), BigMatrix(5, 5, empty_list(BigRational))))\n","redeclares")]{std::fs::write(&path,format!("module test.private\nimport std.matrix_bigq\ntheory App extends StdMatrixBigQ {{\n{body}}}\n")).unwrap();let p=load_program_with_paths(&path,&[repo().join("lib")]).unwrap();let report=Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);assert!(!report.ok());assert!(report.diagnostics.iter().any(|d|d.message.contains(message)),"{:?}",report.diagnostics);assert!(lower_program(&p,CheckPolicy::trusted_only()).is_err());}
    std::fs::remove_dir_all(dir).unwrap();
}
