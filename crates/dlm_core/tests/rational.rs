use dlm_core::math::rational::Rational as Q;
#[test]
fn fraction_oracle_4455_vectors() {
    let mut count = 0;
    for line in include_str!("fixtures/rational128.tsv")
        .lines()
        .filter(|l| !l.starts_with('#'))
    {
        let x: Vec<_> = line.split('\t').collect();
        let a = Q::new(x[1] == "1", x[2].parse().unwrap(), x[3].parse().unwrap()).unwrap();
        let b = Q::new(x[4] == "1", x[5].parse().unwrap(), x[6].parse().unwrap()).unwrap();
        let actual = if x[0] == "cmp" {
            (a.cmp(&b) as i32).to_string()
        } else {
            match x[0] {
                "add" => a.checked_add(b),
                "sub" => a.checked_sub(b),
                "mul" => a.checked_mul(b),
                "div" => a.checked_div(b),
                _ => panic!(),
            }
            .map(|v| v.to_string())
            .unwrap_or_else(|_| "ERR".into())
        };
        assert_eq!(actual, x[7], "{line}");
        count += 1;
    }
    assert_eq!(count, 4455);
}
#[test]
fn canonical_sign_zero_and_min_integer() {
    assert_eq!(Q::new(true, 0, 28).unwrap(), Q::zero());
    assert_eq!(Q::new(true, 8, 12).unwrap().to_string(), "-2/3");
    assert_eq!(Q::from_integer(i128::MIN).numerator(), 1u128 << 127);
    assert_eq!(Q::zero().negated(), Q::zero());
    assert!(Q::new(false, 0, 0).is_err());
    let q = Q::new(true, 3, 7).unwrap();
    assert_eq!(q.reciprocal().unwrap().reciprocal().unwrap(), q);
}
#[test]
fn cancellation_precedes_profile_overflow() {
    let half = Q::new(false, u128::MAX, 2).unwrap();
    assert_eq!(
        half.checked_add(half).unwrap(),
        Q::new(false, u128::MAX, 1).unwrap()
    );
    let a = Q::new(false, u128::MAX, u128::MAX - 1).unwrap();
    assert_eq!(
        a.checked_mul(a.reciprocal().unwrap()).unwrap(),
        Q::from_integer(1)
    );
    assert_eq!(a.checked_sub(a).unwrap(), Q::zero());
    let a = Q::new(false, (u128::MAX - 1) / 2, u128::MAX - 2).unwrap();
    assert_eq!(a.checked_sub(a).unwrap(), Q::zero());
    assert!(Q::new(false, u128::MAX, 1)
        .unwrap()
        .checked_add(Q::from_integer(1))
        .is_err());
    assert!(Q::from_integer(1).checked_div(Q::zero()).is_err());
}
#[test]
fn comparison_is_exact_at_wide_cross_products() {
    let a = Q::new(false, u128::MAX, u128::MAX - 1).unwrap();
    let b = Q::new(false, u128::MAX - 1, u128::MAX - 2).unwrap();
    assert!(a < b);
    assert!(a.negated() > b.negated());
    assert!(a > Q::zero());
    assert!(Q::from_integer(-1) < Q::zero());
}
