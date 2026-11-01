use dlm_core::math::exact::{ExactBudget, Integer, Natural, Rational};
fn budget() -> ExactBudget {
    ExactBudget::default()
}
#[test]
fn independent_python_integer_and_fraction_oracle() {
    let mut count = 0;
    for line in include_str!("fixtures/exact093.tsv")
        .lines()
        .filter(|s| !s.starts_with('#'))
    {
        let x: Vec<_> = line.split('\t').collect();
        let mut b = budget();
        let actual = match x[0] {
            "N" => {
                let a = Natural::parse(x[2], &mut b).unwrap();
                let c = Natural::parse(x[3], &mut b).unwrap();
                match x[1] {
                    "cmp" => Ok((a.cmp(&c) as i32).to_string()),
                    "div" => a.div_rem(&c, &mut b).map(|v| v.0.to_string()),
                    "mod" => a.div_rem(&c, &mut b).map(|v| v.1.to_string()),
                    op => match op {
                        "add" => a.checked_add(&c, &mut b),
                        "sub" => a.checked_sub(&c, &mut b),
                        "mul" => a.checked_mul(&c, &mut b),
                        "gcd" => a.checked_gcd(&c, &mut b),
                        _ => panic!(),
                    }
                    .map(|n| n.to_string()),
                }
            }
            "I" => {
                let a = Integer::parse(x[2], &mut b).unwrap();
                let c = Integer::parse(x[3], &mut b).unwrap();
                match x[1] {
                    "cmp" => a.checked_cmp(&c, &mut b).map(|v| (v as i32).to_string()),
                    "div" => a.div_rem(&c, &mut b).map(|v| v.0.to_string()),
                    "mod" => a.div_rem(&c, &mut b).map(|v| v.1.to_string()),
                    op => match op {
                        "add" => a.checked_add(&c, &mut b),
                        "sub" => a.checked_sub(&c, &mut b),
                        "mul" => a.checked_mul(&c, &mut b),
                        _ => panic!(),
                    }
                    .map(|n| n.to_string()),
                }
            }
            "Q" => {
                let a = Rational::parse(x[2], &mut b).unwrap();
                let c = Rational::parse(x[3], &mut b).unwrap();
                match x[1] {
                    "cmp" => a.checked_cmp(&c, &mut b).map(|v| (v as i32).to_string()),
                    op => match op {
                        "add" => a.checked_add(&c, &mut b),
                        "sub" => a.checked_sub(&c, &mut b),
                        "mul" => a.checked_mul(&c, &mut b),
                        "div" => a.checked_div(&c, &mut b),
                        _ => panic!(),
                    }
                    .map(|n| n.to_string()),
                }
            }
            _ => panic!(),
        }
        .unwrap_or_else(|_| "ERR".into());
        assert_eq!(actual, x[4], "{line}");
        count += 1;
    }
    assert_eq!(count, 6426);
}
#[test]
fn limb_edges_decimal_roundtrip_and_maximum_supported_precision() {
    for bits in [
        1, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 4096, 16384,
    ] {
        let mut b = ExactBudget::new(16384, 200_000_000).unwrap();
        let mut n = Natural::from_u128(1);
        for _ in 1..bits {
            n = n.checked_add(&n, &mut b).unwrap();
        }
        assert_eq!(n.bit_len(), bits);
        assert_eq!(Natural::parse(&n.to_string(), &mut b).unwrap(), n);
        let divisor = Natural::from_u128(3);
        let (q, r) = n.div_rem(&divisor, &mut b).unwrap();
        assert_eq!(
            q.checked_mul(&divisor, &mut b)
                .unwrap()
                .checked_add(&r, &mut b)
                .unwrap(),
            n
        );
        assert!(r < divisor);
    }
}
#[test]
fn explicit_work_and_precision_failure_do_not_change_operands() {
    assert!(ExactBudget::new(0, 100).is_err());
    assert!(ExactBudget::new(16385, 100).is_err());
    let a = Rational::from_integer(7);
    let saved = a.clone();
    let mut no_work = ExactBudget::new(128, 0).unwrap();
    assert!(a
        .checked_add(&a, &mut no_work)
        .unwrap_err()
        .0
        .starts_with("UnknownWithinBudget:"));
    assert_eq!(a, saved);
    let mut b = ExactBudget::new(128, 1_000_000).unwrap();
    let max = Rational::parse(&u128::MAX.to_string(), &mut b).unwrap();
    assert!(max
        .checked_add(&Rational::one(), &mut b)
        .unwrap_err()
        .0
        .contains("precision"));
    assert_eq!(max.numerator().to_u128(), Some(u128::MAX));
    assert!(Natural::parse(&"1".repeat(130), &mut b).is_err());
    let mut zero = ExactBudget::new(128, 0).unwrap();
    assert!(Natural::parse("12", &mut zero).is_err());
    assert_eq!(zero.remaining_work(), 0);
}
#[test]
fn strict_syntax_canonical_zero_and_integer_min() {
    for text in ["", "+1", "--1", "1/-2", "1/0", "1/2/3", " 1", "1.5", "١"] {
        assert!(Rational::parse(text, &mut budget()).is_err(), "{text}");
    }
    assert_eq!(
        Rational::parse("-000/0007", &mut budget()).unwrap(),
        Rational::zero()
    );
    assert_eq!(
        Rational::parse("-6/8", &mut budget()).unwrap().to_string(),
        "-3/4"
    );
    assert_eq!(
        Integer::from_i128(i128::MIN).to_string(),
        i128::MIN.to_string()
    );
    assert_eq!(
        Integer::parse("-0", &mut budget()).unwrap(),
        Integer::from_i128(0)
    );
    assert!(Rational::one()
        .checked_div(&Rational::zero(), &mut budget())
        .unwrap_err()
        .0
        .contains("division by zero"));
    assert_eq!(
        Natural::zero()
            .checked_gcd(&Natural::zero(), &mut budget())
            .unwrap(),
        Natural::zero()
    );
}
#[test]
fn signed_euclidean_division_matches_machine_model_in_all_signs() {
    for a in -20i128..=20 {
        for d in -7i128..=7 {
            if d == 0 {
                continue;
            }
            let (q, r) = Integer::from_i128(a)
                .div_rem(&Integer::from_i128(d), &mut budget())
                .unwrap();
            assert_eq!(q.to_string(), a.div_euclid(d).to_string());
            assert_eq!(r.to_string(), a.rem_euclid(d).to_string());
        }
    }
}
#[test]
fn wide_cancellation_and_large_integer_precision_are_separate_from_u128() {
    let mut b = ExactBudget::new(128, 1_000_000).unwrap();
    let half = Rational::parse(&format!("{}/2", u128::MAX), &mut b).unwrap();
    assert_eq!(
        half.checked_add(&half, &mut b).unwrap().to_string(),
        format!("{}/1", u128::MAX)
    );
    let mut b = budget();
    let large = Rational::parse("340282366920938463463374607431768211456", &mut b).unwrap();
    assert_eq!(large.numerator().to_u128(), None);
    assert_eq!(
        large
            .checked_mul(&large.reciprocal(&mut b).unwrap(), &mut b)
            .unwrap(),
        Rational::one()
    );
    assert_eq!(large.checked_sub(&large, &mut b).unwrap(), Rational::zero());
}
