use dlm_core::math::algebra::*;
use std::collections::BTreeSet;
fn permute(n: usize, unit: usize, table: &[usize], labels: &[usize]) -> (usize, Vec<usize>) {
    let mut out = vec![0; n * n];
    for a in 0..n {
        for b in 0..n {
            out[labels[a] * n + labels[b]] = labels[table[a * n + b]];
        }
    }
    (labels[unit], out)
}
fn s3() -> FiniteGroup {
    let ps = [
        [0, 1, 2],
        [0, 2, 1],
        [1, 0, 2],
        [1, 2, 0],
        [2, 0, 1],
        [2, 1, 0],
    ];
    let mut table = Vec::new();
    for a in ps {
        for b in ps {
            let p = [a[b[0]], a[b[1]], a[b[2]]];
            table.push(ps.iter().position(|&x| x == p).unwrap());
        }
    }
    FiniteGroup::new(6, 0, table).unwrap()
}
fn gf4() -> FiniteRing {
    let mut times = Vec::new();
    for a in 0..4 {
        for b in 0..4 {
            let mut p = 0;
            for i in 0..2 {
                for j in 0..2 {
                    p ^= (((a >> i) & 1) * ((b >> j) & 1)) << (i + j);
                }
            }
            if p & 4 != 0 {
                p ^= 7;
            }
            times.push(p);
        }
    }
    FiniteRing::new(4, 0, 1, (0..16).map(|i| (i / 4) ^ (i % 4)).collect(), times).unwrap()
}
fn matrix_ring() -> FiniteRing {
    let mut times = Vec::new();
    for a in 0..16 {
        for b in 0..16 {
            let mut p = 0;
            for i in 0..2 {
                for j in 0..2 {
                    let mut bit = 0;
                    for k in 0..2 {
                        bit ^= ((a >> (2 * i + k)) & 1) * ((b >> (2 * k + j)) & 1);
                    }
                    p |= bit << (2 * i + j);
                }
            }
            times.push(p);
        }
    }
    FiniteRing::new(
        16,
        0,
        9,
        (0..256).map(|i| (i / 16) ^ (i % 16)).collect(),
        times,
    )
    .unwrap()
}
#[test]
fn all_tables_of_orders_two_and_three_match_relabelings_of_cyclic_groups() {
    for n in [2usize, 3] {
        let canonical = FiniteGroup::cyclic(n).unwrap();
        let mut expected = BTreeSet::new();
        let permutations = if n == 2 {
            vec![vec![0, 1], vec![1, 0]]
        } else {
            vec![
                vec![0, 1, 2],
                vec![0, 2, 1],
                vec![1, 0, 2],
                vec![1, 2, 0],
                vec![2, 0, 1],
                vec![2, 1, 0],
            ]
        };
        for labels in permutations {
            expected.insert(permute(n, 0, canonical.table(), &labels));
        }
        let mut accepted = BTreeSet::new();
        for code in 0..n.pow((n * n) as u32) {
            let mut v = code;
            let table = (0..n * n)
                .map(|_| {
                    let d = v % n;
                    v /= n;
                    d
                })
                .collect::<Vec<_>>();
            for unit in 0..n {
                if FiniteGroup::new(n, unit, table.clone()).is_ok() {
                    accepted.insert((unit, table.clone()));
                }
            }
        }
        assert_eq!(accepted, expected);
    }
}
#[test]
fn group_limits_and_invalid_tables_fail_without_indexing_or_overflow() {
    for n in [0, 65, usize::MAX] {
        assert!(FiniteGroup::cyclic(n).is_err());
    }
    assert!(FiniteGroup::new(2, 2, vec![0, 1, 1, 0]).is_err());
    assert!(FiniteGroup::new(2, 0, vec![0]).is_err());
    assert!(FiniteGroup::new(2, 0, vec![0, 1, 1, usize::MAX]).is_err());
    assert!(FiniteGroup::new(2, 0, vec![0, 1, 1, 1]).is_err()); // monoid, no inverse
    let g = FiniteGroup::cyclic(64).unwrap();
    assert_eq!(g.multiply(63, 63).unwrap(), 62);
    assert!(g.multiply(usize::MAX, 0).is_err());
    assert!(g.inverse(64).is_err());
}
#[test]
fn nonabelian_subgroups_quotients_and_category_bridge() {
    let g = s3();
    assert!(!g.is_abelian());
    let a3 = g.subgroup(&[4, 0, 3]).unwrap();
    assert_eq!(a3.embedding(), &[0, 3, 4]);
    assert!(a3.is_normal());
    let quotient = g.quotient(&[0, 3, 4]).unwrap();
    assert_eq!(quotient.group(), &FiniteGroup::cyclic(2).unwrap());
    assert_eq!(quotient.projection(), &[0, 1, 1, 0, 0, 1]);
    assert!(!g.subgroup(&[0, 1]).unwrap().is_normal());
    assert!(g.quotient(&[0, 1]).is_err());
    for bad in [vec![], vec![1], vec![0, 0], vec![0, 6], vec![0, 1, 3]] {
        assert!(g.subgroup(&bad).is_err());
    }
    let category = g.as_category().unwrap();
    assert_eq!(category.objects(), 1);
    assert_eq!(category.arrows(), 6);
    let target = FiniteGroup::cyclic(2).unwrap().as_category().unwrap();
    assert!(category
        .check_functor(&target, &[0], quotient.projection())
        .is_ok());
}
#[test]
fn kernel_image_and_first_isomorphism_are_constructed_and_rechecked() {
    let g = FiniteGroup::cyclic(4).unwrap();
    let h = FiniteGroup::cyclic(2).unwrap();
    let f = g.homomorphism(&h, &[0, 1, 0, 1]).unwrap();
    assert_eq!(f.kernel().embedding(), &[0, 2]);
    assert_eq!(f.image().embedding(), &[0, 1]);
    assert!(!f.is_injective());
    assert!(f.is_surjective());
    let iso = f.first_isomorphism().unwrap();
    assert_eq!(iso.quotient().group(), iso.image().group());
    assert_eq!(iso.induced(), &[0, 1]);
    let sign = s3().homomorphism(&h, &[0, 1, 1, 0, 0, 1]).unwrap();
    assert_eq!(sign.first_isomorphism().unwrap().induced(), &[0, 1]);
    assert!(g.homomorphism(&h, &[0, 1, 1, 1]).is_err());
    assert!(g.homomorphism(&h, &[1, 0, 1, 0]).is_err());
    assert!(g.homomorphism(&h, &[0, 1, 2, 0]).is_err());
    let trivial = g.homomorphism(&h, &[0; 4]).unwrap();
    assert_eq!(
        trivial
            .first_isomorphism()
            .unwrap()
            .quotient()
            .group()
            .order(),
        1
    );
}
#[test]
fn identity_and_zero_labels_are_not_assumed_to_be_zero() {
    let labels = [2, 0, 1];
    let c = FiniteGroup::cyclic(3).unwrap();
    let (e, t) = permute(3, 0, c.table(), &labels);
    let g = FiniteGroup::new(3, e, t).unwrap();
    assert_eq!(g.identity(), 2);
    assert_eq!(g.quotient(&[2]).unwrap().projection(), &[0, 1, 2]);
    assert!(g.homomorphism(&g, &[0, 1, 2]).unwrap().is_injective());
    let r = FiniteRing::residues(3).unwrap();
    let (_, add) = permute(3, 0, r.addition_table(), &labels);
    let (_, mul) = permute(3, 0, r.multiplication_table(), &labels);
    let relabeled = FiniteRing::new(3, labels[0], labels[1], add, mul).unwrap();
    assert!(relabeled.field().is_ok());
}
#[test]
fn finite_fields_include_extension_fields_and_reject_zero_divisors() {
    for n in 1..=16 {
        let r = FiniteRing::residues(n).unwrap();
        let prime = [2, 3, 5, 7, 11, 13].contains(&n);
        assert_eq!(r.field().is_ok(), prime, "Z/{n}");
    }
    let field = gf4().field().unwrap();
    assert_eq!(field.inverse(2).unwrap(), 3);
    assert_eq!(field.divide(3, 2).unwrap(), 2);
    for x in 1..4 {
        assert_eq!(
            field.ring().multiply(x, field.inverse(x).unwrap()).unwrap(),
            1
        );
    }
    assert!(field.inverse(0).is_err());
    assert!(field.inverse(4).is_err());
    assert!(FiniteField::prime(9).is_err());
}
#[test]
fn noncommutative_ring_and_left_regular_module() {
    let r = matrix_ring();
    assert!(!r.is_commutative());
    assert!(r.field().is_err());
    assert_eq!(r.multiply(2, 4).unwrap(), 1);
    assert_eq!(r.multiply(4, 2).unwrap(), 8);
    let m = FiniteModule::regular(&r).unwrap();
    assert_eq!(m.scale(2, 4).unwrap(), 1);
    let right = (0..256)
        .map(|i| r.multiply(i % 16, i / 16).unwrap())
        .collect();
    assert!(FiniteModule::new(r.clone(), r.additive().clone(), right).is_err());
}
#[test]
fn ring_laws_and_unital_maps_are_independently_enforced() {
    let r = FiniteRing::residues(4).unwrap();
    let s = FiniteRing::residues(2).unwrap();
    let f = r.homomorphism(&s, &[0, 1, 0, 1]).unwrap();
    assert_eq!(f.kernel().embedding(), &[0, 2]);
    assert_eq!(f.image().embedding(), &[0, 1]);
    assert!(f.is_surjective());
    assert!(!f.is_injective());
    assert!(r.homomorphism(&s, &[0, 0, 0, 0]).is_err()); // additive, fails one
    let mut mult = Vec::new();
    for a in 0..4 {
        for b in 0..4 {
            mult.push(if a == 0 || b == 0 {
                0
            } else if a == 1 {
                b
            } else if b == 1 {
                a
            } else {
                a.max(b)
            });
        }
    }
    assert!(FiniteRing::new(4, 0, 1, r.addition_table().to_vec(), mult)
        .unwrap_err()
        .0
        .contains("distributivity"));
    let f4 = gf4();
    assert!(f4.homomorphism(&f4, &[0, 1, 3, 2]).is_ok()); // Frobenius is a field automorphism
}
#[test]
fn module_maps_require_scalar_linearity_not_just_additivity() {
    let r = FiniteRing::residues(2).unwrap();
    let v = FiniteGroup::new(4, 0, (0..16).map(|i| (i / 4) ^ (i % 4)).collect()).unwrap();
    let m = FiniteModule::new(r.clone(), v, vec![0, 0, 0, 0, 0, 1, 2, 3]).unwrap();
    let target = FiniteModule::regular(&r).unwrap();
    let f = m.homomorphism(&target, &[0, 1, 0, 1]).unwrap();
    assert_eq!(f.kernel().embedding(), &[0, 2]);
    assert!(f.is_surjective());
    let f4 = gf4();
    let regular = FiniteModule::regular(&f4).unwrap();
    let frobenius = [0, 1, 3, 2];
    assert!(regular
        .additive()
        .homomorphism(regular.additive(), &frobenius)
        .is_ok());
    assert!(regular
        .homomorphism(&regular, &frobenius)
        .unwrap_err()
        .0
        .contains("scalar"));
    assert!(regular
        .homomorphism(
            &FiniteModule::regular(&FiniteRing::residues(4).unwrap()).unwrap(),
            &[0, 1, 2, 3]
        )
        .is_err());
    assert!(m.scale(2, 0).is_err());
    assert!(m.scale(0, 4).is_err());
    assert!(FiniteModule::new(r, m.additive().clone(), vec![0]).is_err());
}
