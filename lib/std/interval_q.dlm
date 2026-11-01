// Closed rational intervals [lo, hi] in ordinary DLM over std.exact BigRational.
// Raw records are revalidated (canonical endpoints, lo <= hi) by every operation;
// budgets are threaded; endpoint arithmetic is exact. See docs/EXACT_INTERVALS.md.
module std.interval_q
import std.exact

theory StdIntervalQ extends StdExact {
    record IntervalQ(lo: BigRational, hi: BigRational)
    record IntervalQCalc(value: IntervalQ, budget: ExactBudget)
    record IntervalQFlag(value: Bool, budget: ExactBudget)
    record IntervalQMeet(nonempty: Bool, value: IntervalQ, budget: ExactBudget)
    // order: -1 certainly less, 1 certainly greater, 0 the same point, 2 undecided (overlap)
    record IntervalQOrder(order: Int, budget: ExactBudget)
    private fn iv_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn iv_two() -> BigRational = BigRational(false, list(2), list(1))
    private fn iv_done(lo: BigRational, hi: BigRational, b: ExactBudget) -> Result<IntervalQCalc, Text> = ok(IntervalQCalc(IntervalQ(lo, hi), b), Text)
    private fn iv_is_zero(a: BigRational) -> Bool = is_empty(field(a, numerator))
    // Smaller / larger of two validated rationals.
    private fn iv_pick(b: ExactBudget, a: BigRational, c: BigRational, larger: Bool) -> Result<BigRationalCalc, Text> =
        match bigrat_cmp(b, a, c) { err(e) => err(e, BigRationalCalc), ok(o) =>
            ok(BigRationalCalc(if_then_else(int_eq(field(o, order), if_then_else(larger, -1, int(1))), c, a), field(o, budget)), Text) }
    private fn iv_min4(b: ExactBudget, p: BigRational, q: BigRational, r: BigRational, s: BigRational, larger: Bool) -> Result<BigRationalCalc, Text> =
        match iv_pick(b, p, q, larger) { err(e) => err(e, BigRationalCalc), ok(x) =>
        match iv_pick(field(x, budget), field(x, value), r, larger) { err(e) => err(e, BigRationalCalc), ok(y) =>
            iv_pick(field(y, budget), field(y, value), s, larger) } }
    // sqrt bounds of a validated nonnegative x = p/q at resolution k:
    // s = isqrt(p*q*4^k); lower = s/(q*2^k); upper = lower if exact, else (s+1)/(q*2^k).
    private fn iv_sqrt_bound(b: ExactBudget, x: BigRational, k: Nat, upper: Bool) -> Result<BigRationalCalc, Text> =
        if_then_else(iv_is_zero(x), ok(BigRationalCalc(iv_zero(), b), Text),
        match big_pow(b, BigInt(false, list(2)), k) { err(e) => err(e, BigRationalCalc), ok(scale) =>
        match big_mul(field(scale, budget), field(scale, value), field(scale, value)) { err(e) => err(e, BigRationalCalc), ok(square) =>
        match big_mul(field(square, budget), BigInt(false, field(x, numerator)), BigInt(false, field(x, denominator))) { err(e) => err(e, BigRationalCalc), ok(pq) =>
        match big_mul(field(pq, budget), field(pq, value), field(square, value)) { err(e) => err(e, BigRationalCalc), ok(radicand) =>
        match big_sqrt_rem(field(radicand, budget), field(radicand, value)) { err(e) => err(e, BigRationalCalc), ok(root) =>
        match big_mul(field(root, budget), BigInt(false, field(x, denominator)), field(scale, value)) { err(e) => err(e, BigRationalCalc), ok(den) =>
        match if_then_else(and(upper, not(is_empty(field(field(root, remainder), words)))), big_add(field(den, budget), field(root, root), BigInt(false, list(1))), ok(BigIntCalc(field(root, root), field(den, budget)), Text)) {
            err(e) => err(e, BigRationalCalc), ok(num) => bigrat_from_ints(field(num, budget), field(num, value), field(den, value)) } } } } } } })

    fn ivq_new(b: ExactBudget, lo: BigRational, hi: BigRational) -> Result<IntervalQCalc, Text> =
        match bigrat_cmp(b, lo, hi) { err(e) => err(e, IntervalQCalc), ok(o) =>
            if_then_else(int_eq(field(o, order), int(1)), err("interval bounds out of order", IntervalQCalc), iv_done(lo, hi, field(o, budget))) }
    fn ivq_check(b: ExactBudget, x: IntervalQ) -> Result<IntervalQCalc, Text> = ivq_new(b, field(x, lo), field(x, hi))
    fn ivq_point(b: ExactBudget, q: BigRational) -> Result<IntervalQCalc, Text> =
        match bigrat_check(b, q) { err(e) => err(e, IntervalQCalc), ok(v) => iv_done(q, q, field(v, budget)) }
    fn ivq_contains(b: ExactBudget, x: IntervalQ, q: BigRational) -> Result<IntervalQFlag, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQFlag), ok(c) =>
        match bigrat_cmp(field(c, budget), field(x, lo), q) { err(e) => err(e, IntervalQFlag), ok(l) =>
        match bigrat_cmp(field(l, budget), q, field(x, hi)) { err(e) => err(e, IntervalQFlag), ok(h) =>
            ok(IntervalQFlag(and(not(int_eq(field(l, order), int(1))), not(int_eq(field(h, order), int(1)))), field(h, budget)), Text) } } }
    fn ivq_contains_zero(b: ExactBudget, x: IntervalQ) -> Result<IntervalQFlag, Text> = ivq_contains(b, x, iv_zero())
    // x is a subset of y
    fn ivq_subset(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQFlag, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQFlag), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQFlag), ok(cy) =>
        match bigrat_cmp(field(cy, budget), field(y, lo), field(x, lo)) { err(e) => err(e, IntervalQFlag), ok(l) =>
        match bigrat_cmp(field(l, budget), field(x, hi), field(y, hi)) { err(e) => err(e, IntervalQFlag), ok(h) =>
            ok(IntervalQFlag(and(not(int_eq(field(l, order), int(1))), not(int_eq(field(h, order), int(1)))), field(h, budget)), Text) } } } }
    fn ivq_hull(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQCalc), ok(cy) =>
        match iv_pick(field(cy, budget), field(x, lo), field(y, lo), false) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match iv_pick(field(l, budget), field(x, hi), field(y, hi), true) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } } } }
    // The common part; nonempty = false (and value = x) when x and y are disjoint.
    fn ivq_intersect(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQMeet, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQMeet), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQMeet), ok(cy) =>
        match iv_pick(field(cy, budget), field(x, lo), field(y, lo), true) { err(e) => err(e, IntervalQMeet), ok(l) =>
        match iv_pick(field(l, budget), field(x, hi), field(y, hi), false) { err(e) => err(e, IntervalQMeet), ok(h) =>
        match bigrat_cmp(field(h, budget), field(l, value), field(h, value)) { err(e) => err(e, IntervalQMeet), ok(o) =>
            if_then_else(int_eq(field(o, order), int(1)), ok(IntervalQMeet(false, x, field(o, budget)), Text),
                ok(IntervalQMeet(true, IntervalQ(field(l, value), field(h, value)), field(o, budget)), Text)) } } } } }
    fn ivq_compare(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQOrder, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQOrder), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQOrder), ok(cy) =>
        match bigrat_cmp(field(cy, budget), field(x, hi), field(y, lo)) { err(e) => err(e, IntervalQOrder), ok(a) =>
        match bigrat_cmp(field(a, budget), field(y, hi), field(x, lo)) { err(e) => err(e, IntervalQOrder), ok(c) =>
        match bigrat_cmp(field(c, budget), field(x, lo), field(x, hi)) { err(e) => err(e, IntervalQOrder), ok(px) =>
        match bigrat_cmp(field(px, budget), field(y, lo), field(y, hi)) { err(e) => err(e, IntervalQOrder), ok(py) =>
        match bigrat_cmp(field(py, budget), field(x, lo), field(y, lo)) { err(e) => err(e, IntervalQOrder), ok(same) =>
            ok(IntervalQOrder(if_then_else(int_eq(field(a, order), -1), -1, if_then_else(int_eq(field(c, order), -1), int(1),
                if_then_else(and(and(int_eq(field(px, order), int(0)), int_eq(field(py, order), int(0))), int_eq(field(same, order), int(0))), int(0), int(2)))), field(same, budget)), Text) } } } } } } }
    fn ivq_width(b: ExactBudget, x: IntervalQ) -> Result<BigRationalCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, BigRationalCalc), ok(c) => bigrat_sub(field(c, budget), field(x, hi), field(x, lo)) }
    fn ivq_midpoint(b: ExactBudget, x: IntervalQ) -> Result<BigRationalCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, BigRationalCalc), ok(c) =>
        match bigrat_add(field(c, budget), field(x, lo), field(x, hi)) { err(e) => err(e, BigRationalCalc), ok(s) => bigrat_div(field(s, budget), field(s, value), iv_two()) } }
    fn ivq_add(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQCalc), ok(cy) =>
        match bigrat_add(field(cy, budget), field(x, lo), field(y, lo)) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match bigrat_add(field(l, budget), field(x, hi), field(y, hi)) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } } } }
    fn ivq_sub(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQCalc), ok(cy) =>
        match bigrat_sub(field(cy, budget), field(x, lo), field(y, hi)) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match bigrat_sub(field(l, budget), field(x, hi), field(y, lo)) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } } } }
    fn ivq_neg(b: ExactBudget, x: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(c) =>
        match bigrat_neg(field(c, budget), field(x, hi)) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match bigrat_neg(field(l, budget), field(x, lo)) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } } }
    // Endpoint products lo*lo', lo*hi', hi*lo', hi*hi' and their min and max.
    fn ivq_mul(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(cx) =>
        match ivq_check(field(cx, budget), y) { err(e) => err(e, IntervalQCalc), ok(cy) =>
        match bigrat_mul(field(cy, budget), field(x, lo), field(y, lo)) { err(e) => err(e, IntervalQCalc), ok(p) =>
        match bigrat_mul(field(p, budget), field(x, lo), field(y, hi)) { err(e) => err(e, IntervalQCalc), ok(q) =>
        match bigrat_mul(field(q, budget), field(x, hi), field(y, lo)) { err(e) => err(e, IntervalQCalc), ok(r) =>
        match bigrat_mul(field(r, budget), field(x, hi), field(y, hi)) { err(e) => err(e, IntervalQCalc), ok(s) =>
        match iv_min4(field(s, budget), field(p, value), field(q, value), field(r, value), field(s, value), false) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match iv_min4(field(l, budget), field(p, value), field(q, value), field(r, value), field(s, value), true) { err(e) => err(e, IntervalQCalc), ok(h) =>
            iv_done(field(l, value), field(h, value), field(h, budget)) } } } } } } } }
    // {x*x : x in X}: [0, max(lo^2, hi^2)] when 0 is in X, otherwise the ordered squares.
    fn ivq_square(b: ExactBudget, x: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(c) =>
        match bigrat_mul(field(c, budget), field(x, lo), field(x, lo)) { err(e) => err(e, IntervalQCalc), ok(p) =>
        match bigrat_mul(field(p, budget), field(x, hi), field(x, hi)) { err(e) => err(e, IntervalQCalc), ok(q) =>
        match iv_pick(field(q, budget), field(p, value), field(q, value), false) { err(e) => err(e, IntervalQCalc), ok(small) =>
        match iv_pick(field(small, budget), field(p, value), field(q, value), true) { err(e) => err(e, IntervalQCalc), ok(large) =>
        match ivq_contains_zero(field(large, budget), x) { err(e) => err(e, IntervalQCalc), ok(z) =>
            iv_done(if_then_else(field(z, value), iv_zero(), field(small, value)), field(large, value), field(z, budget)) } } } } } }
    fn ivq_abs(b: ExactBudget, x: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(c) =>
            if_then_else(not(field(field(x, lo), negative)), ok(c, Text),
            match bigrat_neg(field(c, budget), field(x, lo)) { err(e) => err(e, IntervalQCalc), ok(a) =>
            match bigrat_neg(field(a, budget), field(x, hi)) { err(e) => err(e, IntervalQCalc), ok(h) =>
                if_then_else(or(field(field(x, hi), negative), iv_is_zero(field(x, hi))), iv_done(field(h, value), field(a, value), field(h, budget)),
                    match iv_pick(field(h, budget), field(a, value), field(x, hi), true) { err(e) => err(e, IntervalQCalc), ok(m) => iv_done(iv_zero(), field(m, value), field(m, budget)) }) } }) }
    // [1/hi, 1/lo]; an interval containing zero is refused.
    fn ivq_inv(b: ExactBudget, x: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_contains_zero(b, x) { err(e) => err(e, IntervalQCalc), ok(z) =>
            if_then_else(field(z, value), err("interval division by an interval containing zero", IntervalQCalc),
            match bigrat_inv(field(z, budget), field(x, hi)) { err(e) => err(e, IntervalQCalc), ok(l) =>
            match bigrat_inv(field(l, budget), field(x, lo)) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } }) }
    fn ivq_div(b: ExactBudget, x: IntervalQ, y: IntervalQ) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(cx) =>
        match ivq_inv(field(cx, budget), y) { err(e) => err(e, IntervalQCalc), ok(i) => ivq_mul(field(i, budget), x, field(i, value)) } }
    // Guaranteed enclosure of {sqrt(x) : x in X} for X >= 0, each bound within 2^-k.
    fn ivq_sqrt(b: ExactBudget, x: IntervalQ, k: Nat) -> Result<IntervalQCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, IntervalQCalc), ok(c) =>
            if_then_else(k > 16384, err("interval square root resolution exceeds 16384 bits", IntervalQCalc),
            if_then_else(field(field(x, lo), negative), err("interval square root of negative values", IntervalQCalc),
            match iv_sqrt_bound(field(c, budget), field(x, lo), k, false) { err(e) => err(e, IntervalQCalc), ok(l) =>
            match iv_sqrt_bound(field(l, budget), field(x, hi), k, true) { err(e) => err(e, IntervalQCalc), ok(h) => iv_done(field(l, value), field(h, value), field(h, budget)) } })) }
    private fn iv_comma_block(n: Nat, text: Text, i: Nat, found: Option<Nat>) -> Option<Nat> decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), found, iv_comma_block(pred(n), text, i + 1, if_then_else(text_eq(substring(text, i, 1), ","), some(i), found)))
    private fn iv_comma(n: Nat, text: Text, i: Nat, found: Option<Nat>) -> Option<Nat> decreasing n fuel 8192 =
        if_then_else(nat_eq(n, 0), found, iv_comma(pred(n), text, i + 16, iv_comma_block(16, text, i, found)))
    // Strict text [<lo>,<hi>]: exactly one comma, endpoints as accepted by bigrat_parse.
    fn ivq_parse(b: ExactBudget, text: Text) -> Result<IntervalQCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, IntervalQCalc), ok(valid) =>
            if_then_else(text_length(text) > mul(4, field(b, bits)) + 8, err("UnknownWithinBudget: exact interval input exceeded", IntervalQCalc),
            if_then_else(or(not(text_length(text) > 1), or(not(text_eq(substring(text, 0, 1), "[")), not(text_eq(substring(text, pred(text_length(text)), 1), "]")))), err("invalid exact interval text: expected [<lo>,<hi>]", IntervalQCalc),
                let body = substring(text, 1, pred(pred(text_length(text)))) in
                match iv_comma(div(text_length(body) + 15, 16), body, 0, none(Nat)) {
                    none => err("invalid exact interval text: expected [<lo>,<hi>]", IntervalQCalc),
                    some(c) => match bigrat_parse(valid, substring(body, 0, c)) { err(e) => err(e, IntervalQCalc), ok(l) =>
                        match bigrat_parse(field(l, budget), substring(body, c + 1, text_length(body))) { err(e) => err(e, IntervalQCalc), ok(h) =>
                            ivq_new(field(h, budget), field(l, value), field(h, value)) } }
                })) }
    fn ivq_text(b: ExactBudget, x: IntervalQ) -> Result<BigTextCalc, Text> =
        match ivq_check(b, x) { err(e) => err(e, BigTextCalc), ok(c) =>
        match bigrat_text(field(c, budget), field(x, lo)) { err(e) => err(e, BigTextCalc), ok(l) =>
        match bigrat_text(field(l, budget), field(x, hi)) { err(e) => err(e, BigTextCalc), ok(h) =>
            ok(BigTextCalc(concat("[", concat(field(l, value), concat(",", concat(field(h, value), "]")))), field(h, budget)), Text) } } }

    // Closed legacy finite computations, not general enclosure proofs.
    theorem zero_divisor_is_refused: is_err(ivq_inv(exact_default_budget(), IntervalQ(BigRational(true, list(1), list(1)), BigRational(false, list(1), list(1)))))
    theorem reversed_bounds_are_refused: is_err(ivq_check(exact_default_budget(), IntervalQ(BigRational(false, list(1), list(1)), iv_zero())))
}
