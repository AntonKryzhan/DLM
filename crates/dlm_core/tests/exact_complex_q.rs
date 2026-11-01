use dlm_core::math::complex_q::ComplexQ;
use dlm_core::math::exact::{ExactBudget, Rational};

fn budget(bits: usize) -> ExactBudget {
    ExactBudget::new(bits, 2_000_000_000).unwrap()
}
fn pair(real: &str, imag: &str, bits: usize) -> ComplexQ {
    let mut b = budget(bits);
    ComplexQ::from_parts(
        Rational::parse(real, &mut b).unwrap(),
        Rational::parse(imag, &mut b).unwrap(),
        &mut b,
    )
    .unwrap()
}

#[test]
fn complex_arithmetic_matches_independent_fraction_pair_oracles_through_16384_bits() {
    let mut count = 0;
    for line in include_str!("fixtures/complex_q098.tsv")
        .lines()
        .filter(|l| !l.starts_with('#'))
    {
        let f: Vec<_> = line.split('\t').collect();
        let bits = f[1].parse().unwrap();
        let z = pair(f[2], f[3], bits);
        let w = pair(f[4], f[5], bits);
        let mut b = budget(bits);
        let result = match f[0] {
            "new" => ComplexQ::from_parts(z.real().clone(), z.imag().clone(), &mut b)
                .map(|v| v.to_string()),
            "add" => z.checked_add(&w, &mut b).map(|v| v.to_string()),
            "sub" => z.checked_sub(&w, &mut b).map(|v| v.to_string()),
            "mul" => z.checked_mul(&w, &mut b).map(|v| v.to_string()),
            "div" => z.checked_div(&w, &mut b).map(|v| v.to_string()),
            "neg" => z.checked_neg(&mut b).map(|v| v.to_string()),
            "conj" => z.conjugate(&mut b).map(|v| v.to_string()),
            "norm_squared" => z.norm_squared(&mut b).map(|v| v.to_string()),
            "equal" => z.checked_equal(&w, &mut b).map(|v| v.to_string()),
            "inv" => z.reciprocal(&mut b).map(|v| v.to_string()),
            op => panic!("unknown fixture operation {op}"),
        };
        match f[6] {
            "domain" => assert_eq!(
                result.unwrap_err().to_string(),
                "exact complex division by zero"
            ),
            "precision" => assert!(result
                .unwrap_err()
                .to_string()
                .contains("precision exceeded")),
            _ => assert_eq!(
                result.unwrap(),
                if f[7] == "-" {
                    f[6].into()
                } else {
                    format!("{};{}", f[6], f[7])
                },
                "row {count}: {}",
                f[0]
            ),
        }
        count += 1;
    }
    assert_eq!(count, 1059);
}

#[test]
fn gaussian_units_field_identities_conjugation_and_norm_agree() {
    let mut b = budget(256);
    assert_eq!(
        ComplexQ::i().checked_mul(&ComplexQ::i(), &mut b).unwrap(),
        ComplexQ::from_integers(-1, 0, &mut b).unwrap()
    );
    for a in -2..=2 {
        for c in -2..=2 {
            let z = ComplexQ::from_integers(a, c, &mut b).unwrap();
            let before = z.clone();
            let conjugate = z.conjugate(&mut b).unwrap();
            assert_eq!(conjugate.conjugate(&mut b).unwrap(), z);
            let norm = z.norm_squared(&mut b).unwrap();
            let product = z.checked_mul(&conjugate, &mut b).unwrap();
            assert_eq!(product.real(), &norm);
            assert_eq!(product.imag(), &Rational::zero());
            if !z.is_zero() {
                assert_eq!(
                    z.checked_mul(&z.reciprocal(&mut b).unwrap(), &mut b)
                        .unwrap(),
                    ComplexQ::one()
                );
                assert_eq!(
                    ComplexQ::zero().checked_div(&z, &mut b).unwrap(),
                    ComplexQ::zero()
                );
            }
            for d in -2..=2 {
                let w = ComplexQ::from_integers(d, 1, &mut b).unwrap();
                let u = ComplexQ::from_integers(1, d, &mut b).unwrap();
                assert_eq!(
                    z.checked_mul(&w.checked_add(&u, &mut b).unwrap(), &mut b)
                        .unwrap(),
                    z.checked_mul(&w, &mut b)
                        .unwrap()
                        .checked_add(&z.checked_mul(&u, &mut b).unwrap(), &mut b)
                        .unwrap()
                );
                let zw = z.checked_mul(&w, &mut b).unwrap();
                assert_eq!(
                    zw.norm_squared(&mut b).unwrap(),
                    norm.checked_mul(&w.norm_squared(&mut b).unwrap(), &mut b)
                        .unwrap()
                );
            }
            assert_eq!(z, before);
        }
    }
}

#[test]
fn complex_precision_is_checked_before_zero_or_inequality_shortcuts() {
    let z = pair("1", "256", 16);
    let zero = ComplexQ::zero();
    assert!(z.checked_mul(&zero, &mut budget(8)).is_err());
    assert!(z.checked_equal(&zero, &mut budget(8)).is_err());
    assert!(zero.checked_equal(&z, &mut budget(8)).is_err());
    assert!(zero.checked_div(&z, &mut budget(8)).is_err());
    assert!(z.checked_neg(&mut budget(8)).is_err());
    assert!(z.conjugate(&mut budget(8)).is_err());
    assert!(z.norm_squared(&mut budget(8)).is_err());
    assert!(ComplexQ::from_parts(z.real().clone(), z.imag().clone(), &mut budget(8)).is_err());
    let real_outside = pair("256", "0", 16);
    assert!(real_outside
        .checked_div(&zero, &mut budget(8))
        .unwrap_err()
        .to_string()
        .contains("precision"));
    assert!(ComplexQ::from_integers(i128::MIN, i128::MAX, &mut budget(127)).is_err());
    let extremes = ComplexQ::from_integers(i128::MIN, i128::MAX, &mut budget(128)).unwrap();
    assert_eq!(extremes.real().to_string(), format!("{}/1", i128::MIN));
    assert_eq!(extremes.imag().to_string(), format!("{}/1", i128::MAX));
    assert_eq!(pair("-0/3", "2/4", 8).to_string(), "0/1;1/2");
}

#[test]
fn every_fallible_complex_operation_uses_the_same_mutable_work_budget() {
    let z = pair("1/2", "-3/4", 64);
    let w = pair("-2/3", "5/7", 64);
    let before = z.clone();
    for op in [
        "new", "ints", "equal", "neg", "conj", "add", "sub", "mul", "div", "inv", "norm",
    ] {
        let evaluate = |b: &mut ExactBudget| match op {
            "new" => ComplexQ::from_parts(z.real().clone(), z.imag().clone(), b).map(|_| ()),
            "ints" => ComplexQ::from_integers(-3, 7, b).map(|_| ()),
            "equal" => z.checked_equal(&w, b).map(|_| ()),
            "neg" => z.checked_neg(b).map(|_| ()),
            "conj" => z.conjugate(b).map(|_| ()),
            "add" => z.checked_add(&w, b).map(|_| ()),
            "sub" => z.checked_sub(&w, b).map(|_| ()),
            "mul" => z.checked_mul(&w, b).map(|_| ()),
            "div" => z.checked_div(&w, b).map(|_| ()),
            "inv" => z.reciprocal(b).map(|_| ()),
            "norm" => z.norm_squared(b).map(|_| ()),
            _ => unreachable!(),
        };
        let mut measured = budget(64);
        evaluate(&mut measured).unwrap();
        let used = measured.work_used();
        assert!(used > 0, "{op}");
        let mut exact = ExactBudget::new(64, used).unwrap();
        evaluate(&mut exact).unwrap();
        assert_eq!(exact.remaining_work(), 0, "{op}");
        assert!(
            evaluate(&mut exact).is_err(),
            "{op} restarted an exhausted budget"
        );
        let mut short = ExactBudget::new(64, used - 1).unwrap();
        assert!(
            evaluate(&mut short)
                .unwrap_err()
                .to_string()
                .contains("work exhausted"),
            "{op}"
        );
        assert_eq!(short.remaining_work(), 0);
        assert_eq!(z, before);
    }
}
