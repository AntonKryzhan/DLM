use dlm_core::math::{
    exact::{ExactBudget, Rational},
    rational_matrix::Matrix,
    rational_witness::{
        certify_rank, check_inverse, check_rank, check_rank_against, check_solution,
        decode_rank_certificate, encode_rank_certificate, ErrorKind, RankCertificate,
        MAX_CERT_TEXT_BYTES,
    },
};
fn b() -> ExactBudget {
    ExactBudget::new(4096, 100_000_000).unwrap()
}
fn m(rows: usize, cols: usize, data: &[i128]) -> Matrix {
    Matrix::new(
        rows,
        cols,
        data.iter().map(|&n| Rational::from_integer(n)).collect(),
        &mut b(),
    )
    .unwrap()
}
fn source() -> Matrix {
    m(2, 3, &[1, 2, 3, 2, 4, 6])
}
#[test]
fn exhaustive_small_ranks_and_kernels_check_without_elimination() {
    for mut code in 0..81 {
        let d: Vec<_> = (0..4)
            .map(|_| {
                let v = code % 3;
                code /= 3;
                v as i128 - 1
            })
            .collect();
        let a = m(2, 2, &d);
        let c = certify_rank(&a, &mut b()).unwrap();
        let checked = check_rank(&c, &mut b()).unwrap();
        let det = d[0] * d[3] - d[1] * d[2];
        let rank = if det != 0 {
            2
        } else if d.iter().any(|&v| v != 0) {
            1
        } else {
            0
        };
        assert_eq!(checked.rank, rank);
        assert_eq!(checked.nullity, 2 - rank);
        assert!(a.mul(&checked.kernel, &mut b()).unwrap().is_zero());
    }
}
#[test]
fn independent_fraction_fixtures_and_transport_replay() {
    let fixed = decode_rank_certificate(
        include_str!("../../../examples/math/rational_rank.qproof"),
        &mut b(),
    )
    .unwrap();
    assert_eq!(
        check_rank_against(&source(), &fixed, &mut b())
            .unwrap()
            .rank,
        1
    );
    let mut count = 0;
    for line in include_str!("fixtures/rational_matrices093.tsv")
        .lines()
        .filter(|s| !s.starts_with('#'))
    {
        let x: Vec<_> = line.split('\t').collect();
        let a = Matrix::parse(
            &format!("{} {}\n{}", x[0], x[1], x[2].replace(',', " ")),
            &mut b(),
        )
        .unwrap();
        let c = certify_rank(&a, &mut b()).unwrap();
        let text = encode_rank_certificate(&c, &mut b()).unwrap();
        let replay = decode_rank_certificate(&text, &mut b()).unwrap();
        assert_eq!(replay, c);
        let checked = check_rank_against(&a, &replay, &mut b()).unwrap();
        let rank = if x[4].is_empty() {
            0
        } else {
            x[4].split(',').count()
        };
        assert_eq!(checked.rank, rank);
        assert_eq!(
            checked
                .kernel
                .entries()
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(","),
            x[6]
        );
        count += 1;
    }
    assert_eq!(count, 50);
}
fn changed(a: &Matrix, i: usize) -> Matrix {
    let mut data = a.entries().to_vec();
    data[i] = data[i].checked_add(&Rational::one(), &mut b()).unwrap();
    Matrix::new(a.rows(), a.cols(), data, &mut b()).unwrap()
}
#[test]
fn every_numerical_entry_mutation_in_the_fixture_is_rejected() {
    let c = certify_rank(&source(), &mut b()).unwrap();
    let mut count = 0;
    for part in 0..7 {
        let a = match part {
            0 => &c.input,
            1 => &c.columns,
            2 => &c.coordinates,
            3 => &c.left,
            4 => &c.right,
            5 => &c.kernel,
            _ => &c.kernel_left,
        };
        for i in 0..a.entries().len() {
            let mut bad = c.clone();
            let a = changed(a, i);
            match part {
                0 => bad.input = a,
                1 => bad.columns = a,
                2 => bad.coordinates = a,
                3 => bad.left = a,
                4 => bad.right = a,
                5 => bad.kernel = a,
                _ => bad.kernel_left = a,
            };
            assert_eq!(
                check_rank(&bad, &mut b()).unwrap_err().kind,
                ErrorKind::Invalid,
                "part {part} entry {i}"
            );
            count += 1;
        }
    }
    assert_eq!(count, 28);
    let mut bad = c.clone();
    bad.rank = usize::MAX;
    assert_eq!(
        check_rank(&bad, &mut b()).unwrap_err().kind,
        ErrorKind::Invalid
    );
    bad = c.clone();
    bad.kernel = m(3, 1, &[0, 0, 0]);
    assert_eq!(
        check_rank(&bad, &mut b()).unwrap_err().kind,
        ErrorKind::Invalid
    );
}
#[test]
fn independent_inverse_and_affine_solution_residuals() {
    let a = source();
    let c = certify_rank(&a, &mut b()).unwrap();
    let rhs = m(2, 2, &[7, 1, 14, 2]);
    let solution = a.solve(&rhs, &mut b()).unwrap();
    let checked = check_solution(&c, &rhs, &solution.particular, &mut b()).unwrap();
    assert_eq!(checked.nullity, 2);
    let bad = changed(&solution.particular, 0);
    assert_eq!(
        check_solution(&c, &rhs, &bad, &mut b()).unwrap_err().kind,
        ErrorKind::Invalid
    );
    let a = Matrix::parse(
        "2 2\n340282366920938463463374607431768211456 1\n1 0",
        &mut b(),
    )
    .unwrap();
    let inverse = a.inverse(&mut b()).unwrap();
    check_inverse(&a, &inverse, &mut b()).unwrap();
    assert_eq!(
        check_inverse(&a, &changed(&inverse, 0), &mut b())
            .unwrap_err()
            .kind,
        ErrorKind::Invalid
    );
    assert!(check_inverse(&source(), &m(3, 2, &[1, 0, 0, 1, 0, 0]), &mut b()).is_err());
}
#[test]
fn strict_codec_and_resource_errors_are_not_false_mathematical_claims() {
    let c = certify_rank(&source(), &mut b()).unwrap();
    let text = encode_rank_certificate(&c, &mut b()).unwrap();
    for bad in [
        "DLM-F2-HOMOLOGY v1".to_string(),
        text.replace("v1", "v9"),
        format!("{text}extra"),
        text.replace("rank 1", "rank 999999999999999999999999999999"),
        text.replace("matrix input 2 3", "matrix input 999 3"),
        text.replace("1/1", "1/0"),
        text.replace("matrix columns", "matrix missing"),
    ] {
        assert!(decode_rank_certificate(&bad, &mut b()).is_err());
    }
    let tokens: Vec<_> = text.split_whitespace().collect();
    for n in 0..tokens.len() {
        assert!(decode_rank_certificate(&tokens[..n].join(" "), &mut b()).is_err());
    }
    let mut high = dlm_core::math::exact::Natural::from_u128(1);
    for _ in 0..4095 {
        high = high.checked_add(&high, &mut b()).unwrap();
    }
    let high = Rational::new(
        false,
        high,
        dlm_core::math::exact::Natural::from_u128(1),
        &mut b(),
    )
    .unwrap();
    let mut oversized = c.clone();
    oversized.input = Matrix::new(32, 32, vec![high; 1024], &mut b()).unwrap();
    assert_eq!(
        encode_rank_certificate(&oversized, &mut b())
            .unwrap_err()
            .kind,
        ErrorKind::UnknownWithinBudget
    );
    assert_eq!(
        decode_rank_certificate(&" ".repeat(MAX_CERT_TEXT_BYTES + 1), &mut b())
            .unwrap_err()
            .kind,
        ErrorKind::UnknownWithinBudget
    );
    assert_eq!(
        check_rank(&c, &mut ExactBudget::new(4096, 0).unwrap())
            .unwrap_err()
            .kind,
        ErrorKind::UnknownWithinBudget
    );
    assert_eq!(
        decode_rank_certificate(&text, &mut ExactBudget::new(4096, 0).unwrap())
            .unwrap_err()
            .kind,
        ErrorKind::UnknownWithinBudget
    );
    let a = Matrix::parse("1 1\n340282366920938463463374607431768211456", &mut b()).unwrap();
    let c = certify_rank(&a, &mut b()).unwrap();
    assert_eq!(
        check_rank(&c, &mut ExactBudget::new(128, 100_000_000).unwrap())
            .unwrap_err()
            .kind,
        ErrorKind::UnknownWithinBudget
    );
}
#[test]
fn zero_dimensions_maximum_axes_and_external_binding_are_explicit() {
    for a in [
        m(0, 0, &[]),
        m(0, 3, &[]),
        m(3, 0, &[]),
        m(2, 3, &[0; 6]),
        Matrix::identity(32, &mut b()).unwrap(),
    ] {
        let c = certify_rank(&a, &mut b()).unwrap();
        let checked = check_rank(&c, &mut b()).unwrap();
        assert_eq!(checked.rank + checked.nullity, a.cols());
        let text = encode_rank_certificate(&c, &mut b()).unwrap();
        check_rank(&decode_rank_certificate(&text, &mut b()).unwrap(), &mut b()).unwrap();
    }
    let c = certify_rank(&source(), &mut b()).unwrap();
    assert_eq!(
        check_rank_against(&m(2, 3, &[1, 2, 3, 2, 4, 7]), &c, &mut b())
            .unwrap_err()
            .kind,
        ErrorKind::Invalid
    );
}
#[test]
fn verifier_source_has_no_solver_calls() {
    let source = include_str!("../src/math/rational_witness.rs");
    for name in [".rref(", ".rank(", ".solve(", ".inverse("] {
        assert!(!source.contains(name), "verifier must not call {name}");
    }
    let _type_check: Option<RankCertificate> = None;
}
