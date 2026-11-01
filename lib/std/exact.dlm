// Exact source integers/rationals, radix 2^16. Public precision: 1..16384 bits.
// Work reservations are explicit immutable values, separate from host counters.
module std.exact

theory StdExact {
    record ExactBudget(bits: Nat, work: Nat)
    record BigInt(negative: Bool, words: List<Nat>)
    record BigRational(negative: Bool, numerator: List<Nat>, denominator: List<Nat>)
    record BigIntCalc(value: BigInt, budget: ExactBudget)
    record BigRationalCalc(value: BigRational, budget: ExactBudget)
    record BigTextCalc(value: Text, budget: ExactBudget)
    record BigCompare(order: Int, budget: ExactBudget)
    record BigDivision(quotient: BigInt, remainder: BigInt, budget: ExactBudget)
    record BigSquareRoot(root: BigInt, remainder: BigInt, budget: ExactBudget)
    record WordDivision(quotient: List<Nat>, remainder: List<Nat>)
    record WordSmallDivision(quotient: List<Nat>, remainder: Nat)
    record WordSquareRoot(root: List<Nat>, remainder: List<Nat>)
    record WordDigitDivision(quotient: Nat, remainder: List<Nat>)
    record WordGcdState(a: List<Nat>, b: List<Nat>)

    private fn bx_work_limit(bits: Nat) -> Nat = if_then_else(bits > 1024, 18446744073709551615, 1000000000000000)
    fn exact_budget(bits: Nat, work: Nat) -> Result<ExactBudget, Text> =
        if_then_else(or(nat_eq(bits, 0), or(bits > 16384, work > bx_work_limit(bits))), err("invalid exact source budget", ExactBudget), ok(ExactBudget(bits, work), Text))
    fn exact_default_budget() -> ExactBudget = ExactBudget(256, 1000000000000000)
    private fn bx_budget_valid(b: ExactBudget) -> Bool = and(and(field(b, bits) > 0, not(field(b, bits) > 16384)), not(field(b, work) > bx_work_limit(field(b, bits))))
    private fn bx_reverse(xs: List<Nat>, acc: List<Nat>) -> List<Nat> decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), acc, bx_reverse(tail(xs), cons(unwrap_or(head(xs), 0), acc)))
    private fn bx_drop_zero(xs: List<Nat>) -> List<Nat> decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), xs, if_then_else(nat_eq(unwrap_or(head(xs), 0), 0), bx_drop_zero(tail(xs)), xs))
    private fn bx_trim(xs: List<Nat>) -> List<Nat> = bx_reverse(bx_drop_zero(bx_reverse(xs, empty_list(Nat))), empty_list(Nat))
    private fn bx_word_width(n: Nat, word: Nat) -> Nat decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(word, 0), 0, 1 + bx_word_width(pred(n), div(word, 2))))
    private fn bx_width(xs: List<Nat>) -> Nat =
        if_then_else(is_empty(xs), 0, mul(monus(length(xs), 1), 16) + bx_word_width(16, unwrap_or(head(bx_reverse(xs, empty_list(Nat))), 0)))
    private fn bx_words(b: ExactBudget, xs: List<Nat>) -> Bool =
        if_then_else(length(xs) > if_then_else(field(b, bits) > 1024, 1024, 64), false, fold(xs, true, fun(acc: Bool, word: Nat, and(acc, 65536 > word))))
    private fn bx_canonical(xs: List<Nat>) -> Bool =
        if_then_else(is_empty(xs), true, not(nat_eq(unwrap_or(head(bx_reverse(xs, empty_list(Nat))), 0), 0)))
    private fn bx_shape(b: ExactBudget, a: BigInt) -> Bool =
        if_then_else(not(bx_words(b, field(a, words))), false,
            and(bx_canonical(field(a, words)), if_then_else(is_empty(field(a, words)), not(field(a, negative)), true)))
    private fn bx_bool_same(a: Bool, b: Bool) -> Bool = if_then_else(a, b, not(b))
    private fn bx_lex(a: List<Nat>, b: List<Nat>) -> Int decreasing a fuel 4096 =
        if_then_else(is_empty(a), int(0), let x = unwrap_or(head(a), 0) in let y = unwrap_or(head(b), 0) in
            if_then_else(nat_eq(x, y), bx_lex(tail(a), tail(b)), if_then_else(x > y, int(1), -1)))
    private fn bx_cmp(a: List<Nat>, b: List<Nat>) -> Int =
        if_then_else(nat_eq(length(a), length(b)), bx_lex(bx_reverse(a, empty_list(Nat)), bx_reverse(b, empty_list(Nat))), if_then_else(length(a) > length(b), int(1), -1))
    private fn bx_add_loop(n: Nat, a: List<Nat>, b: List<Nat>, carry: Nat) -> List<Nat> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), empty_list(Nat),
            let v = unwrap_or(head(a), 0) + unwrap_or(head(b), 0) + carry in
            cons(mod(v, 65536), bx_add_loop(pred(n), tail(a), tail(b), div(v, 65536))))
    private fn bx_add(a: List<Nat>, b: List<Nat>) -> List<Nat> =
        bx_trim(bx_add_loop(if_then_else(length(a) > length(b), length(a), length(b)) + 1, a, b, 0))
    private fn bx_sub_loop(n: Nat, a: List<Nat>, b: List<Nat>, borrow: Nat) -> List<Nat> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), empty_list(Nat),
            let x = unwrap_or(head(a), 0) in let y = unwrap_or(head(b), 0) + borrow in
            cons(mod(monus(x + 65536, y), 65536), bx_sub_loop(pred(n), tail(a), tail(b), if_then_else(y > x, 1, 0))))
    private fn bx_sub(a: List<Nat>, b: List<Nat>) -> List<Nat> = bx_trim(bx_sub_loop(length(a), a, b, 0))
    private fn bx_scale_loop(xs: List<Nat>, word: Nat, carry: Nat) -> List<Nat> decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), if_then_else(nat_eq(carry, 0), empty_list(Nat), if_then_else(carry > 65535, list(mod(carry, 65536), div(carry, 65536)), list(carry))),
            let v = mul(unwrap_or(head(xs), 0), word) + carry in cons(mod(v, 65536), bx_scale_loop(tail(xs), word, div(v, 65536))))
    private fn bx_scale(xs: List<Nat>, word: Nat, carry: Nat) -> List<Nat> =
        if_then_else(nat_eq(word, 0), bx_trim(bx_scale_loop(xs, word, carry)), bx_scale_loop(xs, word, carry))
    private fn bx_mul_loop(xs: List<Nat>, shifted: List<Nat>, acc: List<Nat>) -> List<Nat> decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), acc,
            bx_mul_loop(tail(xs), if_then_else(is_empty(shifted), shifted, cons(0, shifted)), if_then_else(nat_eq(unwrap_or(head(xs), 0), 0), acc, bx_add(acc, bx_scale(shifted, unwrap_or(head(xs), 0), 0)))))
    private fn bx_mul(a: List<Nat>, b: List<Nat>) -> List<Nat> =
        if_then_else(length(a) > length(b), bx_mul_loop(b, a, empty_list(Nat)), bx_mul_loop(a, b, empty_list(Nat)))
    private fn bx_pow2(n: Nat) -> Nat decreasing n fuel 32 = if_then_else(nat_eq(n, 0), 1, mul(2, bx_pow2(pred(n))))
    private fn bx_bit(a: List<Nat>, i: Nat) -> Nat = mod(div(unwrap_or(index(a, div(i, 16)), 0), bx_pow2(mod(i, 16))), 2)
    private fn bx_div_digit(n: Nat, word: Nat, d: List<Nat>, q: Nat, r: List<Nat>) -> WordDigitDivision decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), WordDigitDivision(q, r),
            let shifted = bx_scale(r, 2, mod(div(word, bx_pow2(pred(n))), 2)) in
            let take = not(int_eq(bx_cmp(shifted, d), -1)) in
            bx_div_digit(pred(n), word, d, mul(q, 2) + if_then_else(take, 1, 0), if_then_else(take, bx_sub(shifted, d), shifted)))
    private fn bx_div_words(xs: List<Nat>, d: List<Nat>, q: List<Nat>, r: List<Nat>) -> WordDivision decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), WordDivision(q, r),
            let split = bx_div_digit(16, unwrap_or(head(xs), 0), d, 0, r) in
            let digit = field(split, quotient) in
            bx_div_words(tail(xs), d, if_then_else(and(is_empty(q), nat_eq(digit, 0)), q, cons(digit, q)), field(split, remainder)))
    private fn bx_div(a: List<Nat>, d: List<Nat>) -> WordDivision =
        if_then_else(and(nat_eq(length(d), 1), nat_eq(unwrap_or(head(d), 0), 1)), WordDivision(a, empty_list(Nat)),
            if_then_else(int_eq(bx_cmp(a, d), int(0)), WordDivision(list(1), empty_list(Nat)),
                if_then_else(int_eq(bx_cmp(a, d), -1), WordDivision(empty_list(Nat), a),
                    let difference = bx_sub(a, d) in if_then_else(int_eq(bx_cmp(difference, d), -1), WordDivision(list(1), difference), bx_div_words(bx_reverse(a, empty_list(Nat)), d, empty_list(Nat), empty_list(Nat))))))
    // v0.107: dense native kernel (BigNat builtins, N14). Same values as the list algorithms above, which stay
    // as the reference and the fallback (a zero divisor or a word outside 0..65535 never reaches BigNat).
    private fn bx_dadd(a: List<Nat>, b: List<Nat>) -> List<Nat> =
        match bn_from_words(a) { none => bx_add(a, b), some(x) => match bn_from_words(b) { none => bx_add(a, b), some(y) =>
            match bn_add(x, y) { none => bx_add(a, b), some(z) => bn_to_words(z) } } }
    private fn bx_dsub(a: List<Nat>, b: List<Nat>) -> List<Nat> =
        match bn_from_words(a) { none => bx_sub(a, b), some(x) => match bn_from_words(b) { none => bx_sub(a, b), some(y) => bn_to_words(bn_monus(x, y)) } }
    private fn bx_dmul(a: List<Nat>, b: List<Nat>) -> List<Nat> =
        match bn_from_words(a) { none => bx_mul(a, b), some(x) => match bn_from_words(b) { none => bx_mul(a, b), some(y) =>
            match bn_mul(x, y) { none => bx_mul(a, b), some(z) => bn_to_words(z) } } }
    private fn bx_ddiv(a: List<Nat>, d: List<Nat>) -> WordDivision =
        match bn_from_words(a) { none => bx_div(a, d), some(x) => match bn_from_words(d) { none => bx_div(a, d), some(y) =>
            match bn_div(x, y) { none => bx_div(a, d), some(q) => WordDivision(bn_to_words(q), bn_to_words(bn_monus(x, unwrap_or(bn_mul(q, y), x)))) } } }
    private fn bx_dcmp(a: List<Nat>, b: List<Nat>) -> Int =
        match bn_from_words(a) { none => bx_cmp(a, b), some(x) => match bn_from_words(b) { none => bx_cmp(a, b), some(y) => bn_cmp(x, y) } }
    private fn bx_one(a: List<Nat>) -> Bool = and(nat_eq(length(a), 1), nat_eq(unwrap_or(head(a), 0), 1))
    private fn bx_gcd_block(n: Nat, a: List<Nat>, b: List<Nat>) -> WordGcdState decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), WordGcdState(a, b),
            if_then_else(or(is_empty(b), bx_one(b)), WordGcdState(a, b), bx_gcd_block(pred(n), b, field(bx_ddiv(a, b), remainder))))
    private fn bx_gcd(n: Nat, a: List<Nat>, b: List<Nat>) -> Result<List<Nat>, Text> decreasing n fuel 16384 =
        if_then_else(is_empty(b), ok(a, Text), if_then_else(bx_one(b), ok(b, Text),
            if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: source gcd steps exhausted", List(Nat)),
                let block = bx_gcd_block(16, a, b) in bx_gcd(pred(n), field(block, a), field(block, b)))))
    private fn bx_small_div(xs: List<Nat>, d: Nat, carry: Nat, quotient: List<Nat>) -> WordSmallDivision decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), WordSmallDivision(quotient, carry),
            let v = mul(carry, 65536) + unwrap_or(head(xs), 0) in
            let digit = div(v, d) in bx_small_div(tail(xs), d, mod(v, d), if_then_else(and(is_empty(quotient), nat_eq(digit, 0)), quotient, cons(digit, quotient))))
    private fn bx_decimal(n: Nat, a: List<Nat>, text: Text) -> Result<Text, Text> decreasing n fuel 4096 =
        if_then_else(is_empty(a), ok(if_then_else(text_eq(text, ""), "0", text), Text),
            if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: source decimal steps exhausted", Text),
                let split = bx_small_div(bx_reverse(a, empty_list(Nat)), 1000000000, 0, empty_list(Nat)) in
                let chunk = nat_to_text(field(split, remainder)) in
                let digits = if_then_else(is_empty(field(split, quotient)), chunk, concat(substring("000000000", 0, monus(9, text_length(chunk))), chunk)) in
                bx_decimal(pred(n), field(split, quotient), concat(digits, text))))
    private fn bx_parse_digits(n: Nat, text: Text, i: Nat, a: List<Nat>, bits: Nat) -> Result<List<Nat>, Text> decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), ok(a, Text),
            match text_to_nat(substring(text, i, 1)) {
                none => err("invalid exact decimal", List(Nat)),
                some(d) => let next = bx_scale(a, 10, d) in
                    if_then_else(bx_width(next) > bits, err("UnknownWithinBudget: source precision exceeded", List(Nat)), bx_parse_digits(pred(n), text, i + 1, next, bits))
            })
    private fn bx_ten_power(n: Nat) -> Nat decreasing n fuel 16 = if_then_else(nat_eq(n, 0), 1, mul(10, bx_ten_power(pred(n))))
    private fn bx_parse_blocks(n: Nat, text: Text, i: Nat, a: List<Nat>, bits: Nat) -> Result<List<Nat>, Text> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), ok(a, Text),
            let count = if_then_else(monus(text_length(text), i) > 9, 9, monus(text_length(text), i)) in
            let parsed = if_then_else(bx_width(a) + 30 > bits, bx_parse_digits(count, text, i, a, bits),
                match text_to_nat(substring(text, i, count)) { none => err("invalid exact decimal", List(Nat)), some(chunk) => ok(bx_scale(a, bx_ten_power(count), chunk), Text) }) in
            match parsed { err(e) => err(e, List(Nat)), ok(next) => bx_parse_blocks(pred(n), text, i + count, next, bits) })
    private fn bx_unsigned(text: Text, bits: Nat) -> Result<List<Nat>, Text> =
        if_then_else(nat_eq(text_length(text), 0), err("invalid exact decimal", List(Nat)),
            match bn_from_text(text) {
                some(v) => if_then_else(bn_bits(v) > bits, err("UnknownWithinBudget: source precision exceeded", List(Nat)), ok(bn_to_words(v), Text)),
                none => bx_parse_blocks(div(text_length(text) + 8, 9), text, 0, empty_list(Nat), bits)
            })
    private fn bx_from_nat(n: Nat, value: Nat) -> List<Nat> decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), empty_list(Nat), if_then_else(nat_eq(value, 0), empty_list(Nat), cons(mod(value, 65536), bx_from_nat(pred(n), div(value, 65536)))))
    private fn bx_number(negative: Bool, words: List<Nat>) -> BigInt = BigInt(and(negative, not(is_empty(words))), words)
    private fn bx_max(a: Nat, b: Nat) -> Nat = if_then_else(a > b, a, b)
    private fn bx_min(a: Nat, b: Nat) -> Nat = if_then_else(a > b, b, a)
    private fn bx_cost(words: Nat) -> Nat =
        let n = mul(words, 2) + 3 in mul(20000, mul(mul(n, n), mul(n, n)))
    private fn bx_charge(b: ExactBudget, cost: Nat) -> Result<ExactBudget, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", ExactBudget),
            if_then_else(cost > field(b, work), err("UnknownWithinBudget: source work reservation exhausted", ExactBudget), ok(ExactBudget(field(b, bits), monus(field(b, work), cost)), Text)))
    private fn bx_prepare(b: ExactBudget, a: BigInt, c: BigInt) -> Result<ExactBudget, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", ExactBudget),
            if_then_else(not(and(bx_shape(b, a), bx_shape(b, c))), err("invalid canonical big integer", ExactBudget),
                if_then_else(or(bx_width(field(a, words)) > field(b, bits), bx_width(field(c, words)) > field(b, bits)), err("UnknownWithinBudget: source precision exceeded", ExactBudget), bx_charge(b, bx_cost(bx_max(length(field(a, words)), length(field(c, words))))))))
    private fn bx_finish(b: ExactBudget, negative: Bool, words: List<Nat>) -> Result<BigIntCalc, Text> =
        if_then_else(bx_width(words) > field(b, bits), err("UnknownWithinBudget: source precision exceeded", BigIntCalc), ok(BigIntCalc(bx_number(negative, words), b), Text))
    private fn bx_signed_add(a: BigInt, c: BigInt) -> BigInt =
        if_then_else(bx_bool_same(field(a, negative), field(c, negative)), bx_number(field(a, negative), bx_dadd(field(a, words), field(c, words))),
            if_then_else(int_eq(bx_dcmp(field(a, words), field(c, words)), -1), bx_number(field(c, negative), bx_dsub(field(c, words), field(a, words))), bx_number(field(a, negative), bx_dsub(field(a, words), field(c, words)))))

    fn big_from_words(b: ExactBudget, negative: Bool, words: List<Nat>) -> Result<BigIntCalc, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", BigIntCalc),
            if_then_else(not(bx_words(b, words)), err("invalid big integer words", BigIntCalc), match bx_charge(b, bx_cost(length(words))) { err(e) => err(e, BigIntCalc), ok(left) => bx_finish(left, negative, bx_trim(words)) }))
    fn big_from_int(b: ExactBudget, value: Int) -> Result<BigIntCalc, Text> = big_from_words(b, int(0) > value, bx_from_nat(8, abs(value)))
    fn big_parse(b: ExactBudget, text: Text) -> Result<BigIntCalc, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", BigIntCalc),
            if_then_else(text_length(text) > field(b, bits) + 2, err("UnknownWithinBudget: source decimal input exceeded", BigIntCalc),
                match bx_charge(b, bx_cost(div(field(b, bits) + 15, 16))) {
                    err(e) => err(e, BigIntCalc), ok(left) => let negative = text_eq(substring(text, 0, 1), "-") in
                        match bx_unsigned(if_then_else(negative, substring(text, 1, text_length(text)), text), field(b, bits)) { err(e) => err(e, BigIntCalc), ok(words) => bx_finish(left, negative, words) }
                }))
    fn big_add(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, c) { err(e) => err(e, BigIntCalc), ok(left) => let sum = bx_signed_add(a, c) in bx_finish(left, field(sum, negative), field(sum, words)) }
    fn big_sub(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, c) { err(e) => err(e, BigIntCalc), ok(left) => let sum = bx_signed_add(a, bx_number(not(field(c, negative)), field(c, words))) in bx_finish(left, field(sum, negative), field(sum, words)) }
    fn big_mul(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, c) { err(e) => err(e, BigIntCalc), ok(left) => bx_finish(left, not(bx_bool_same(field(a, negative), field(c, negative))), bx_dmul(field(a, words), field(c, words))) }
    fn big_neg(b: ExactBudget, a: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, BigInt(false, empty_list(Nat))) { err(e) => err(e, BigIntCalc), ok(left) => bx_finish(left, not(field(a, negative)), field(a, words)) }
    fn big_cmp(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigCompare, Text> =
        match bx_prepare(b, a, c) { err(e) => err(e, BigCompare), ok(left) =>
            let order = if_then_else(not(bx_bool_same(field(a, negative), field(c, negative))), if_then_else(field(a, negative), -1, int(1)), if_then_else(field(a, negative), -bx_dcmp(field(a, words), field(c, words)), bx_dcmp(field(a, words), field(c, words)))) in ok(BigCompare(order, left), Text) }
    fn big_div_rem(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigDivision, Text> =
        match bx_prepare(b, a, c) {
            err(e) => err(e, BigDivision), ok(left) => if_then_else(is_empty(field(c, words)), err("exact division by zero", BigDivision),
                let split = bx_ddiv(field(a, words), field(c, words)) in
                let adjust = and(field(a, negative), not(is_empty(field(split, remainder)))) in
                let q = if_then_else(adjust, bx_dadd(field(split, quotient), list(1)), field(split, quotient)) in
                let r = if_then_else(adjust, bx_dsub(field(c, words), field(split, remainder)), field(split, remainder)) in
                ok(BigDivision(bx_number(not(bx_bool_same(field(a, negative), field(c, negative))), q), bx_number(false, r), left), Text))
        }
    fn big_gcd(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, c) { err(e) => err(e, BigIntCalc), ok(left) => match bx_gcd(8192, field(a, words), field(c, words)) { err(e) => err(e, BigIntCalc), ok(words) => bx_finish(left, false, words) } }
    private fn bx_power(n: Nat, b: ExactBudget, a: BigInt, exponent: Nat, acc: BigInt) -> Result<BigIntCalc, Text> decreasing n fuel 256 =
        if_then_else(nat_eq(exponent, 0), ok(BigIntCalc(acc, b), Text),
            if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: source power steps exhausted", BigIntCalc),
                match if_then_else(nat_eq(mod(exponent, 2), 1), big_mul(b, acc, a), ok(BigIntCalc(acc, b), Text)) {
                    err(e) => err(e, BigIntCalc), ok(product) =>
                        if_then_else(nat_eq(div(exponent, 2), 0), ok(product, Text),
                            match big_mul(field(product, budget), a, a) { err(e) => err(e, BigIntCalc), ok(square) => bx_power(pred(n), field(square, budget), field(square, value), div(exponent, 2), field(product, value)) })
                }))
    fn big_pow(b: ExactBudget, a: BigInt, exponent: Nat) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, BigInt(false, empty_list(Nat))) { err(e) => err(e, BigIntCalc), ok(left) => bx_power(128, left, a, exponent, BigInt(false, list(1))) }
    fn big_lcm(b: ExactBudget, a: BigInt, c: BigInt) -> Result<BigIntCalc, Text> =
        match bx_prepare(b, a, c) {
            err(e) => err(e, BigIntCalc), ok(left) => if_then_else(or(is_empty(field(a, words)), is_empty(field(c, words))), ok(BigIntCalc(BigInt(false, empty_list(Nat)), left), Text),
                match big_gcd(left, a, c) { err(e) => err(e, BigIntCalc), ok(g) =>
                    match big_div_rem(field(g, budget), BigInt(false, field(a, words)), field(g, value)) { err(e) => err(e, BigIntCalc), ok(q) => big_mul(field(q, budget), field(q, quotient), BigInt(false, field(c, words))) }
                })
        }
    private fn bx_sqrt_loop(n: Nat, word: Nat, root: List<Nat>, remainder: List<Nat>) -> WordSquareRoot decreasing n fuel 16 =
        if_then_else(nat_eq(n, 0), WordSquareRoot(root, remainder),
            let bit = mul(pred(n), 2) in
            let digit = mod(div(word, bx_pow2(bit)), 4) in
            let shifted = bx_scale(remainder, 4, digit) in
            let trial = bx_scale(root, 4, 1) in
            let take = not(int_eq(bx_dcmp(shifted, trial), -1)) in
            bx_sqrt_loop(pred(n), word, bx_scale(root, 2, if_then_else(take, 1, 0)), if_then_else(take, bx_dsub(shifted, trial), shifted)))
    private fn bx_sqrt_word(acc: WordSquareRoot, word: Nat) -> WordSquareRoot =
        bx_sqrt_loop(8, word, field(acc, root), field(acc, remainder))
    fn big_sqrt_rem(b: ExactBudget, a: BigInt) -> Result<BigSquareRoot, Text> =
        match bx_prepare(b, a, BigInt(false, empty_list(Nat))) {
            err(e) => err(e, BigSquareRoot), ok(left) => if_then_else(field(a, negative), err("exact square root of negative integer", BigSquareRoot),
                let result = match bn_from_words(field(a, words)) {
                    none => fold(bx_reverse(field(a, words), empty_list(Nat)), WordSquareRoot(empty_list(Nat), empty_list(Nat)), bx_sqrt_word),
                    some(x) => let r = bn_sqrt(x) in WordSquareRoot(bn_to_words(r), bn_to_words(bn_monus(x, unwrap_or(bn_mul(r, r), x)))) } in
                ok(BigSquareRoot(BigInt(false, field(result, root)), BigInt(false, field(result, remainder)), left), Text))
        }
    fn big_text(b: ExactBudget, a: BigInt) -> Result<BigTextCalc, Text> =
        match bx_prepare(b, a, BigInt(false, empty_list(Nat))) { err(e) => err(e, BigTextCalc), ok(left) => let digits = match bn_from_words(field(a, words)) { some(v) => ok(bn_to_text(v), Text), none => bx_decimal(1024, field(a, words), "") } in match digits {
            err(e) => err(e, BigTextCalc), ok(text) => ok(BigTextCalc(concat(if_then_else(field(a, negative), "-", ""), text), left), Text)
        } }

    // v0.104: gcd(numerator, denominator) = 1 without a gcd when the denominator is 1 or a power of two
    // over an odd numerator (dyadic values), otherwise by the word gcd; the answer is the same.
    private fn bx_pow2_word(n: Nat, w: Nat) -> Bool decreasing n fuel 20 =
        if_then_else(nat_eq(n, 0), false, if_then_else(nat_eq(w, 1), true, if_then_else(nat_eq(mod(w, 2), 1), false, bx_pow2_word(pred(n), div(w, 2)))))
    private fn bx_pow2_words(xs: List<Nat>) -> Bool decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), false, if_then_else(is_empty(tail(xs)), bx_pow2_word(17, unwrap_or(head(xs), 0)), and(nat_eq(unwrap_or(head(xs), 1), 0), bx_pow2_words(tail(xs)))))
    // dyadic normalization: gcd(n, 2^e) = 2^min(tz(n), e), removed by a word shift
    private fn bx_tz_word(n: Nat, w: Nat) -> Nat decreasing n fuel 20 = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(mod(w, 2), 1), 0, 1 + bx_tz_word(pred(n), div(w, 2))))
    private fn bx_tz(xs: List<Nat>) -> Nat decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), 0, if_then_else(nat_eq(unwrap_or(head(xs), 0), 0), 16 + bx_tz(tail(xs)), bx_tz_word(17, unwrap_or(head(xs), 1))))
    private fn bx_drop(n: Nat, xs: List<Nat>) -> List<Nat> decreasing n fuel 4096 = if_then_else(nat_eq(n, 0), xs, bx_drop(pred(n), tail(xs)))
    private fn bx_shr_bits(xs: List<Nat>, p: Nat) -> List<Nat> decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), empty_list(Nat), cons(div(unwrap_or(head(xs), 0), p) + mul(mod(unwrap_or(head(tail(xs)), 0), p), div(65536, p)), bx_shr_bits(tail(xs), p)))
    private fn bx_shr(xs: List<Nat>, t: Nat) -> List<Nat> = bx_trim(bx_shr_bits(bx_drop(div(t, 16), xs), bx_pow2(mod(t, 16))))
    private fn bq_coprime(a: BigRational) -> Result<Bool, Text> =
        if_then_else(or(bx_one(field(a, denominator)), and(nat_eq(mod(unwrap_or(head(field(a, numerator)), 0), 2), 1), bx_pow2_words(field(a, denominator)))), ok(true, Text),
            match bx_gcd(8192, field(a, numerator), field(a, denominator)) { err(e) => err(e, Bool), ok(g) => ok(bx_one(g), Text) })
    private fn bq_shape(b: ExactBudget, a: BigRational) -> Bool =
        if_then_else(not(and(bx_words(b, field(a, numerator)), bx_words(b, field(a, denominator)))), false,
            and(and(bx_canonical(field(a, numerator)), bx_canonical(field(a, denominator))),
                and(not(is_empty(field(a, denominator))), if_then_else(is_empty(field(a, numerator)), not(field(a, negative)), true))))
    private fn bq_size(a: BigRational) -> Nat = bx_max(length(field(a, numerator)), length(field(a, denominator)))
    private fn bq_prepare(b: ExactBudget, a: BigRational, c: BigRational) -> Result<ExactBudget, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", ExactBudget),
            if_then_else(not(and(bq_shape(b, a), bq_shape(b, c))), err("invalid canonical big rational", ExactBudget),
                if_then_else(bx_max(bx_max(bx_width(field(a, numerator)), bx_width(field(a, denominator))), bx_max(bx_width(field(c, numerator)), bx_width(field(c, denominator)))) > field(b, bits), err("UnknownWithinBudget: source precision exceeded", ExactBudget),
                match bx_charge(b, bx_cost(bx_max(bq_size(a), bq_size(c)))) {
                    err(e) => err(e, ExactBudget), ok(left) => match bq_coprime(a) {
                        err(e) => err(e, ExactBudget), ok(g) => if_then_else(not(g), err("noncanonical big rational", ExactBudget),
                            match bq_coprime(c) { err(e) => err(e, ExactBudget), ok(h) => if_then_else(h, ok(left, Text), err("noncanonical big rational", ExactBudget)) })
                    }
                })))
    private fn bq_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn bq_finish(b: ExactBudget, negative: Bool, numerator: List<Nat>, denominator: List<Nat>) -> Result<BigRationalCalc, Text> =
        if_then_else(is_empty(denominator), err("exact rational denominator is zero", BigRationalCalc),
            if_then_else(is_empty(numerator), ok(BigRationalCalc(bq_zero(), b), Text),
                let dyadic = bx_pow2_words(denominator) in
                match if_then_else(or(bx_one(denominator), dyadic), ok(list(1), Text), bx_gcd(8192, numerator, denominator)) {
                    err(e) => err(e, BigRationalCalc), ok(g) =>
                        let t = if_then_else(dyadic, bx_min(bx_tz(numerator), pred(bx_width(denominator))), 0) in
                        let n = if_then_else(dyadic, bx_shr(numerator, t), field(bx_ddiv(numerator, g), quotient)) in
                        let d = if_then_else(dyadic, bx_shr(denominator, t), field(bx_ddiv(denominator, g), quotient)) in
                        if_then_else(or(bx_width(n) > field(b, bits), bx_width(d) > field(b, bits)), err("UnknownWithinBudget: source precision exceeded", BigRationalCalc), ok(BigRationalCalc(BigRational(negative, n, d), b), Text))
                }))
    fn bigrat_from_ints(b: ExactBudget, n: BigInt, d: BigInt) -> Result<BigRationalCalc, Text> =
        match bx_prepare(b, n, d) { err(e) => err(e, BigRationalCalc), ok(left) => bq_finish(left, not(bx_bool_same(field(n, negative), field(d, negative))), field(n, words), field(d, words)) }
    private fn bq_slash_block(n: Nat, text: Text, i: Nat) -> Option<Nat> decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(text_eq(substring(text, i, 1), "/"), some(i), bq_slash_block(pred(n), text, i + 1)))
    private fn bq_slash(n: Nat, text: Text, i: Nat) -> Option<Nat> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), none(Nat), match bq_slash_block(16, text, i) { some(found) => some(found), none => bq_slash(pred(n), text, i + 16) })
    fn bigrat_parse(b: ExactBudget, text: Text) -> Result<BigRationalCalc, Text> =
        if_then_else(not(bx_budget_valid(b)), err("invalid exact source budget", BigRationalCalc),
            if_then_else(text_length(text) > mul(2, field(b, bits)) + 3, err("UnknownWithinBudget: source rational input exceeded", BigRationalCalc),
                match bx_charge(b, bx_cost(div(field(b, bits) + 15, 16))) {
                    err(e) => err(e, BigRationalCalc), ok(left) =>
                        let negative = text_eq(substring(text, 0, 1), "-") in
                        let body = if_then_else(negative, substring(text, 1, text_length(text)), text) in
                        let slash = bq_slash(div(text_length(body) + 15, 16), body, 0) in
                        let numerator = match slash { none => body, some(i) => substring(body, 0, i) } in
                        let denominator = match slash { none => "1", some(i) => substring(body, i + 1, text_length(body)) } in
                        match bx_unsigned(numerator, field(b, bits)) { err(e) => err(e, BigRationalCalc), ok(n) => match bx_unsigned(denominator, field(b, bits)) { err(e) => err(e, BigRationalCalc), ok(d) => bq_finish(left, negative, n, d) } }
                }))
    private fn bq_add(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        let sum = bx_signed_add(bx_number(field(a, negative), bx_dmul(field(a, numerator), field(c, denominator))), bx_number(field(c, negative), bx_dmul(field(c, numerator), field(a, denominator)))) in
        bq_finish(b, field(sum, negative), field(sum, words), bx_dmul(field(a, denominator), field(c, denominator)))
    fn bigrat_add(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, c) { err(e) => err(e, BigRationalCalc), ok(left) => bq_add(left, a, c) }
    fn bigrat_sub(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, c) { err(e) => err(e, BigRationalCalc), ok(left) => bq_add(left, a, BigRational(and(not(field(c, negative)), not(is_empty(field(c, numerator)))), field(c, numerator), field(c, denominator))) }
    fn bigrat_mul(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, c) { err(e) => err(e, BigRationalCalc), ok(left) => bq_finish(left, not(bx_bool_same(field(a, negative), field(c, negative))), bx_dmul(field(a, numerator), field(c, numerator)), bx_dmul(field(a, denominator), field(c, denominator))) }
    fn bigrat_div(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, c) { err(e) => err(e, BigRationalCalc), ok(left) => if_then_else(is_empty(field(c, numerator)), err("exact rational division by zero", BigRationalCalc), bq_finish(left, not(bx_bool_same(field(a, negative), field(c, negative))), bx_dmul(field(a, numerator), field(c, denominator)), bx_dmul(field(a, denominator), field(c, numerator)))) }
    fn bigrat_inv(b: ExactBudget, a: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, bq_zero()) { err(e) => err(e, BigRationalCalc), ok(left) => if_then_else(is_empty(field(a, numerator)), err("exact rational division by zero", BigRationalCalc), ok(BigRationalCalc(BigRational(field(a, negative), field(a, denominator), field(a, numerator)), left), Text)) }
    fn bigrat_neg(b: ExactBudget, a: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, bq_zero()) { err(e) => err(e, BigRationalCalc), ok(left) => ok(BigRationalCalc(BigRational(and(not(field(a, negative)), not(is_empty(field(a, numerator)))), field(a, numerator), field(a, denominator)), left), Text) }
    fn bigrat_cmp(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigCompare, Text> =
        match bq_prepare(b, a, c) { err(e) => err(e, BigCompare), ok(left) =>
            let order = if_then_else(not(bx_bool_same(field(a, negative), field(c, negative))), if_then_else(field(a, negative), -1, int(1)),
                let magnitude = bx_dcmp(bx_dmul(field(a, numerator), field(c, denominator)), bx_dmul(field(c, numerator), field(a, denominator))) in if_then_else(field(a, negative), -magnitude, magnitude)) in ok(BigCompare(order, left), Text) }
    fn bigrat_check(b: ExactBudget, a: BigRational) -> Result<BigRationalCalc, Text> =
        match bq_prepare(b, a, bq_zero()) { err(e) => err(e, BigRationalCalc), ok(left) => ok(BigRationalCalc(a, left), Text) }
    private fn bq_power(n: Nat, b: ExactBudget, a: BigRational, exponent: Nat, acc: BigRational) -> Result<BigRationalCalc, Text> decreasing n fuel 256 =
        if_then_else(nat_eq(exponent, 0), ok(BigRationalCalc(acc, b), Text),
            if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: source rational power steps exhausted", BigRationalCalc),
                match if_then_else(nat_eq(mod(exponent, 2), 1), bigrat_mul(b, acc, a), ok(BigRationalCalc(acc, b), Text)) {
                    err(e) => err(e, BigRationalCalc), ok(product) =>
                        if_then_else(nat_eq(div(exponent, 2), 0), ok(product, Text),
                            match bigrat_mul(field(product, budget), a, a) { err(e) => err(e, BigRationalCalc), ok(square) => bq_power(pred(n), field(square, budget), field(square, value), div(exponent, 2), field(product, value)) })
                }))
    fn bigrat_pow(b: ExactBudget, a: BigRational, exponent: Int) -> Result<BigRationalCalc, Text> =
        match bigrat_check(b, a) { err(e) => err(e, BigRationalCalc), ok(checked) =>
            match if_then_else(int(0) > exponent, bigrat_inv(field(checked, budget), field(checked, value)), ok(checked, Text)) {
                err(e) => err(e, BigRationalCalc), ok(base) => bq_power(128, field(base, budget), field(base, value), abs(exponent), BigRational(false, list(1), list(1)))
            }
        }
    fn bigrat_text(b: ExactBudget, a: BigRational) -> Result<BigTextCalc, Text> =
        match bq_prepare(b, a, bq_zero()) { err(e) => err(e, BigTextCalc), ok(left) => match bx_decimal(1024, field(a, numerator), "") { err(e) => err(e, BigTextCalc), ok(n) => match bx_decimal(1024, field(a, denominator), "") {
            err(e) => err(e, BigTextCalc), ok(d) => ok(BigTextCalc(concat(if_then_else(field(a, negative), "-", ""), concat(n, concat("/", d))), left), Text)
        } } }

    // Closed legacy finite computations, not generic arithmetic proof terms.
    theorem zero_has_no_negative_sign: not(field(field(unwrap_or(big_from_words(exact_default_budget(), true, empty_list(Nat)), BigIntCalc(BigInt(true, empty_list(Nat)), exact_default_budget())), value), negative))
    theorem invalid_word_is_rejected: is_err(big_from_words(exact_default_budget(), false, list(65536)))
}
