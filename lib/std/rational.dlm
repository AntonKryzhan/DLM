// Exact signed rationals; public canonical magnitudes/denominators <= 2^32-1.
// Intermediate arithmetic is bounded below 2^65 in the machine Nat profile.
module std.rational

theory StdRational {
    private fn qn_same(a: Bool, b: Bool) -> Bool = if_then_else(a, b, not(b))
    record Rational(negative: Bool, numerator: Nat, denominator: Nat)
    private fn qn_gcd(budget: Nat, a: Nat, b: Nat) -> Result<Nat, Text> decreasing budget fuel 512 =
        if_then_else(nat_eq(b, 0), ok(a, Text), if_then_else(nat_eq(budget, 0), err("rational gcd budget exhausted", Nat), qn_gcd(pred(budget), b, mod(a, b))))
    private fn qn_finish(negative: Bool, n: Nat, d: Nat) -> Result<Rational, Text> =
        if_then_else(nat_eq(d, 0), err("rational denominator is zero", Rational),
            if_then_else(nat_eq(n, 0), ok(Rational(false, 0, 1), Text),
                match qn_gcd(256, n, d) {
                    err(e) => err(e, Rational),
                    ok(g) => let nn = div(n, g) in let dd = div(d, g) in
                        if_then_else(or(nn > 4294967295, dd > 4294967295), err("rational profile overflow", Rational), ok(Rational(negative, nn, dd), Text))
                }))
    fn rat_new(negative: Bool, n: Nat, d: Nat) -> Result<Rational, Text> = qn_finish(negative, n, d)
    fn rat_valid(a: Rational) -> Bool =
        if_then_else(or(field(a, numerator) > 4294967295, or(field(a, denominator) > 4294967295, nat_eq(field(a, denominator), 0))), false,
            if_then_else(nat_eq(field(a, numerator), 0), and(not(field(a, negative)), nat_eq(field(a, denominator), 1)), nat_eq(unwrap_or(qn_gcd(256, field(a, numerator), field(a, denominator)), 0), 1)))
    private fn qn_neg(a: Rational) -> Rational = Rational(and(not(field(a, negative)), not(nat_eq(field(a, numerator), 0))), field(a, numerator), field(a, denominator))
    fn rat_neg(a: Rational) -> Result<Rational, Text> = if_then_else(rat_valid(a), ok(qn_neg(a), Text), err("invalid canonical rational", Rational))
    fn rat_inv(a: Rational) -> Result<Rational, Text> =
        if_then_else(not(rat_valid(a)), err("invalid canonical rational", Rational),
            if_then_else(nat_eq(field(a, numerator), 0), err("rational division by zero", Rational), ok(Rational(field(a, negative), field(a, denominator), field(a, numerator)), Text)))
    private fn qn_add(a: Rational, b: Rational) -> Result<Rational, Text> =
        let left = mul(field(a, numerator), field(b, denominator)) in let right = mul(field(b, numerator), field(a, denominator)) in
        let denominator = mul(field(a, denominator), field(b, denominator)) in
        if_then_else(qn_same(field(a, negative), field(b, negative)), qn_finish(field(a, negative), left + right, denominator),
            if_then_else(left > right, qn_finish(field(a, negative), monus(left, right), denominator), qn_finish(field(b, negative), monus(right, left), denominator)))
    fn rat_add(a: Rational, b: Rational) -> Result<Rational, Text> = if_then_else(and(rat_valid(a), rat_valid(b)), qn_add(a, b), err("invalid canonical rational", Rational))
    fn rat_sub(a: Rational, b: Rational) -> Result<Rational, Text> = if_then_else(and(rat_valid(a), rat_valid(b)), qn_add(a, qn_neg(b)), err("invalid canonical rational", Rational))
    private fn qn_mul(a: Rational, b: Rational) -> Result<Rational, Text> = qn_finish(not(qn_same(field(a, negative), field(b, negative))), mul(field(a, numerator), field(b, numerator)), mul(field(a, denominator), field(b, denominator)))
    fn rat_mul(a: Rational, b: Rational) -> Result<Rational, Text> = if_then_else(and(rat_valid(a), rat_valid(b)), qn_mul(a, b), err("invalid canonical rational", Rational))
    fn rat_div(a: Rational, b: Rational) -> Result<Rational, Text> =
        if_then_else(not(and(rat_valid(a), rat_valid(b))), err("invalid canonical rational", Rational), match rat_inv(b) { err(e) => err(e, Rational), ok(inverse) => qn_mul(a, inverse) })
    fn rat_equal(a: Rational, b: Rational) -> Bool = if_then_else(and(rat_valid(a), rat_valid(b)), and(qn_same(field(a, negative), field(b, negative)), and(nat_eq(field(a, numerator), field(b, numerator)), nat_eq(field(a, denominator), field(b, denominator)))), false)
    fn rat_cmp(a: Rational, b: Rational) -> Result<Int, Text> =
        if_then_else(not(and(rat_valid(a), rat_valid(b))), err("invalid canonical rational", Int),
            if_then_else(not(qn_same(field(a, negative), field(b, negative))), ok(if_then_else(field(a, negative), -1, int(1)), Text),
                let left = mul(field(a, numerator), field(b, denominator)) in let right = mul(field(b, numerator), field(a, denominator)) in
                ok(if_then_else(nat_eq(left, right), int(0), if_then_else(qn_same(left > right, field(a, negative)), -1, int(1))), Text)))
    fn rat_text(a: Rational) -> Result<Text, Text> =
        if_then_else(rat_valid(a), ok(concat(if_then_else(field(a, negative), "-", ""), concat(nat_to_text(field(a, numerator)), concat("/", nat_to_text(field(a, denominator))))), Text), err("invalid canonical rational", Text))
    // Closed finite computations; no general field laws are added to the kernel.
    theorem reduce_half: rat_equal(unwrap_or(rat_new(false, 2, 4), Rational(false, 0, 1)), Rational(false, 1, 2))
    theorem canonical_zero: rat_equal(unwrap_or(rat_new(true, 0, 8), Rational(true, 0, 1)), Rational(false, 0, 1))
    theorem reject_zero_inverse: is_err(rat_inv(Rational(false, 0, 1)))
}
