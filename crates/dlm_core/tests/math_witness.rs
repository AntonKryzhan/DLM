use dlm_core::math::{
    f2::Matrix,
    topology::{ChainComplex, SimplicialComplex},
    witness::*,
};
fn matrix(r: usize, c: usize, bits: &[u8]) -> Matrix {
    Matrix::new(r, c, bits.to_vec()).unwrap()
}
fn flip(a: &Matrix) -> Matrix {
    let mut data = a.entries().to_vec();
    data[0] ^= 1;
    matrix(a.rows(), a.cols(), &data)
}
fn circle() -> ChainComplex {
    SimplicialComplex::parse("0 1\n1 2\n0 2")
        .unwrap()
        .chains()
        .unwrap()
}
fn budget() -> WitnessBudget {
    WitnessBudget::default()
}
#[test]
fn rank_certificates_exhaust_all_binary_three_by_three_matrices() {
    for bits in 0..512 {
        let a = matrix(
            3,
            3,
            &(0..9).map(|i| ((bits >> i) & 1) as u8).collect::<Vec<_>>(),
        );
        let w = certify_rank(&a).unwrap();
        assert_eq!(check_rank(&a, &w, budget()).unwrap(), a.rank());
        let mut bad = w.clone();
        bad.left_inverse = flip(&bad.left_inverse);
        assert!(check_rank(&a, &bad, budget()).is_err());
    }
    for (r, c) in [(0, 0), (0, 3), (3, 0), (2, 3), (3, 2)] {
        let a = Matrix::zero(r, c).unwrap();
        let w = certify_rank(&a).unwrap();
        assert_eq!(check_rank(&a, &w, budget()).unwrap(), 0);
    }
}
#[test]
fn rank_witness_rejects_forged_reduction_and_pivots() {
    let a = matrix(3, 3, &[1, 1, 0, 1, 1, 1, 0, 0, 1]);
    let w = certify_rank(&a).unwrap();
    let mut bad = w.clone();
    bad.reduced = flip(&w.reduced);
    assert!(check_rank(&a, &bad, budget()).is_err());
    let mut bad = w.clone();
    bad.left = Matrix::zero(3, 3).unwrap();
    assert!(check_rank(&a, &bad, budget()).is_err());
    let mut bad = w.clone();
    bad.pivots.clear();
    assert!(check_rank(&a, &bad, budget()).is_err());
    let mut bad = w.clone();
    bad.pivots = vec![0, 0];
    assert!(check_rank(&a, &bad, budget()).is_err());
    let mut bad = w.clone();
    bad.pivots = vec![usize::MAX];
    assert!(check_rank(&a, &bad, budget()).is_err());
    let mut bad = w;
    bad.left_inverse = Matrix::identity(2).unwrap();
    assert!(check_rank(&a, &bad, budget()).is_err());
}
#[test]
fn standalone_homology_roundtrip_and_external_claim_binding() {
    for text in [
        "",
        "0",
        "0\n1",
        "0 1\n1 2\n0 2",
        "0 1 2",
        "0 1 2\n0 1 3\n0 2 3\n1 2 3",
    ] {
        let c = SimplicialComplex::parse(text).unwrap().chains().unwrap();
        let cert = certify_homology(&c, budget()).unwrap();
        let encoded = encode_homology_certificate(&cert, budget()).unwrap();
        let decoded = decode_homology_certificate(&encoded, budget()).unwrap();
        assert_eq!(cert, decoded);
        let checked = check_homology_against(&c, &decoded, budget()).unwrap();
        assert_eq!(
            checked.betti,
            (0..c.dimensions().len())
                .map(|n| c.homology(n).unwrap().betti)
                .collect::<Vec<_>>()
        );
        assert_eq!(
            encode_homology_certificate(&decoded, budget()).unwrap(),
            encoded
        );
    }
    let c = circle();
    let cert = certify_homology(&c, budget()).unwrap();
    let disk = SimplicialComplex::parse("0 1 2").unwrap().chains().unwrap();
    assert!(check_homology_against(&disk, &cert, budget()).is_err());
}
#[test]
fn homology_mutations_reject_false_cycles_and_dependent_representatives() {
    let c = circle();
    let cert = certify_homology(&c, budget()).unwrap();
    let mut bad = cert.clone();
    bad.groups[1].representatives = matrix(3, 1, &[1, 0, 0]);
    // Refresh the quotient witness: a valid column-rank certificate alone is insufficient.
    bad.groups[1].quotient = certify_rank(&bad.groups[1].representatives).unwrap();
    assert!(check_homology(&bad, budget())
        .unwrap_err()
        .message
        .contains("not a cycle"));
    let mut bad = cert.clone();
    bad.groups[1].representatives = Matrix::zero(3, 1).unwrap();
    bad.groups[1].quotient = certify_rank(&bad.groups[1].representatives).unwrap();
    assert!(check_homology(&bad, budget())
        .unwrap_err()
        .message
        .contains("dependent"));
    // A nonzero cycle which is a boundary cannot represent H_0.
    let mut bad = cert.clone();
    bad.groups[0].representatives = matrix(3, 1, &[1, 1, 0]);
    let incoming = c.boundaries()[1]
        .select_columns(&bad.groups[0].incoming.pivots)
        .unwrap();
    bad.groups[0].quotient =
        certify_rank(&incoming.hstack(&bad.groups[0].representatives).unwrap()).unwrap();
    assert!(check_homology(&bad, budget())
        .unwrap_err()
        .message
        .contains("dependent"));
    let mut bad = cert.clone();
    bad.groups[0].degree = 1;
    assert!(check_homology(&bad, budget()).is_err());
    let mut bad = cert.clone();
    bad.groups[1].representatives = Matrix::zero(3, 0).unwrap();
    assert!(check_homology(&bad, budget()).is_err());
    let mut bad = cert;
    bad.boundaries[1] = Matrix::identity(3).unwrap();
    assert!(check_homology(&bad, budget()).is_err());
}
#[test]
fn malformed_chain_law_is_checked_before_any_rank_claim() {
    let mut cert = certify_homology(&circle(), budget()).unwrap();
    cert.boundaries[0] = matrix(0, 1, &[]);
    assert!(check_homology(&cert, budget()).is_err());
    let mut cert = certify_homology(&circle(), budget()).unwrap();
    cert.boundaries.push(matrix(3, 1, &[1, 0, 0]));
    cert.groups.push(cert.groups[0].clone());
    assert!(check_homology(&cert, budget())
        .unwrap_err()
        .message
        .contains("squared"));
}
#[test]
fn budgets_report_unknown_and_do_not_return_a_checked_result() {
    let c = circle();
    let cert = certify_homology(&c, budget()).unwrap();
    let text = encode_homology_certificate(&cert, budget()).unwrap();
    let zero_work = WitnessBudget {
        max_work: 0,
        ..budget()
    };
    assert_eq!(
        check_homology(&cert, zero_work).unwrap_err().kind,
        WitnessErrorKind::UnknownWithinBudget
    );
    let zero_matrices = WitnessBudget {
        max_matrices: 0,
        ..budget()
    };
    assert_eq!(
        check_homology(&cert, zero_matrices).unwrap_err().kind,
        WitnessErrorKind::UnknownWithinBudget
    );
    assert_eq!(
        decode_homology_certificate(&text, zero_matrices)
            .unwrap_err()
            .kind,
        WitnessErrorKind::UnknownWithinBudget
    );
    let tiny = WitnessBudget {
        max_text_bytes: 8,
        ..budget()
    };
    assert_eq!(
        decode_homology_certificate(&text, tiny).unwrap_err().kind,
        WitnessErrorKind::UnknownWithinBudget
    );
    assert_eq!(
        encode_homology_certificate(&cert, tiny).unwrap_err().kind,
        WitnessErrorKind::UnknownWithinBudget
    );
}
#[test]
fn strict_codec_rejects_truncation_counts_bits_and_trailing_data() {
    let cert = certify_homology(&circle(), budget()).unwrap();
    let text = encode_homology_certificate(&cert, budget()).unwrap();
    for i in 0..text.len() {
        if text.as_bytes()[i].is_ascii_whitespace() && !text[..i].trim_end().ends_with("end") {
            assert!(decode_homology_certificate(&text[..i], budget()).is_err());
        }
    }
    for bad in [
        text.replace("v1", "v9"),
        format!("{text}extra"),
        text.replacen(
            "boundaries 2",
            "boundaries 9999999999999999999999999999999999",
            1,
        ),
        text.replacen("matrix 0 3 -", "matrix 257 3 -", 1),
        text.replacen("matrix 0 3 -", "matrix 0 3 0", 1),
        text.replacen("matrix 3 3 110101011", "matrix 3 3 210101011", 1),
    ] {
        assert_ne!(bad, text);
        assert!(decode_homology_certificate(&bad, budget()).is_err());
    }
}
#[test]
fn graph_certificates_exhaust_all_graphs_on_four_vertices() {
    let edges = [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)];
    for mask in 0..64u32 {
        let mut facets = vec![vec![0], vec![1], vec![2], vec![3]];
        for (i, &(a, b)) in edges.iter().enumerate() {
            if mask & (1 << i) != 0 {
                facets.push(vec![a, b]);
            }
        }
        let c = SimplicialComplex::from_facets(&facets)
            .unwrap()
            .chains()
            .unwrap();
        let cert = certify_homology(&c, budget()).unwrap();
        let checked = check_homology(&cert, budget()).unwrap();
        let b1 = checked.betti.get(1).copied().unwrap_or(0);
        assert_eq!(
            checked.betti[0] as isize - b1 as isize,
            4 - mask.count_ones() as isize
        );
    }
}

#[test]
fn every_single_bit_mutation_of_a_valid_circle_certificate_is_rejected() {
    let cert = certify_homology(&circle(), budget()).unwrap();
    let text = encode_homology_certificate(&cert, budget()).unwrap();
    let lines = text.lines().map(str::to_string).collect::<Vec<_>>();
    let mut cases = 0;
    for (i, line) in lines.iter().enumerate() {
        if !line.starts_with("matrix ") {
            continue;
        }
        let parts = line.split_whitespace().collect::<Vec<_>>();
        if parts[3] == "-" {
            continue;
        }
        for bit in 0..parts[3].len() {
            let mut data = parts[3].as_bytes().to_vec();
            data[bit] = if data[bit] == b'0' { b'1' } else { b'0' };
            let mut changed = lines.clone();
            changed[i] = format!(
                "matrix {} {} {}",
                parts[1],
                parts[2],
                String::from_utf8(data).unwrap()
            );
            let decoded =
                decode_homology_certificate(&(changed.join("\n") + "\n"), budget()).unwrap();
            assert!(
                check_homology(&decoded, budget()).is_err(),
                "line {i}, bit {bit}"
            );
            cases += 1;
        }
    }
    assert!(cases > 100);
}

#[test]
fn rank_witness_handles_the_maximum_axis_and_conservative_work_limit() {
    let a = Matrix::identity(256).unwrap();
    let w = certify_rank(&a).unwrap();
    assert_eq!(check_rank(&a, &w, budget()).unwrap(), 256);
    let small = WitnessBudget {
        max_work: 10_000_000,
        ..budget()
    };
    assert_eq!(
        check_rank(&a, &w, small).unwrap_err().kind,
        WitnessErrorKind::UnknownWithinBudget
    );
}
