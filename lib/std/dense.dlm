// Dense exact integers (N14): signed magnitudes stored as BigNat (32-bit limbs) instead of List<Nat> words.
// Same values, refusals and precision limits as std.exact; work is charged in dense units
// (1 + limbs(a)·limbs(b) plus the linear reservation for products/quotients). See docs/DENSE_NUMBERS.md.
module std.dense
import std.scalar_witness

// v0.108: on top of the numeric chain, so one program reaches every numeric family but std.complex_q.
theory StdDense extends StdScalarWitness {
    record DInt(negative: Bool, magnitude: BigNat)
    record DIntCalc(value: DInt, budget: ExactBudget)
    record DDivision(quotient: DInt, remainder: DInt, budget: ExactBudget)
    record DCompare(order: Int, budget: ExactBudget)
    private fn dz_zero() -> BigNat = bn_from_nat(0)
    private fn dz_number(negative: Bool, m: BigNat) -> DInt = DInt(and(negative, bn_bits(m) > 0), m)
    private fn dz_valid(b: ExactBudget) -> Bool = is_ok(exact_budget(field(b, bits), field(b, work)))
    fn dz_charge(b: ExactBudget, cost: Nat) -> Result<ExactBudget, Text> =
        if_then_else(not(dz_valid(b)), err("invalid exact source budget", ExactBudget),
            if_then_else(cost > field(b, work), err("UnknownWithinBudget: source work reservation exhausted", ExactBudget), ok(ExactBudget(field(b, bits), monus(field(b, work), cost)), Text)))
    private fn dz_fits(b: ExactBudget, x: DInt) -> Bool = not(bn_bits(field(x, magnitude)) > field(b, bits))
    private fn dz_shape(x: DInt) -> Bool = or(not(field(x, negative)), bn_bits(field(x, magnitude)) > 0)
    private fn dz_max(a: Nat, c: Nat) -> Nat = if_then_else(a > c, a, c)
    // operands within precision, then the reservation
    private fn dz_prepare(b: ExactBudget, a: DInt, c: DInt, cost: Nat) -> Result<ExactBudget, Text> =
        if_then_else(not(dz_valid(b)), err("invalid exact source budget", ExactBudget),
            if_then_else(not(and(dz_shape(a), dz_shape(c))), err("invalid canonical big integer", ExactBudget),
            if_then_else(or(not(dz_fits(b, a)), not(dz_fits(b, c))), err("UnknownWithinBudget: source precision exceeded", ExactBudget), dz_charge(b, cost))))
    private fn dz_linear(a: DInt, c: DInt) -> Nat = 1 + dz_max(bn_limbs(field(a, magnitude)), bn_limbs(field(c, magnitude)))
    private fn dz_product(a: DInt, c: DInt) -> Nat = 1 + mul(bn_limbs(field(a, magnitude)), bn_limbs(field(c, magnitude))) + dz_linear(a, c)
    private fn dz_finish(b: ExactBudget, x: DInt) -> Result<DIntCalc, Text> =
        if_then_else(not(dz_valid(b)), err("invalid exact source budget", DIntCalc),
            if_then_else(dz_fits(b, x), ok(DIntCalc(x, b), Text), err("UnknownWithinBudget: source precision exceeded", DIntCalc)))
    private fn dz_opt(b: ExactBudget, negative: Bool, m: Option<BigNat>) -> Result<DIntCalc, Text> =
        match m { none => err("UnknownWithinBudget: source precision exceeded", DIntCalc), some(v) => dz_finish(b, dz_number(negative, v)) }

    fn dz_from_big(b: ExactBudget, x: BigInt) -> Result<DIntCalc, Text> =
        if_then_else(not(dz_valid(b)), err("invalid exact source budget", DIntCalc),
        match bn_from_words(field(x, words)) { none => err("invalid big integer words", DIntCalc), some(m) =>
            if_then_else(or(not(nat_eq(length(bn_to_words(m)), length(field(x, words)))), and(field(x, negative), nat_eq(bn_bits(m), 0))), err("invalid canonical big integer", DIntCalc),
            match dz_charge(b, 1 + length(field(x, words))) { err(e) => err(e, DIntCalc), ok(left) => dz_finish(left, dz_number(field(x, negative), m)) }) })
    fn dz_to_big(b: ExactBudget, x: DInt) -> Result<BigIntCalc, Text> =
        match dz_prepare(b, x, x, 1 + bn_limbs(field(x, magnitude))) { err(e) => err(e, BigIntCalc), ok(left) => ok(BigIntCalc(BigInt(field(x, negative), bn_to_words(field(x, magnitude))), left), Text) }
    fn dz_from_int(b: ExactBudget, v: Int) -> Result<DIntCalc, Text> = dz_finish(b, dz_number(int(0) > v, bn_from_nat(abs(v))))
    fn dz_parse(b: ExactBudget, text: Text) -> Result<DIntCalc, Text> =
        if_then_else(not(dz_valid(b)), err("invalid exact source budget", DIntCalc),
        if_then_else(text_length(text) > field(b, bits) + 2, err("UnknownWithinBudget: source decimal input exceeded", DIntCalc),
            let negative = text_eq(substring(text, 0, 1), "-") in
            match bn_from_text(if_then_else(negative, substring(text, 1, text_length(text)), text)) {
                none => err("invalid exact decimal", DIntCalc),
                some(m) => match dz_charge(b, 1 + div(text_length(text), 9)) { err(e) => err(e, DIntCalc), ok(left) => dz_finish(left, dz_number(negative, m)) }
            }))
    fn dz_text(b: ExactBudget, x: DInt) -> Result<BigTextCalc, Text> =
        match dz_prepare(b, x, x, 1 + mul(bn_limbs(field(x, magnitude)), bn_limbs(field(x, magnitude)))) { err(e) => err(e, BigTextCalc), ok(left) =>
            ok(BigTextCalc(concat(if_then_else(field(x, negative), "-", ""), bn_to_text(field(x, magnitude))), left), Text) }
    private fn dz_xor(a: Bool, c: Bool) -> Bool = if_then_else(a, not(c), c)
    private fn dz_signed_add(a: DInt, c: DInt) -> Option<DInt> =
        if_then_else(not(dz_xor(field(a, negative), field(c, negative))),
            match bn_add(field(a, magnitude), field(c, magnitude)) { none => none(DInt), some(m) => some(dz_number(field(a, negative), m)) },
            if_then_else(int_eq(bn_cmp(field(a, magnitude), field(c, magnitude)), -1), some(dz_number(field(c, negative), bn_monus(field(c, magnitude), field(a, magnitude)))),
                some(dz_number(field(a, negative), bn_monus(field(a, magnitude), field(c, magnitude))))))
    fn dz_add(b: ExactBudget, a: DInt, c: DInt) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, c, dz_linear(a, c)) { err(e) => err(e, DIntCalc), ok(left) =>
            match dz_signed_add(a, c) { none => err("UnknownWithinBudget: source precision exceeded", DIntCalc), some(s) => dz_finish(left, s) } }
    fn dz_neg(a: DInt) -> DInt = dz_number(not(field(a, negative)), field(a, magnitude))
    fn dz_sub(b: ExactBudget, a: DInt, c: DInt) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, c, 0) { err(e) => err(e, DIntCalc), ok(left) => dz_add(left, a, dz_neg(c)) }
    fn dz_mul(b: ExactBudget, a: DInt, c: DInt) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, c, dz_product(a, c)) { err(e) => err(e, DIntCalc), ok(left) =>
            dz_opt(left, dz_xor(field(a, negative), field(c, negative)), bn_mul(field(a, magnitude), field(c, magnitude))) }
    fn dz_cmp(b: ExactBudget, a: DInt, c: DInt) -> Result<DCompare, Text> =
        match dz_prepare(b, a, c, dz_linear(a, c)) { err(e) => err(e, DCompare), ok(left) =>
            let m = bn_cmp(field(a, magnitude), field(c, magnitude)) in
            ok(DCompare(if_then_else(dz_xor(field(a, negative), field(c, negative)), if_then_else(field(a, negative), -1, int(1)), if_then_else(field(a, negative), -m, m)), left), Text) }
    // floor quotient for a negative dividend, remainder in 0..|c| (std.exact big_div_rem)
    fn dz_div_rem(b: ExactBudget, a: DInt, c: DInt) -> Result<DDivision, Text> =
        match dz_prepare(b, a, c, dz_product(a, c)) { err(e) => err(e, DDivision), ok(left) =>
            match bn_div(field(a, magnitude), field(c, magnitude)) { none => err("exact division by zero", DDivision), some(q0) =>
                let r0 = unwrap_or(bn_mod(field(a, magnitude), field(c, magnitude)), dz_zero()) in
                let adjust = and(field(a, negative), bn_bits(r0) > 0) in
                let q = if_then_else(adjust, unwrap_or(bn_add(q0, bn_from_nat(1)), q0), q0) in
                let r = if_then_else(adjust, bn_monus(field(c, magnitude), r0), r0) in
                ok(DDivision(dz_number(dz_xor(field(a, negative), field(c, negative)), q), dz_number(false, r), left), Text) } }
    fn dz_gcd(b: ExactBudget, a: DInt, c: DInt) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, c, dz_product(a, c)) { err(e) => err(e, DIntCalc), ok(left) => dz_finish(left, dz_number(false, bn_gcd(field(a, magnitude), field(c, magnitude)))) }
    fn dz_sqrt(b: ExactBudget, a: DInt) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, a, dz_product(a, a)) { err(e) => err(e, DIntCalc), ok(left) =>
            if_then_else(field(a, negative), err("exact square root of negative integer", DIntCalc), dz_finish(left, dz_number(false, bn_sqrt(field(a, magnitude))))) }
    private fn dz_power(n: Nat, b: ExactBudget, a: DInt, e: Nat, acc: DInt) -> Result<DIntCalc, Text> decreasing n fuel 256 =
        if_then_else(nat_eq(e, 0), ok(DIntCalc(acc, b), Text),
            if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: source power steps exhausted", DIntCalc),
                match if_then_else(nat_eq(mod(e, 2), 1), dz_mul(b, acc, a), ok(DIntCalc(acc, b), Text)) { err(x) => err(x, DIntCalc), ok(p) =>
                    if_then_else(nat_eq(div(e, 2), 0), ok(p, Text),
                        match dz_mul(field(p, budget), a, a) { err(x) => err(x, DIntCalc), ok(sq) => dz_power(pred(n), field(sq, budget), field(sq, value), div(e, 2), field(p, value)) }) }))
    fn dz_pow(b: ExactBudget, a: DInt, e: Nat) -> Result<DIntCalc, Text> =
        match dz_prepare(b, a, DInt(false, dz_zero()), dz_linear(a, DInt(false, dz_zero()))) { err(e) => err(e, DIntCalc), ok(left) => dz_power(128, left, a, e, DInt(false, bn_from_nat(1))) }
    // the memory of a value: its 32-bit limbs
    fn dz_limbs(x: DInt) -> Nat = bn_limbs(field(x, magnitude))

    // Closed finite computations, not general arithmetic proofs.
    theorem division_by_zero_is_refused: is_err(dz_div_rem(exact_default_budget(), DInt(false, bn_from_nat(7)), DInt(false, bn_from_nat(0))))
    theorem dense_product_is_exact: nat_eq(unwrap_or(bn_to_nat(unwrap_or(bn_mul(bn_from_nat(65536), bn_from_nat(65536)), bn_from_nat(0))), 0), 4294967296)
}
