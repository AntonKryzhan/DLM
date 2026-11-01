use dlm_core::math::exact::{ExactBudget, Integer, Natural, Rational, MAX_BITS};

fn budget(bits: usize) -> ExactBudget {
    ExactBudget::new(bits, 200_000_000).unwrap()
}

#[test]
fn scalar_operations_match_independent_python_integer_fraction_and_isqrt_vectors() {
    let mut count = 0;
    for line in include_str!("fixtures/exact_ops096.tsv").lines() {
        if line.starts_with('#') {
            continue;
        }
        let f: Vec<_> = line.split('\t').collect();
        let bits = f[1].parse().unwrap();
        let mut b = budget(bits);
        let result = match f[0] {
            "pow" => Integer::parse(f[2], &mut b)
                .unwrap()
                .checked_pow(f[3].parse().unwrap(), &mut b)
                .unwrap()
                .to_string(),
            "qpow" => Rational::parse(f[2], &mut b)
                .unwrap()
                .checked_pow(f[3].parse().unwrap(), &mut b)
                .unwrap()
                .to_string(),
            "lcm" => {
                let a = Integer::parse(f[2], &mut b).unwrap();
                let c = Integer::parse(f[3], &mut b).unwrap();
                a.checked_lcm(&c, &mut b).unwrap().to_string()
            }
            "sqrt" => {
                let a = Natural::parse(f[2], &mut b).unwrap();
                let (root, rem) = a.checked_sqrt_rem(&mut b).unwrap();
                assert_eq!(rem.to_string(), f[5], "remainder for {}", f[2]);
                root.to_string()
            }
            op => panic!("unexpected fixture operation {op}"),
        };
        assert_eq!(result, f[4], "{}({}, {})", f[0], f[2], f[3]);
        count += 1;
    }
    assert_eq!(count, 2134);
}

#[test]
fn sqrt_invariants_hold_exhaustively_and_at_the_maximum_precision() {
    for n in 0..=32768u128 {
        let a = Natural::from_u128(n);
        let (s, r) = a.checked_sqrt_rem(&mut budget(16)).unwrap();
        let s = s.to_u128().unwrap();
        let r = r.to_u128().unwrap();
        assert_eq!(s * s + r, n);
        assert!(r < 2 * s + 1);
    }
    let mut b = budget(MAX_BITS);
    let a = Natural::from_u128(2)
        .checked_pow((MAX_BITS - 1) as u128, &mut b)
        .unwrap();
    let snapshot = a.clone();
    let (s, r) = a.checked_sqrt_rem(&mut b).unwrap();
    let square = s.checked_mul(&s, &mut b).unwrap();
    assert_eq!(square.checked_add(&r, &mut b).unwrap(), a);
    let upper = s
        .checked_mul(&Natural::from_u128(2), &mut b)
        .unwrap()
        .checked_add(&Natural::from_u128(1), &mut b)
        .unwrap();
    assert!(r < upper);
    assert_eq!(a, snapshot);
    let (s, r) = Integer::from_i128(10)
        .checked_sqrt_rem(&mut budget(4))
        .unwrap();
    assert_eq!((s.to_string(), r.to_string()), ("3".into(), "1".into()));
}

#[test]
fn power_and_lcm_validate_inputs_and_only_compute_required_intermediates() {
    let two = Natural::from_u128(2);
    assert_eq!(two.checked_pow(1, &mut budget(2)).unwrap(), two);
    assert_eq!(
        Natural::zero().checked_pow(0, &mut budget(1)).unwrap(),
        Natural::from_u128(1)
    );
    let high = Natural::from_u128(255);
    assert_eq!(high.checked_lcm(&high, &mut budget(8)).unwrap(), high);
    assert!(two.checked_pow(2, &mut budget(2)).is_err());
    assert!(high.checked_pow(0, &mut budget(1)).is_err());
    assert!(high.checked_lcm(&Natural::zero(), &mut budget(1)).is_err());
    assert!(high.checked_sqrt_rem(&mut budget(1)).is_err());
    assert!(Integer::from_i128(-1)
        .checked_sqrt_rem(&mut budget(1))
        .unwrap_err()
        .to_string()
        .contains("negative"));
    assert!(Rational::zero().checked_pow(-1, &mut budget(1)).is_err());
    let mut b = budget(2);
    let fraction = Rational::parse("2/3", &mut b).unwrap();
    assert_eq!(fraction.checked_pow(-1, &mut b).unwrap().to_string(), "3/2");
    assert!(fraction.checked_pow(0, &mut budget(1)).is_err());
    let a = Integer::from_i128(-128);
    assert!(a
        .checked_lcm(&Integer::from_i128(3), &mut budget(8))
        .is_err());
    assert_eq!(a.to_string(), "-128");
}

#[test]
fn scalar_work_exhaustion_stops_and_shared_budget_is_not_reset() {
    let n = Natural::from_u128(7);
    let q = Rational::from_integer(-1);
    assert!(n
        .checked_pow(0, &mut ExactBudget::new(8, 0).unwrap())
        .is_err());
    assert!(n
        .checked_lcm(&n, &mut ExactBudget::new(8, 0).unwrap())
        .is_err());
    assert!(n
        .checked_sqrt_rem(&mut ExactBudget::new(8, 0).unwrap())
        .is_err());
    assert!(q
        .checked_pow(0, &mut ExactBudget::new(8, 0).unwrap())
        .is_err());
    let mut measured = budget(8);
    n.checked_pow(2, &mut measured).unwrap();
    let cost = measured.work_used();
    let mut shared = ExactBudget::new(8, cost).unwrap();
    assert_eq!(n.checked_pow(2, &mut shared).unwrap().to_string(), "49");
    assert_eq!(shared.remaining_work(), 0);
    let err = n.checked_pow(2, &mut shared).unwrap_err();
    assert!(err.to_string().starts_with("UnknownWithinBudget:"));
    let mut short = ExactBudget::new(8, cost - 1).unwrap();
    assert!(n.checked_pow(2, &mut short).is_err());
    assert_eq!(short.remaining_work(), 0);
    assert_eq!(n.to_string(), "7");
}
