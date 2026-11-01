use dlm_core::math::{
    exact::{ExactBudget, Rational},
    rational_matrix::Matrix,
};
fn b() -> ExactBudget {
    ExactBudget::default()
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
fn qs(a: &Matrix) -> Vec<String> {
    a.entries().iter().map(ToString::to_string).collect()
}
#[test]
fn exhaustive_small_integer_matrices_have_correct_determinant_and_kernel() {
    for code in 0usize..625 {
        let mut n = code;
        let d: Vec<_> = (0..4)
            .map(|_| {
                let x = (n % 5) as i128 - 2;
                n /= 5;
                x
            })
            .collect();
        let a = m(2, 2, &d);
        let det = d[0] * d[3] - d[1] * d[2];
        assert_eq!(
            a.determinant(&mut b()).unwrap(),
            Rational::from_integer(det)
        );
        let k = a.kernel(&mut b()).unwrap();
        assert!(a.mul(&k, &mut b()).unwrap().is_zero());
        assert_eq!(a.rank(&mut b()).unwrap() + k.cols(), 2);
        assert_eq!(k.rank(&mut b()).unwrap(), k.cols());
        if det != 0 {
            let inv = a.inverse(&mut b()).unwrap();
            assert_eq!(a.mul(&inv, &mut b()).unwrap(), m(2, 2, &[1, 0, 0, 1]));
            assert_eq!(inv.mul(&a, &mut b()).unwrap(), m(2, 2, &[1, 0, 0, 1]));
        } else {
            assert!(a.inverse(&mut b()).is_err());
        }
    }
}
#[test]
fn rational_pivots_row_swaps_and_rank_deficient_images() {
    let a = Matrix::parse("2 3\n0 2/3 1/2\n1/5 -2 3/7", &mut b()).unwrap();
    let (r, p) = a.rref(&mut b()).unwrap();
    assert_eq!(p, vec![0, 1]);
    assert_eq!(qs(&r), vec!["1/1", "0/1", "135/14", "0/1", "1/1", "3/4"]);
    let k = a.kernel(&mut b()).unwrap();
    assert_eq!(qs(&k), vec!["-135/14", "-3/4", "1/1"]);
    assert!(a.mul(&k, &mut b()).unwrap().is_zero());
    assert_eq!(
        a.image(&mut b()).unwrap(),
        a.select_columns(&[0, 1], &mut b()).unwrap()
    );
    assert_eq!(
        m(2, 2, &[0, 1, 1, 0]).determinant(&mut b()).unwrap(),
        Rational::from_integer(-1)
    );
}
#[test]
fn solve_returns_particular_solution_and_full_homogeneous_kernel() {
    let a = m(2, 3, &[1, 2, 3, 2, 4, 6]);
    let rhs = m(2, 2, &[7, 1, 14, 2]);
    let s = a.solve(&rhs, &mut b()).unwrap();
    assert_eq!(a.mul(&s.particular, &mut b()).unwrap(), rhs);
    assert!(a.mul(&s.kernel, &mut b()).unwrap().is_zero());
    assert_eq!(s.kernel.cols(), 2);
    assert_eq!(s.kernel.rank(&mut b()).unwrap(), 2);
    let shifted = s
        .particular
        .add(
            &s.kernel.mul(&m(2, 2, &[4, 5, 6, 7]), &mut b()).unwrap(),
            &mut b(),
        )
        .unwrap();
    assert_eq!(a.mul(&shifted, &mut b()).unwrap(), rhs);
    assert!(a
        .solve(&m(2, 1, &[1, 3]), &mut b())
        .unwrap_err()
        .0
        .contains("inconsistent"));
}
#[test]
fn empty_shapes_rectangles_and_private_augmentation() {
    let empty = m(0, 0, &[]);
    assert_eq!(empty.determinant(&mut b()).unwrap(), Rational::one());
    assert_eq!(empty.inverse(&mut b()).unwrap(), empty);
    let a = m(0, 3, &[]);
    let s = a.solve(&m(0, 2, &[]), &mut b()).unwrap();
    assert_eq!((s.particular.rows(), s.particular.cols()), (3, 2));
    assert_eq!(s.kernel, Matrix::identity(3, &mut b()).unwrap());
    assert!(m(2, 0, &[]).solve(&m(2, 1, &[0, 1]), &mut b()).is_err());
    assert_eq!(m(2, 0, &[]).rank(&mut b()).unwrap(), 0);
    let identity = Matrix::identity(32, &mut b()).unwrap();
    assert_eq!(identity.inverse(&mut b()).unwrap(), identity);
    assert!(Matrix::zero(33, 0, &mut b()).is_err());
    assert!(Matrix::new(2, 2, vec![Rational::one()], &mut b()).is_err());
}
#[test]
fn more_than_128_bit_entries_and_explicit_budget_limits() {
    let text = "2 2\n340282366920938463463374607431768211456 1\n1 0";
    let a = Matrix::parse(text, &mut b()).unwrap();
    assert_eq!(a.determinant(&mut b()).unwrap(), Rational::from_integer(-1));
    let inv = a.inverse(&mut b()).unwrap();
    assert_eq!(a.mul(&inv, &mut b()).unwrap(), m(2, 2, &[1, 0, 0, 1]));
    assert!(Matrix::parse(text, &mut ExactBudget::new(128, 1_000_000).unwrap()).is_err());
    let copy = a.clone();
    assert!(a.rref(&mut ExactBudget::new(4096, 0).unwrap()).is_err());
    assert_eq!(a, copy);
    let small = m(2, 2, &[2, 1, 1, 1]);
    assert!(small
        .inverse(&mut ExactBudget::new(1, 1_000_000).unwrap())
        .is_err());
}
#[test]
fn strict_text_shapes_and_operation_errors() {
    for text in [
        "", "2", "1 1\n", "1 1\n1 2", "33 0", "-1 0", "1 1\n1/0", "1 1\n+2",
    ] {
        assert!(Matrix::parse(text, &mut b()).is_err(), "{text}");
    }
    assert!(Matrix::parse(&" ".repeat(262145), &mut b()).is_err());
    let a = m(2, 3, &[1, 0, 0, 0, 1, 0]);
    assert!(a.determinant(&mut b()).is_err());
    assert!(a.inverse(&mut b()).is_err());
    assert!(a.solve(&m(1, 1, &[1]), &mut b()).is_err());
    assert!(a.add(&m(1, 1, &[1]), &mut b()).is_err());
    assert!(a.mul(&m(2, 2, &[1, 0, 0, 1]), &mut b()).is_err());
    assert!(a.select_columns(&[3], &mut b()).is_err());
    assert_eq!(Matrix::parse(&a.to_string(), &mut b()).unwrap(), a);
    assert_eq!(
        a.transpose(&mut b()).unwrap().transpose(&mut b()).unwrap(),
        a
    );
}
#[test]
fn independent_fraction_row_reduction_and_leibniz_determinant_oracle() {
    let mut count = 0;
    for line in include_str!("fixtures/rational_matrices093.tsv")
        .lines()
        .filter(|s| !s.starts_with('#'))
    {
        let x: Vec<_> = line.split('\t').collect();
        let rows = x[0].parse::<usize>().unwrap();
        let cols = x[1].parse::<usize>().unwrap();
        let a = Matrix::parse(
            &format!("{rows} {cols}\n{}", x[2].replace(',', " ")),
            &mut b(),
        )
        .unwrap();
        let (r, pivots) = a.rref(&mut b()).unwrap();
        assert_eq!(qs(&r).join(","), x[3], "rref {line}");
        assert_eq!(
            pivots
                .iter()
                .map(usize::to_string)
                .collect::<Vec<_>>()
                .join(","),
            x[4]
        );
        if x[5] != "-" {
            assert_eq!(
                a.determinant(&mut b()).unwrap().to_string(),
                x[5],
                "det {line}"
            );
        }
        assert_eq!(
            qs(&a.kernel(&mut b()).unwrap()).join(","),
            x[6],
            "kernel {line}"
        );
        count += 1;
    }
    assert_eq!(count, 50);
}
