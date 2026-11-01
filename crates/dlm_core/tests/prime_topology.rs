use dlm_core::math::{
    prime::{Matrix, PrimeField},
    prime_topology::ChainComplex,
    topology::SimplicialComplex,
};
fn f(p: u16) -> PrimeField {
    PrimeField::new(p).unwrap()
}
fn m(p: u16, r: usize, c: usize, data: &[u8]) -> Matrix {
    Matrix::new(f(p), r, c, data.to_vec()).unwrap()
}
fn digits(mut code: usize, p: usize, n: usize) -> Vec<u8> {
    (0..n)
        .map(|_| {
            let a = (code % p) as u8;
            code /= p;
            a
        })
        .collect()
}
#[test]
fn exhaustive_odd_field_matrices_against_enumerated_solutions() {
    for (p, rows, cols) in [(3u16, 2usize, 3usize), (5, 2, 2)] {
        for code in 0..(p as usize).pow((rows * cols) as u32) {
            let a = m(p, rows, cols, &digits(code, p as usize, rows * cols));
            let k = a.kernel();
            let solutions: Vec<_> = (0..(p as usize).pow(cols as u32))
                .filter(|&v| {
                    a.mul(&m(p, cols, 1, &digits(v, p as usize, cols)))
                        .unwrap()
                        .is_zero()
                })
                .collect();
            let mut span = Vec::new();
            for v in 0..(p as usize).pow(k.cols() as u32) {
                let x = k
                    .mul(&m(p, k.cols(), 1, &digits(v, p as usize, k.cols())))
                    .unwrap();
                span.push(
                    x.entries()
                        .iter()
                        .rev()
                        .fold(0, |s, &v| s * p as usize + v as usize),
                );
            }
            span.sort_unstable();
            assert_eq!(span, solutions);
            assert_eq!(k.rank(), k.cols());
            assert_eq!(a.rank() + k.cols(), cols);
            assert_eq!(a.image().rank(), a.rank());
            assert!(a.mul(&k).unwrap().is_zero());
        }
    }
}
#[test]
fn all_binary_three_by_three_match_existing_f2() {
    for code in 0..512 {
        let data = digits(code, 2, 9);
        let a = m(2, 3, 3, &data);
        let old = dlm_core::math::f2::Matrix::new(3, 3, data).unwrap();
        assert_eq!(a.rank(), old.rank());
        assert_eq!(a.kernel().entries(), old.kernel().entries());
        assert_eq!(a.rref().0.entries(), old.rref().0.entries());
    }
}
#[test]
fn nonunit_pivots_and_scalar_field_operations() {
    let p = f(251);
    for a in 1..251 {
        assert_eq!(p.multiply(a, p.inverse(a).unwrap()).unwrap(), 1);
    }
    assert_eq!(p.reduce_integer(-1), 250);
    assert_eq!(p.reduce_integer(i128::MIN), i128::MIN.rem_euclid(251) as u8);
    assert!(p.inverse(0).is_err());
    assert!(p.element(251).is_err());
    assert!(p.add(255, 0).is_err());
    for bad in [0, 1, 4, 9, 252, 65535] {
        assert!(PrimeField::new(bad).is_err());
    }
    let a = m(5, 1, 2, &[2, 1]);
    assert_eq!(a.rref().0.entries(), &[1, 3]);
    assert_eq!(a.kernel().entries(), &[2, 1]);
    assert_eq!(a.tensor(&Matrix::identity(f(5), 1).unwrap()).unwrap(), a);
    assert!(a.add(&a).unwrap().entries() == [4, 2]);
}
#[test]
fn boundaries_carry_orientation_and_unsigned_triangle_is_rejected() {
    let s = SimplicialComplex::from_facets(&[vec![2, 0, 1]]).unwrap();
    let c = s.chains_over(f(3)).unwrap();
    assert_eq!(c.boundaries()[1].entries(), &[2, 2, 0, 1, 0, 2, 0, 1, 1]);
    assert_eq!(c.boundaries()[2].entries(), &[1, 2, 1]);
    assert!(c.boundaries()[1].mul(&c.boundaries()[2]).unwrap().is_zero());
    assert!(ChainComplex::new(
        f(3),
        vec![3, 3, 1],
        vec![
            m(3, 0, 3, &[]),
            m(3, 3, 3, &[1, 1, 0, 1, 0, 1, 0, 1, 1]),
            m(3, 3, 1, &[1, 1, 1])
        ]
    )
    .is_err());
}
fn betti(s: &SimplicialComplex, p: u16) -> Vec<usize> {
    let c = s.chains_over(f(p)).unwrap();
    (0..c.dimensions().len())
        .map(|n| {
            let h = c.homology(n).unwrap();
            let co = c.cohomology(n).unwrap();
            assert_eq!(h.betti, co.betti);
            assert!(c.boundaries()[n].mul(&h.representatives).unwrap().is_zero());
            assert_eq!(h.representatives.rank(), h.betti);
            let up = if n + 1 < c.dimensions().len() {
                c.boundaries()[n + 1].transpose()
            } else {
                Matrix::zero(f(p), 0, c.dimensions()[n]).unwrap()
            };
            assert!(up.mul(&co.representatives).unwrap().is_zero());
            h.betti
        })
        .collect()
}
#[test]
fn circle_disk_sphere_torus_and_projective_plane_distinguish_characteristic() {
    for p in [2, 3, 5, 251] {
        for (name, expected) in [
            ("circle", vec![1, 1]),
            ("disk", vec![1, 0, 0]),
            ("sphere", vec![1, 0, 1]),
            ("torus", vec![1, 2, 1]),
            (
                "projective_plane",
                if p == 2 { vec![1, 1, 1] } else { vec![1, 0, 0] },
            ),
        ] {
            let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join(format!("../../examples/math/{name}.simp"));
            let s = SimplicialComplex::parse(&std::fs::read_to_string(path).unwrap()).unwrap();
            assert_eq!(betti(&s, p), expected, "{name} F{p}");
        }
    }
}
#[test]
fn chain_maps_validate_shapes_fields_and_commutation() {
    let s = SimplicialComplex::parse("0 1\n1 2\n0 2").unwrap();
    let c = s.chains_over(f(3)).unwrap();
    let ids: Vec<_> = c
        .dimensions()
        .iter()
        .map(|&n| Matrix::identity(f(3), n).unwrap())
        .collect();
    assert_eq!(c.induced_ranks(&c, &ids).unwrap(), vec![1, 1]);
    let zeros: Vec<_> = c
        .dimensions()
        .iter()
        .map(|&n| Matrix::zero(f(3), n, n).unwrap())
        .collect();
    assert_eq!(c.induced_ranks(&c, &zeros).unwrap(), vec![0, 0]);
    let mut bad = ids.clone();
    bad[0] = zeros[0].clone();
    assert!(c.induced_ranks(&c, &bad).is_err());
    assert!(c
        .induced_ranks(&s.chains_over(f(5)).unwrap(), &ids)
        .is_err());
    let empty = SimplicialComplex::parse("")
        .unwrap()
        .chains_over(f(3))
        .unwrap();
    assert_eq!(empty.field(), f(3));
    assert!(empty.homology(0).is_err());
    assert!(empty
        .induced_ranks(&s.chains_over(f(5)).unwrap(), &[])
        .is_err());
    assert!(ChainComplex::new(f(3), vec![0], vec![m(5, 0, 0, &[])]).is_err());
    assert!(ChainComplex::new(f(3), vec![0; 17], vec![]).is_err());
}
#[test]
fn public_bounds_and_private_augmented_basis_extension() {
    assert!(Matrix::zero(f(3), 257, 0).is_err());
    assert!(Matrix::new(f(3), 1, 1, vec![3]).is_err());
    assert!(Matrix::new(f(3), 2, 2, vec![1]).is_err());
    let a = Matrix::identity(f(3), 256).unwrap();
    assert!(a.hstack(&a).is_err());
    assert_eq!(a.extend_basis(&a).unwrap().cols(), 0);
    assert_eq!(
        Matrix::zero(f(3), 256, 0)
            .unwrap()
            .extend_basis(&a)
            .unwrap(),
        a
    );
    let e = m(3, 0, 0, &[]);
    let wrong = m(5, 0, 0, &[]);
    assert!(e.add(&wrong).is_err());
    assert!(e.mul(&wrong).is_err());
    assert!(e.hstack(&wrong).is_err());
    assert!(e.tensor(&wrong).is_err());
    assert!(e.extend_basis(&wrong).is_err());
    assert_eq!(m(3, 0, 3, &[]).kernel(), Matrix::identity(f(3), 3).unwrap());
    assert_eq!(m(3, 3, 0, &[]).kernel().cols(), 0);
    assert!(a.select_columns(&[256]).is_err());
    assert!(a.tensor(&a).is_err());
}
