use dlm_core::math::{
    category::Category,
    f2::Matrix,
    topology::{ChainComplex, SimplicialComplex},
};
fn m(r: usize, c: usize, d: &[u8]) -> Matrix {
    Matrix::new(r, c, d.to_vec()).unwrap()
}
fn betti(facets: &[Vec<u32>]) -> Vec<usize> {
    let c = SimplicialComplex::from_facets(facets)
        .unwrap()
        .chains()
        .unwrap();
    (0..c.dimensions().len())
        .map(|n| {
            let h = c.homology(n).unwrap();
            assert_eq!(h.betti, c.cohomology(n).unwrap().betti);
            assert!(c.boundaries()[n].mul(&h.representatives).unwrap().is_zero());
            h.betti
        })
        .collect()
}
#[test]
fn exhaustive_small_matrices_against_enumerated_kernel() {
    for bits in 0u16..512 {
        let a = m(
            3,
            3,
            &(0..9).map(|i| ((bits >> i) & 1) as u8).collect::<Vec<_>>(),
        );
        let k = a.kernel();
        assert!(a.mul(&k).unwrap().is_zero());
        assert_eq!(k.rank(), k.cols());
        let zeros = (0..8)
            .filter(|v| {
                let x = m(
                    3,
                    1,
                    &(0..3).map(|i| ((v >> i) & 1) as u8).collect::<Vec<_>>(),
                );
                a.mul(&x).unwrap().is_zero()
            })
            .count();
        assert_eq!(zeros, 1 << k.cols());
        assert_eq!(a.rank() + k.cols(), 3);
        assert_eq!(a.image().rank(), a.rank());
        assert_eq!(a.transpose().transpose(), a);
    }
}
#[test]
fn matrix_shapes_and_exact_arithmetic() {
    assert!(Matrix::new(1, 1, vec![2]).is_err());
    assert!(Matrix::zero(257, 0).is_err());
    assert!(Matrix::new(2, 2, vec![0]).is_err());
    assert!(Matrix::zero(2, 3)
        .unwrap()
        .mul(&Matrix::identity(2).unwrap())
        .is_err());
    let a = m(2, 2, &[1, 1, 0, 1]);
    assert!(a.add(&a).unwrap().is_zero());
    assert_eq!(a.mul(&a).unwrap(), Matrix::identity(2).unwrap());
    assert_eq!(a.tensor(&Matrix::identity(1).unwrap()).unwrap(), a);
    assert!(Matrix::identity(17)
        .unwrap()
        .tensor(&Matrix::identity(17).unwrap())
        .is_err());
    assert_eq!(
        Matrix::zero(0, 3).unwrap().kernel(),
        Matrix::identity(3).unwrap()
    );
    assert_eq!(Matrix::zero(3, 0).unwrap().kernel().cols(), 0);
}
#[test]
fn point_circle_disk_sphere_and_disconnected() {
    assert_eq!(betti(&[vec![0]]), vec![1]);
    assert_eq!(betti(&[vec![0], vec![7]]), vec![2]);
    assert_eq!(betti(&[vec![0, 1], vec![1, 2], vec![0, 2]]), vec![1, 1]);
    assert_eq!(betti(&[vec![0, 1, 2]]), vec![1, 0, 0]);
    assert_eq!(
        betti(&[vec![0, 1, 2], vec![0, 1, 3], vec![0, 2, 3], vec![1, 2, 3]]),
        vec![1, 0, 1]
    );
    assert_eq!(betti(&[]), Vec::<usize>::new());
}
#[test]
fn torus_and_projective_plane_mod_two() {
    let mut facets = Vec::new();
    let v = |i: u32, j: u32| (i % 3) * 3 + j % 3;
    for i in 0..3 {
        for j in 0..3 {
            facets.push(vec![v(i, j), v(i + 1, j), v(i + 1, j + 1)]);
            facets.push(vec![v(i, j), v(i, j + 1), v(i + 1, j + 1)]);
        }
    }
    assert_eq!(betti(&facets), vec![1, 2, 1]);
    let rp2 = vec![
        vec![1, 2, 3],
        vec![1, 2, 4],
        vec![1, 3, 5],
        vec![1, 4, 6],
        vec![1, 5, 6],
        vec![2, 3, 6],
        vec![2, 4, 5],
        vec![2, 5, 6],
        vec![3, 4, 5],
        vec![3, 4, 6],
    ];
    assert_eq!(betti(&rp2), vec![1, 1, 1]);
}
#[test]
fn invalid_complexes_and_budgets() {
    assert!(SimplicialComplex::from_facets(&[vec![1, 1]]).is_err());
    assert!(SimplicialComplex::from_facets(&[vec![]]).is_err());
    assert!(SimplicialComplex::from_facets(&[(0..16).collect()]).is_err());
    assert!(SimplicialComplex::parse("1 nope").is_err());
    assert!(SimplicialComplex::parse(&" ".repeat(65537)).is_err());
    assert!(ChainComplex::new(
        vec![1, 1, 1],
        vec![m(0, 1, &[]), m(1, 1, &[1]), m(1, 1, &[1])]
    )
    .is_err());
    assert!(ChainComplex::new(vec![1], vec![Matrix::identity(1).unwrap()]).is_err());
    let c = SimplicialComplex::from_facets(&[])
        .unwrap()
        .chains()
        .unwrap();
    assert!(c.homology(0).is_err());
    assert!(c.cohomology(0).is_err());
}
#[test]
fn chain_maps_identity_zero_and_rejection() {
    let c = SimplicialComplex::parse("0 1\n1 2\n0 2")
        .unwrap()
        .chains()
        .unwrap();
    let ids = c
        .dimensions()
        .iter()
        .map(|&n| Matrix::identity(n).unwrap())
        .collect::<Vec<_>>();
    assert_eq!(c.induced_ranks(&c, &ids).unwrap(), vec![1, 1]);
    let zero = c
        .dimensions()
        .iter()
        .map(|&n| Matrix::zero(n, n).unwrap())
        .collect::<Vec<_>>();
    assert_eq!(c.induced_ranks(&c, &zero).unwrap(), vec![0, 0]);
    let mut bad = ids;
    bad[0] = Matrix::zero(3, 3).unwrap();
    assert!(c.induced_ranks(&c, &bad).is_err());
}
fn c2() -> Category {
    Category::new(
        1,
        vec![0, 0],
        vec![0, 0],
        vec![0],
        vec![Some(0), Some(1), Some(1), Some(0)],
    )
    .unwrap()
}
#[test]
fn categories_and_functor_laws() {
    let c = c2();
    assert_eq!(c.objects(), 1);
    assert_eq!(c.arrows(), 2);
    assert!(c.check_functor(&c, &[0], &[0, 1]).is_ok());
    assert!(c.check_functor(&c, &[0], &[0, 0]).is_ok());
    assert!(c.check_functor(&c, &[0], &[1, 0]).is_err());
    assert!(c.check_functor(&c, &[1], &[0, 1]).is_err());
    assert_eq!(c.comp(usize::MAX, 0), None);
    assert!(Category::new(1, vec![0], vec![0], vec![0], vec![None]).is_err());
    // Unital but nonassociative multiplication: (a*a)*b != a*(a*b).
    assert!(Category::new(
        1,
        vec![0; 3],
        vec![0; 3],
        vec![0],
        vec![
            Some(0),
            Some(1),
            Some(2),
            Some(1),
            Some(2),
            Some(0),
            Some(2),
            Some(0),
            Some(0)
        ]
    )
    .is_err());
}
#[test]
fn tensor_complex_kunneth_examples() {
    let circle = SimplicialComplex::parse("0 1\n1 2\n0 2")
        .unwrap()
        .chains()
        .unwrap();
    let product = circle.tensor(&circle).unwrap();
    assert_eq!(
        (0..3)
            .map(|n| product.homology(n).unwrap().betti)
            .collect::<Vec<_>>(),
        vec![1, 2, 1]
    );
    let disk = SimplicialComplex::parse("0 1 2").unwrap().chains().unwrap();
    let cylinder = circle.tensor(&disk).unwrap();
    assert_eq!(
        (0..4)
            .map(|n| cylinder.homology(n).unwrap().betti)
            .collect::<Vec<_>>(),
        vec![1, 1, 0, 0]
    );
    let empty = ChainComplex::new(vec![], vec![]).unwrap();
    assert!(empty.tensor(&circle).unwrap().dimensions().is_empty());
}
#[test]
fn naturality_is_checked() {
    let c = c2();
    assert!(c
        .check_natural_transformation(&c, &[0], &[0, 1], &[0], &[0, 1], &[1])
        .is_ok());
    assert!(c
        .check_natural_transformation(&c, &[0], &[0, 1], &[0], &[0, 0], &[1])
        .is_err());
    assert!(c
        .check_natural_transformation(&c, &[0], &[0, 1], &[0], &[0, 1], &[9])
        .is_err());
}

#[test]
fn graph_euler_characteristic_and_full_rank_boundary_cases() {
    let edges = [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)];
    for mask in 0..64 {
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
        let b0 = c.homology(0).unwrap().betti as isize;
        let b1 = if c.dimensions().len() > 1 {
            c.homology(1).unwrap().betti as isize
        } else {
            0
        };
        assert_eq!(b0 - b1, 4 - (mask as u32).count_ones() as isize);
    }
    let c = ChainComplex::new(
        vec![256, 256],
        vec![
            Matrix::zero(0, 256).unwrap(),
            Matrix::identity(256).unwrap(),
        ],
    )
    .unwrap();
    assert_eq!(c.homology(0).unwrap().betti, 0);
    assert_eq!(c.homology(1).unwrap().betti, 0);
    assert_eq!(c.cohomology(0).unwrap().betti, 0);
}

#[test]
fn walking_arrow_category_domains_and_endpoints() {
    // arrows id_0, id_1, u:0->1
    let table = vec![
        Some(0),
        None,
        None,
        None,
        Some(1),
        Some(2),
        Some(2),
        None,
        None,
    ];
    let c = Category::new(2, vec![0, 1, 0], vec![0, 1, 1], vec![0, 1], table.clone()).unwrap();
    assert!(c.check_functor(&c, &[0, 1], &[0, 1, 2]).is_ok());
    let mut bad = table.clone();
    bad[0] = Some(2);
    assert!(Category::new(2, vec![0, 1, 0], vec![0, 1, 1], vec![0, 1], bad).is_err());
    let mut bad = table;
    bad[1] = Some(0);
    assert!(Category::new(2, vec![0, 1, 0], vec![0, 1, 1], vec![0, 1], bad).is_err());
    assert!(Category::new(0, vec![], vec![], vec![], vec![]).is_ok());
}
