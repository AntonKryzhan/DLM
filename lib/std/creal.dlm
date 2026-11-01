// Computable reals as finite expression programs over std.interval_q (numeric criterion N08).
// A program is a nonempty list of at most 64 nodes; node i is a rational constant (op 0) or an
// operation on earlier nodes; the last node is the value. creal_approx returns an interval of width
// <= 2^-n containing the real, a domain error proved by the bounds, or UnknownWithinBudget (never a
// refutation). The refinement schedule (square-root resolution (n + 8) * 2^r, r < 6, at most 16384)
// and every result are the host's (math::creal). See docs/COMPUTABLE_REALS.md.
module std.creal
import std.interval_q

theory StdCReal extends StdIntervalQ {
    // op: 0 c, 1 add, 2 sub, 3 mul, 4 neg, 5 inv, 6 div, 7 sqrt, 8 square, 9 abs; unused operands are 0
    record CRealNode(op: Nat, a: Nat, b: Nat, value: BigRational)
    record CReal(nodes: List<CRealNode>)
    record CRealCalc(value: CReal, budget: ExactBudget)
    // retry: the bounds did not decide a divisor sign or a square-root domain at this resolution
    record CRealStep(retry: Bool, value: IntervalQ, budget: ExactBudget)
    private record CRealNodeCalc(value: CRealNode, budget: ExactBudget)
    private fn cr_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn cr_is_zero(a: BigRational) -> Bool = is_empty(field(a, numerator))
    private fn cr_canonical_zero(a: BigRational) -> Bool =
        and(and(not(field(a, negative)), is_empty(field(a, numerator))), and(nat_eq(length(field(a, denominator)), 1), nat_eq(unwrap_or(head(field(a, denominator)), 0), 1)))
    private fn cr_arity(op: Nat) -> Nat = if_then_else(or(nat_eq(op, 0), nat_eq(op, 14)), 0, if_then_else(or(or(nat_eq(op, 1), nat_eq(op, 2)), or(nat_eq(op, 3), nat_eq(op, 6))), 2, 1))
    private fn cr_name(op: Nat) -> Text = unwrap_or(index(list("c", "add", "sub", "mul", "neg", "inv", "div", "sqrt", "square", "abs", "exp", "log", "sin", "cos", "pi"), op), "")
    private fn cr_code(n: Nat, name: Text, op: Nat) -> Option<Nat> decreasing n fuel 16 =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(text_eq(cr_name(op), name), some(op), cr_code(pred(n), name, op + 1)))
    private fn cr_check_nodes(n: Nat, b: ExactBudget, nodes: List<CRealNode>, i: Nat) -> Result<ExactBudget, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), ok(b, Text),
        match head(nodes) { none => ok(b, Text), some(node) =>
            let arity = cr_arity(field(node, op)) in
            if_then_else(field(node, op) > 14, err("invalid computable real operation", ExactBudget),
            if_then_else(or(and(arity > 0, not(i > field(node, a))), and(arity > 1, not(i > field(node, b)))), err("computable real operand must refer to an earlier node", ExactBudget),
            if_then_else(or(and(nat_eq(arity, 0), not(nat_eq(field(node, a), 0))), and(not(arity > 1), not(nat_eq(field(node, b), 0)))), err("computable real unused operands must be 0", ExactBudget),
            match if_then_else(nat_eq(field(node, op), 0), match bigrat_check(b, field(node, value)) { err(e) => err(e, ExactBudget), ok(v) => ok(field(v, budget), Text) },
                    if_then_else(cr_canonical_zero(field(node, value)), ok(b, Text), err("computable real non-constant node must carry 0", ExactBudget))) {
                err(e) => err(e, ExactBudget), ok(next) => cr_check_nodes(pred(n), next, tail(nodes), i + 1) }))) })
    // Validates a raw program: 1..64 nodes, operands earlier, unused operands 0, constants checked, other values 0.
    fn creal_check(b: ExactBudget, x: CReal) -> Result<CRealCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, CRealCalc), ok(valid) =>
            if_then_else(or(is_empty(field(x, nodes)), length(field(x, nodes)) > 64), err("computable real program must have 1..64 nodes", CRealCalc),
            match cr_check_nodes(64, valid, field(x, nodes), 0) { err(e) => err(e, CRealCalc), ok(left) => ok(CRealCalc(x, left), Text) }) }
    fn creal_const(b: ExactBudget, q: BigRational) -> Result<CRealCalc, Text> = creal_check(b, CReal(list(CRealNode(0, 0, 0, q))))

    private fn cr_at(vals: List<IntervalQ>, count: Nat, i: Nat) -> IntervalQ = unwrap_or(index(vals, monus(pred(count), i)), IntervalQ(cr_zero(), cr_zero()))
    private fn cr_step(r: Result<IntervalQCalc, Text>) -> Result<CRealStep, Text> =
        match r { err(e) => err(e, CRealStep), ok(v) => ok(CRealStep(false, field(v, value), field(v, budget)), Text) }
    private fn cr_retry(b: ExactBudget) -> Result<CRealStep, Text> = ok(CRealStep(true, IntervalQ(cr_zero(), cr_zero()), b), Text)
    // A divisor that is exactly [0,0] is a proved division by zero; one that merely contains zero is undecided.
    private fn cr_divide(b: ExactBudget, op: Nat, x: IntervalQ, y: IntervalQ) -> Result<CRealStep, Text> =
        let d = if_then_else(nat_eq(op, 5), x, y) in
        if_then_else(and(cr_is_zero(field(d, lo)), cr_is_zero(field(d, hi))), err("computable real division by zero", CRealStep),
        match ivq_contains_zero(b, d) { err(e) => err(e, CRealStep), ok(z) =>
            if_then_else(field(z, value), cr_retry(field(z, budget)),
                cr_step(if_then_else(nat_eq(op, 5), ivq_inv(field(z, budget), x), ivq_div(field(z, budget), x, y)))) })
    // An interval entirely below zero proves a negative radicand; one straddling zero is undecided.
    private fn cr_sqrt(b: ExactBudget, x: IntervalQ, k: Nat) -> Result<CRealStep, Text> =
        if_then_else(field(field(x, hi), negative), err("computable real square root of a negative value", CRealStep),
            if_then_else(field(field(x, lo), negative), cr_retry(b), cr_step(ivq_sqrt(b, x, k))))
    // ---- v0.104 (N10): elementary functions on intervals rounded outward to dyadic 2^-p (docs/ELEMENTARY_FUNCTIONS.md) ----
    private record ElSeries(total: IntervalQ, last: IntervalQ, budget: ExactBudget)
    private fn el_wbits(n: Nat, w: Nat) -> Nat decreasing n fuel 20 = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(w, 0), 0, 1 + el_wbits(pred(n), div(w, 2))))
    // bit length of a little-endian base-65536 magnitude
    private fn el_bits(xs: List<Nat>) -> Nat = if_then_else(is_empty(xs), 0, mul(pred(length(xs)), 16) + el_wbits(17, unwrap_or(index(xs, pred(length(xs))), 0)))
    private fn el_nbits(n: Nat) -> Nat = el_wbits(20, n)
    private fn el_one() -> BigRational = BigRational(false, list(1), list(1))
    private fn el_rat(n: Nat, d: Nat) -> BigRational = BigRational(false, list(n), list(d))
    private fn el_iv(r: Result<IntervalQCalc, Text>) -> IntervalQ = match r { err(e) => IntervalQ(cr_zero(), cr_zero()), ok(v) => field(v, value) }
    private fn el_xor(a: Bool, c: Bool) -> Bool = if_then_else(a, not(c), c)
    private fn el_int(b: ExactBudget, v: Int) -> Result<BigRationalCalc, Text> =
        match big_from_int(b, v) { err(e) => err(e, BigRationalCalc), ok(n) => bigrat_from_ints(field(n, budget), field(n, value), BigInt(false, list(1))) }
    // 2^p as words: p / 16 zero words, then 2^(p mod 16)
    private fn el_zeros(n: Nat, acc: List<Nat>) -> List<Nat> decreasing n fuel 2100 = if_then_else(nat_eq(n, 0), acc, el_zeros(pred(n), cons(0, acc)))
    private fn el_pow2w(r: Nat) -> Nat decreasing r fuel 20 = if_then_else(nat_eq(r, 0), 1, mul(2, el_pow2w(pred(r))))
    private fn el_two(p: Nat) -> BigInt = BigInt(false, el_zeros(div(p, 16), list(el_pow2w(mod(p, 16)))))
    private fn el_pow2_word(n: Nat, w: Nat) -> Bool decreasing n fuel 20 =
        if_then_else(nat_eq(n, 0), false, if_then_else(nat_eq(w, 1), true, if_then_else(nat_eq(mod(w, 2), 1), false, el_pow2_word(pred(n), div(w, 2)))))
    private fn el_pow2_words(xs: List<Nat>) -> Bool decreasing xs fuel 4096 =
        if_then_else(is_empty(xs), false, if_then_else(is_empty(tail(xs)), el_pow2_word(17, unwrap_or(head(xs), 0)), and(nat_eq(unwrap_or(head(xs), 1), 0), el_pow2_words(tail(xs)))))
    // a denominator 2^e with e <= p: x is already exact at resolution p
    private fn el_dyadic_at(x: BigRational, p: Nat) -> Bool = and(not(el_bits(field(x, denominator)) > p + 1), el_pow2_words(field(x, denominator)))
    // floor(x 2^p)/2^p, or the ceiling when up
    private fn el_rnd(b: ExactBudget, x: BigRational, p: Nat, up: Bool) -> Result<BigRationalCalc, Text> =
        if_then_else(el_dyadic_at(x, p), ok(BigRationalCalc(x, b), Text),
        match ok(BigIntCalc(el_two(p), b), Text) { err(e) => err(e, BigRationalCalc), ok(s) =>
        match big_mul(field(s, budget), BigInt(false, field(x, numerator)), field(s, value)) { err(e) => err(e, BigRationalCalc), ok(num) =>
        match big_div_rem(field(num, budget), field(num, value), BigInt(false, field(x, denominator))) { err(e) => err(e, BigRationalCalc), ok(d) =>
        match if_then_else(and(not(is_empty(field(field(d, remainder), words))), el_xor(up, field(x, negative))), big_add(field(d, budget), field(d, quotient), BigInt(false, list(1))), ok(BigIntCalc(field(d, quotient), field(d, budget)), Text)) {
            err(e) => err(e, BigRationalCalc), ok(m) =>
                bigrat_from_ints(field(m, budget), BigInt(and(field(x, negative), not(is_empty(field(field(m, value), words)))), field(field(m, value), words)), field(s, value)) } } } })
    private fn el_out(r: Result<IntervalQCalc, Text>, p: Nat) -> Result<IntervalQCalc, Text> =
        match r { err(e) => err(e, IntervalQCalc), ok(x) =>
        match el_rnd(field(x, budget), field(field(x, value), lo), p, false) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match el_rnd(field(l, budget), field(field(x, value), hi), p, true) { err(e) => err(e, IntervalQCalc), ok(h) => ivq_new(field(h, budget), field(l, value), field(h, value)) } } }
    private fn el_scale(r: Result<IntervalQCalc, Text>, q: BigRational) -> Result<IntervalQCalc, Text> =
        match r { err(e) => err(e, IntervalQCalc), ok(x) => ivq_mul(field(x, budget), field(x, value), IntervalQ(q, q)) }
    private fn el_mul(r: Result<IntervalQCalc, Text>, y: IntervalQ) -> Result<IntervalQCalc, Text> = match r { err(e) => err(e, IntervalQCalc), ok(x) => ivq_mul(field(x, budget), field(x, value), y) }
    private fn el_add(r: Result<IntervalQCalc, Text>, y: IntervalQ) -> Result<IntervalQCalc, Text> = match r { err(e) => err(e, IntervalQCalc), ok(x) => ivq_add(field(x, budget), field(x, value), y) }
    private fn el_sub(r: Result<IntervalQCalc, Text>, y: IntervalQ) -> Result<IntervalQCalc, Text> = match r { err(e) => err(e, IntervalQCalc), ok(x) => ivq_sub(field(x, budget), field(x, value), y) }
    private fn el_eps(b: ExactBudget, p: Nat) -> Result<BigRationalCalc, Text> =
        match big_pow(b, BigInt(false, list(2)), p) { err(e) => err(e, BigRationalCalc), ok(s) => bigrat_from_ints(field(s, budget), BigInt(false, list(1)), field(s, value)) }
    private fn el_le(b: ExactBudget, x: BigRational, y: BigRational) -> Result<IntervalQFlag, Text> =
        match bigrat_cmp(b, x, y) { err(e) => err(e, IntervalQFlag), ok(o) => ok(IntervalQFlag(not(int_eq(field(o, order), int(1))), field(o, budget)), Text) }
    // [lo - l, hi + h]
    private fn el_widen(b: ExactBudget, x: IntervalQ, l: BigRational, h: BigRational) -> Result<IntervalQCalc, Text> =
        match bigrat_sub(b, field(x, lo), l) { err(e) => err(e, IntervalQCalc), ok(a) => match bigrat_add(field(a, budget), field(x, hi), h) { err(e) => err(e, IntervalQCalc), ok(c) => ivq_new(field(c, budget), field(a, value), field(c, value)) } }
    private fn el_abs(q: BigRational) -> BigRational = BigRational(false, field(q, numerator), field(q, denominator))
    private fn el_mag(b: ExactBudget, x: IntervalQ) -> Result<BigRationalCalc, Text> =
        match el_le(b, el_abs(field(x, lo)), el_abs(field(x, hi))) { err(e) => err(e, BigRationalCalc), ok(f) => ok(BigRationalCalc(if_then_else(field(f, value), el_abs(field(x, hi)), el_abs(field(x, lo))), field(f, budget)), Text) }

    // exp series for 0 <= y <= 1/2: t_i = t_{i-1} y / i; the tail after the last term is below it
    private fn el_exp_series(n: Nat, b: ExactBudget, t: IntervalQ, total: IntervalQ, y: IntervalQ, i: Nat, wp: Nat, eps: BigRational) -> Result<ElSeries, Text> decreasing n fuel 40000 =
        if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: elementary series steps exhausted", ElSeries),
        match el_out(el_scale(ivq_mul(b, t, y), el_rat(1, i + 1)), wp) { err(e) => err(e, ElSeries), ok(u) =>
        match ivq_add(field(u, budget), total, field(u, value)) { err(e) => err(e, ElSeries), ok(s) =>
        match el_le(field(s, budget), field(field(u, value), hi), eps) { err(e) => err(e, ElSeries), ok(f) =>
            if_then_else(field(f, value), ok(ElSeries(field(s, value), field(u, value), field(f, budget)), Text),
                el_exp_series(pred(n), field(f, budget), field(u, value), field(s, value), y, i + 1, wp, eps)) } } })
    private fn el_squarings(s: Nat, r: Result<IntervalQCalc, Text>, wp: Nat) -> Result<IntervalQCalc, Text> decreasing s fuel 20000 =
        if_then_else(nat_eq(s, 0), r, match r { err(e) => err(e, IntervalQCalc), ok(x) => el_squarings(pred(s), el_out(ivq_mul(field(x, budget), field(x, value), field(x, value)), wp), wp) })
    private fn el_exp_pos(b: ExactBudget, x: BigRational, w: Nat) -> Result<IntervalQCalc, Text> =
        let s = monus(el_bits(field(x, numerator)) + 2, el_bits(field(x, denominator))) in
        let wp = w + s + 8 in
        match big_pow(b, BigInt(false, list(2)), s) { err(e) => err(e, IntervalQCalc), ok(sc) =>
        match big_mul(field(sc, budget), BigInt(false, field(x, denominator)), field(sc, value)) { err(e) => err(e, IntervalQCalc), ok(den) =>
        match bigrat_from_ints(field(den, budget), BigInt(false, field(x, numerator)), field(den, value)) { err(e) => err(e, IntervalQCalc), ok(y0) =>
        match el_out(ivq_point(field(y0, budget), field(y0, value)), wp) { err(e) => err(e, IntervalQCalc), ok(y) =>
        match el_eps(field(y, budget), wp) { err(e) => err(e, IntervalQCalc), ok(eps) =>
        match el_exp_series(wp + 8, field(eps, budget), IntervalQ(el_one(), el_one()), IntervalQ(el_one(), el_one()), field(y, value), 0, wp, field(eps, value)) { err(e) => err(e, IntervalQCalc), ok(ser) =>
        match bigrat_add(field(ser, budget), field(field(ser, total), hi), field(field(ser, last), hi)) { err(e) => err(e, IntervalQCalc), ok(h) =>
            el_out(el_squarings(s, ivq_new(field(h, budget), field(field(ser, total), lo), field(h, value)), wp), w) } } } } } } }
    // bounds of exp(x) for a rational point; exp(-x) = 1/exp(x)
    private fn el_exp_point(b: ExactBudget, x: BigRational, w: Nat) -> Result<IntervalQCalc, Text> =
        if_then_else(cr_is_zero(x), ivq_point(b, el_one()),
        if_then_else(field(x, negative), match el_exp_pos(b, el_abs(x), w) { err(e) => err(e, IntervalQCalc), ok(e) => el_out(ivq_inv(field(e, budget), field(e, value)), w) },
            el_exp_pos(b, x, w)))
    private fn el_exp(b: ExactBudget, x: IntervalQ, w: Nat) -> Result<IntervalQCalc, Text> =
        match el_exp_point(b, field(x, lo), w) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match el_exp_point(field(l, budget), field(x, hi), w) { err(e) => err(e, IntervalQCalc), ok(h) => ivq_new(field(h, budget), field(field(l, value), lo), field(field(h, value), hi)) } }

    // sum of sign * out(power / (2k+1)); stops when a term is <= eps; alternate: signs (-1)^k
    private fn el_odd_series(n: Nat, b: ExactBudget, power: IntervalQ, zz: IntervalQ, total: IntervalQ, k: Nat, wp: Nat, eps: BigRational, alternate: Bool) -> Result<ElSeries, Text> decreasing n fuel 40000 =
        if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: elementary series steps exhausted", ElSeries),
        match el_out(ivq_mul(b, power, IntervalQ(el_rat(1, mul(2, k) + 1), el_rat(1, mul(2, k) + 1))), wp) { err(e) => err(e, ElSeries), ok(term) =>
        match if_then_else(and(alternate, nat_eq(mod(k, 2), 1)), ivq_sub(field(term, budget), total, field(term, value)), ivq_add(field(term, budget), total, field(term, value))) { err(e) => err(e, ElSeries), ok(s) =>
        match el_le(field(s, budget), field(field(term, value), hi), eps) { err(e) => err(e, ElSeries), ok(f) =>
            if_then_else(field(f, value), ok(ElSeries(field(s, value), field(term, value), field(f, budget)), Text),
            match el_out(ivq_mul(field(f, budget), power, zz), wp) { err(e) => err(e, ElSeries), ok(p) =>
                el_odd_series(pred(n), field(p, budget), field(p, value), zz, field(s, value), k + 1, wp, eps, alternate) }) } } })
    // atanh(z) for 0 <= z <= 1/3 (z rounded outward)
    private fn el_atanh(b: ExactBudget, z: BigRational, wp: Nat) -> Result<IntervalQCalc, Text> =
        if_then_else(cr_is_zero(z), ivq_point(b, cr_zero()),
        match el_out(ivq_point(b, z), wp) { err(e) => err(e, IntervalQCalc), ok(p) =>
        match ivq_mul(field(p, budget), field(p, value), field(p, value)) { err(e) => err(e, IntervalQCalc), ok(zz) =>
        match el_eps(field(zz, budget), wp) { err(e) => err(e, IntervalQCalc), ok(eps) =>
        match el_odd_series(wp + 8, field(eps, budget), field(p, value), field(zz, value), IntervalQ(cr_zero(), cr_zero()), 0, wp, field(eps, value), false) { err(e) => err(e, IntervalQCalc), ok(ser) =>
            el_widen(field(ser, budget), field(ser, total), cr_zero(), field(field(ser, last), hi)) } } } })
    // y = x / 2^m with m = bits(num) - bits(den), then 1 <= y < 2
    private fn el_log_point(b: ExactBudget, x: BigRational, w: Nat) -> Result<IntervalQCalc, Text> =
        let bn = el_bits(field(x, numerator)) in let bd = el_bits(field(x, denominator)) in
        let up = bn > bd in let e = if_then_else(up, monus(bn, bd), monus(bd, bn)) in
        match big_pow(b, BigInt(false, list(2)), e) { err(err0) => err(err0, IntervalQCalc), ok(sc) =>
        match big_mul(field(sc, budget), BigInt(false, if_then_else(up, field(x, denominator), field(x, numerator))), field(sc, value)) { err(err0) => err(err0, IntervalQCalc), ok(t) =>
        match if_then_else(up, bigrat_from_ints(field(t, budget), BigInt(false, field(x, numerator)), field(t, value)), bigrat_from_ints(field(t, budget), field(t, value), BigInt(false, field(x, denominator)))) { err(err0) => err(err0, IntervalQCalc), ok(y0) =>
        match bigrat_cmp(field(y0, budget), field(y0, value), el_one()) { err(err0) => err(err0, IntervalQCalc), ok(o) =>
        let below = int_eq(field(o, order), -1) in
        let m = if_then_else(up, int(e), -int(e)) - if_then_else(below, int(1), int(0)) in
        match if_then_else(below, bigrat_mul(field(o, budget), field(y0, value), el_rat(2, 1)), ok(BigRationalCalc(field(y0, value), field(o, budget)), Text)) { err(err0) => err(err0, IntervalQCalc), ok(y) =>
        let wp = w + el_nbits(abs(m)) + 8 in
        match bigrat_sub(field(y, budget), field(y, value), el_one()) { err(err0) => err(err0, IntervalQCalc), ok(ym) =>
        match bigrat_add(field(ym, budget), field(y, value), el_one()) { err(err0) => err(err0, IntervalQCalc), ok(yp) =>
        match bigrat_div(field(yp, budget), field(ym, value), field(yp, value)) { err(err0) => err(err0, IntervalQCalc), ok(z) =>
        match el_atanh(field(z, budget), field(z, value), wp) { err(err0) => err(err0, IntervalQCalc), ok(a) =>
        match el_atanh(field(a, budget), el_rat(1, 3), wp) { err(err0) => err(err0, IntervalQCalc), ok(h) =>
        match el_int(field(h, budget), mul(int(2), m)) { err(err0) => err(err0, IntervalQCalc), ok(m2) =>
        match el_scale(ok(IntervalQCalc(field(h, value), field(m2, budget)), Text), field(m2, value)) { err(err0) => err(err0, IntervalQCalc), ok(hm) =>
            el_out(el_add(el_scale(ok(IntervalQCalc(field(a, value), field(hm, budget)), Text), el_rat(2, 1)), field(hm, value)), w) } } } } } } } } } } } }
    private fn el_log(b: ExactBudget, x: IntervalQ, w: Nat) -> Result<IntervalQCalc, Text> =
        match el_log_point(b, field(x, lo), w) { err(e) => err(e, IntervalQCalc), ok(l) =>
        match el_log_point(field(l, budget), field(x, hi), w) { err(e) => err(e, IntervalQCalc), ok(h) => ivq_new(field(h, budget), field(field(l, value), lo), field(field(h, value), hi)) } }
    // a log domain proved by the bounds is an error; an undecided sign is retried
    private fn cr_log(b: ExactBudget, x: IntervalQ, k: Nat) -> Result<CRealStep, Text> =
        if_then_else(or(field(field(x, hi), negative), cr_is_zero(field(x, hi))), err("computable real logarithm of a non-positive value", CRealStep),
            if_then_else(or(field(field(x, lo), negative), cr_is_zero(field(x, lo))), cr_retry(b), cr_step(el_log(b, x, k))))

    // atan(1/q): alternating series, remainder below the last term
    private fn el_atan_recip(b: ExactBudget, q: Nat, wp: Nat) -> Result<IntervalQCalc, Text> =
        let z = el_rat(1, q) in let zz = el_rat(1, mul(q, q)) in
        match el_eps(b, wp) { err(e) => err(e, IntervalQCalc), ok(eps) =>
        match el_odd_series(wp + 8, field(eps, budget), IntervalQ(z, z), IntervalQ(zz, zz), IntervalQ(cr_zero(), cr_zero()), 0, wp, field(eps, value), true) { err(e) => err(e, IntervalQCalc), ok(ser) =>
            el_widen(field(ser, budget), field(ser, total), field(field(ser, last), hi), field(field(ser, last), hi)) } }
    // pi = 16 atan(1/5) - 4 atan(1/239)
    private fn el_pi(b: ExactBudget, w: Nat) -> Result<IntervalQCalc, Text> =
        match el_atan_recip(b, 5, w + 8) { err(e) => err(e, IntervalQCalc), ok(a) =>
        match el_atan_recip(field(a, budget), 239, w + 8) { err(e) => err(e, IntervalQCalc), ok(c) =>
        match el_scale(ok(IntervalQCalc(field(c, value), field(c, budget)), Text), el_rat(4, 1)) { err(e) => err(e, IntervalQCalc), ok(c4) =>
            el_out(el_sub(el_scale(ok(IntervalQCalc(field(a, value), field(c4, budget)), Text), el_rat(16, 1)), field(c4, value)), w) } } }

    // alternating Taylor series of sin/cos; stops at k >= 2 with |t| <= eps
    private fn el_taylor_loop(n: Nat, b: ExactBudget, t: IntervalQ, total: IntervalQ, mm: IntervalQ, k: Nat, wp: Nat, eps: BigRational, cosine: Bool) -> Result<ElSeries, Text> decreasing n fuel 40000 =
        if_then_else(nat_eq(n, 0), err("UnknownWithinBudget: elementary series steps exhausted", ElSeries),
        let k1 = k + 1 in let d = if_then_else(cosine, mul(monus(mul(2, k1), 1), mul(2, k1)), mul(mul(2, k1), mul(2, k1) + 1)) in
        match el_out(el_scale(ivq_mul(b, t, mm), BigRational(true, list(1), list(d))), wp) { err(e) => err(e, ElSeries), ok(u) =>
        match ivq_add(field(u, budget), total, field(u, value)) { err(e) => err(e, ElSeries), ok(s) =>
        match el_mag(field(s, budget), field(u, value)) { err(e) => err(e, ElSeries), ok(g) =>
        match el_le(field(g, budget), field(g, value), eps) { err(e) => err(e, ElSeries), ok(f) =>
            if_then_else(and(k1 > 1, field(f, value)), ok(ElSeries(field(s, value), field(u, value), field(f, budget)), Text),
                el_taylor_loop(pred(n), field(f, budget), field(u, value), field(s, value), mm, k1, wp, eps, cosine)) } } } })
    private fn el_taylor(b: ExactBudget, m: BigRational, w: Nat, cosine: Bool) -> Result<IntervalQCalc, Text> =
        match el_out(ivq_point(b, m), w + 8) { err(e) => err(e, IntervalQCalc), ok(mi) =>
        match ivq_mul(field(mi, budget), field(mi, value), field(mi, value)) { err(e) => err(e, IntervalQCalc), ok(mm) =>
        match el_eps(field(mm, budget), w + 8) { err(e) => err(e, IntervalQCalc), ok(eps) =>
        let t = if_then_else(cosine, IntervalQ(el_one(), el_one()), field(mi, value)) in
        match el_taylor_loop(w + 16, field(eps, budget), t, t, field(mm, value), 0, w + 8, field(eps, value), cosine) { err(e) => err(e, IntervalQCalc), ok(ser) =>
        match el_mag(field(ser, budget), field(ser, last)) { err(e) => err(e, IntervalQCalc), ok(g) =>
            el_out(el_widen(field(g, budget), field(ser, total), field(g, value), field(g, value)), w) } } } } }
    // mean value form f(mid) +- r, clipped to [-1, 1]: mid = x's midpoint rounded down at w + 8, r = max(hi - mid, mid - lo)
    private record ElMid(mid: BigRational, r: BigRational, budget: ExactBudget)
    private fn el_mid(b: ExactBudget, x: IntervalQ, w: Nat) -> Result<ElMid, Text> =
        match ivq_midpoint(b, x) { err(e) => err(e, ElMid), ok(m0) =>
        match el_rnd(field(m0, budget), field(m0, value), w + 8, false) { err(e) => err(e, ElMid), ok(mid) =>
        match bigrat_sub(field(mid, budget), field(x, hi), field(mid, value)) { err(e) => err(e, ElMid), ok(u) =>
        match bigrat_sub(field(u, budget), field(mid, value), field(x, lo)) { err(e) => err(e, ElMid), ok(dn) =>
        match el_le(field(dn, budget), field(u, value), field(dn, value)) { err(e) => err(e, ElMid), ok(f) =>
            ok(ElMid(field(mid, value), if_then_else(field(f, value), field(dn, value), field(u, value)), field(f, budget)), Text) } } } } }
    private fn el_clip(r: Result<IntervalQCalc, Text>, rad: BigRational, w: Nat) -> Result<IntervalQCalc, Text> =
        match r { err(e) => err(e, IntervalQCalc), ok(s) =>
        match el_widen(field(s, budget), field(s, value), rad, rad) { err(e) => err(e, IntervalQCalc), ok(v) =>
        match el_le(field(v, budget), field(field(v, value), lo), BigRational(true, list(1), list(1))) { err(e) => err(e, IntervalQCalc), ok(cl) =>
        match el_le(field(cl, budget), el_one(), field(field(v, value), hi)) { err(e) => err(e, IntervalQCalc), ok(ch) =>
            el_out(ivq_new(field(ch, budget), if_then_else(field(cl, value), BigRational(true, list(1), list(1)), field(field(v, value), lo)), if_then_else(field(ch, value), el_one(), field(field(v, value), hi))), w) } } } }
    // |c| <= 4: Taylor; otherwise c - 2 pi j with j = floor(c / (2 pi_lo) + 1/2), then the mean value form
    private fn el_trig_point(b: ExactBudget, c: BigRational, w: Nat, cosine: Bool) -> Result<IntervalQCalc, Text> =
        match el_le(b, el_abs(c), el_rat(4, 1)) { err(e) => err(e, IntervalQCalc), ok(small) =>
        if_then_else(field(small, value), el_taylor(field(small, budget), c, w, cosine),
            let wr = w + monus(el_bits(field(c, numerator)) + 1, el_bits(field(c, denominator))) + 8 in
            match el_pi(field(small, budget), wr) { err(e) => err(e, IntervalQCalc), ok(p) =>
            match bigrat_mul(field(p, budget), field(field(p, value), lo), el_rat(2, 1)) { err(e) => err(e, IntervalQCalc), ok(tp) =>
            match bigrat_div(field(tp, budget), c, field(tp, value)) { err(e) => err(e, IntervalQCalc), ok(q0) =>
            match bigrat_add(field(q0, budget), field(q0, value), el_rat(1, 2)) { err(e) => err(e, IntervalQCalc), ok(q) =>
            match big_div_rem(field(q, budget), BigInt(field(field(q, value), negative), field(field(q, value), numerator)), BigInt(false, field(field(q, value), denominator))) { err(e) => err(e, IntervalQCalc), ok(j) =>
            match big_mul(field(j, budget), field(j, quotient), BigInt(false, list(2))) { err(e) => err(e, IntervalQCalc), ok(j2) =>
            match bigrat_from_ints(field(j2, budget), field(j2, value), BigInt(false, list(1))) { err(e) => err(e, IntervalQCalc), ok(jq) =>
            match el_out(el_sub(ivq_point(field(jq, budget), c), el_iv(el_scale(ok(IntervalQCalc(field(p, value), field(jq, budget)), Text), field(jq, value)))), wr) { err(e) => err(e, IntervalQCalc), ok(red) =>
                if_then_else(and(cr_is_zero(field(field(red, value), lo)), cr_is_zero(field(field(red, value), hi))), el_taylor(field(red, budget), field(field(red, value), lo), w, cosine),
                match bigrat_cmp(field(red, budget), field(field(red, value), lo), field(field(red, value), hi)) { err(e) => err(e, IntervalQCalc), ok(o) =>
                    if_then_else(int_eq(field(o, order), int(0)), el_taylor(field(o, budget), field(field(red, value), lo), w, cosine),
                    match el_mid(field(o, budget), field(red, value), w) { err(e) => err(e, IntervalQCalc), ok(m) =>
                        el_clip(el_taylor(field(m, budget), field(m, mid), w, cosine), field(m, r), w) }) }) } } } } } } } }) }
    private fn el_trig(b: ExactBudget, x: IntervalQ, w: Nat, cosine: Bool) -> Result<IntervalQCalc, Text> =
        match bigrat_cmp(b, field(x, lo), field(x, hi)) { err(e) => err(e, IntervalQCalc), ok(o) =>
            if_then_else(int_eq(field(o, order), int(0)), el_trig_point(field(o, budget), field(x, lo), w, cosine),
            match el_mid(field(o, budget), x, w) { err(e) => err(e, IntervalQCalc), ok(m) =>
                el_clip(el_trig_point(field(m, budget), field(m, mid), w, cosine), field(m, r), w) }) }
    private fn cr_node(b: ExactBudget, node: CRealNode, vals: List<IntervalQ>, count: Nat, k: Nat) -> Result<CRealStep, Text> =
        let op = field(node, op) in
        let x = cr_at(vals, count, field(node, a)) in
        let y = cr_at(vals, count, field(node, b)) in
        if_then_else(nat_eq(op, 0), cr_step(ivq_point(b, field(node, value))),
        if_then_else(nat_eq(op, 1), cr_step(ivq_add(b, x, y)),
        if_then_else(nat_eq(op, 2), cr_step(ivq_sub(b, x, y)),
        if_then_else(nat_eq(op, 3), cr_step(ivq_mul(b, x, y)),
        if_then_else(nat_eq(op, 4), cr_step(ivq_neg(b, x)),
        if_then_else(nat_eq(op, 7), cr_sqrt(b, x, k),
        if_then_else(nat_eq(op, 8), cr_step(ivq_square(b, x)),
        if_then_else(nat_eq(op, 9), cr_step(ivq_abs(b, x)),
        if_then_else(nat_eq(op, 10), cr_step(el_exp(b, x, k)),
        if_then_else(nat_eq(op, 11), cr_log(b, x, k),
        if_then_else(nat_eq(op, 12), cr_step(el_trig(b, x, k, false)),
        if_then_else(nat_eq(op, 13), cr_step(el_trig(b, x, k, true)),
        if_then_else(nat_eq(op, 14), cr_step(el_pi(b, k)),
            cr_divide(b, op, x, y))))))))))))))
    // vals holds the node intervals newest first: node j is at count - 1 - j.
    private fn cr_eval(n: Nat, b: ExactBudget, nodes: List<CRealNode>, k: Nat, vals: List<IntervalQ>, count: Nat) -> Result<CRealStep, Text> decreasing n fuel 128 =
        match head(nodes) { none => ok(CRealStep(false, cr_at(vals, count, pred(count)), b), Text), some(node) =>
            if_then_else(nat_eq(n, 0), err("computable real program must have 1..64 nodes", CRealStep),
            match cr_node(b, node, vals, count, k) { err(e) => err(e, CRealStep), ok(s) =>
                if_then_else(field(s, retry), ok(s, Text), cr_eval(pred(n), field(s, budget), tail(nodes), k, cons(field(s, value), vals), count + 1)) }) }
    private fn cr_rounds(r: Nat, b: ExactBudget, nodes: List<CRealNode>, k: Nat, tolerance: BigRational) -> Result<IntervalQCalc, Text> decreasing r fuel 8 =
        if_then_else(nat_eq(r, 0), err("UnknownWithinBudget: computable real refinement limit reached", IntervalQCalc),
        if_then_else(k > 16384, err("UnknownWithinBudget: computable real refinement limit reached", IntervalQCalc),
        match cr_eval(64, b, nodes, k, empty_list(IntervalQ), 0) { err(e) => err(e, IntervalQCalc), ok(s) =>
            if_then_else(field(s, retry), cr_rounds(pred(r), field(s, budget), nodes, mul(2, k), tolerance),
            match ivq_width(field(s, budget), field(s, value)) { err(e) => err(e, IntervalQCalc), ok(w) =>
            match bigrat_cmp(field(w, budget), field(w, value), tolerance) { err(e) => err(e, IntervalQCalc), ok(o) =>
                if_then_else(int_eq(field(o, order), int(1)), cr_rounds(pred(r), field(o, budget), nodes, mul(2, k), tolerance),
                    ok(IntervalQCalc(field(s, value), field(o, budget)), Text)) } }) }))
    // An interval containing the real with width <= 2^-n (n <= 4096).
    fn creal_approx(b: ExactBudget, x: CReal, n: Nat) -> Result<IntervalQCalc, Text> =
        if_then_else(n > 4096, err("computable real precision exceeds 4096 bits", IntervalQCalc),
        match creal_check(b, x) { err(e) => err(e, IntervalQCalc), ok(c) =>
        match big_pow(field(c, budget), BigInt(false, list(2)), n) { err(e) => err(e, IntervalQCalc), ok(scale) =>
        match bigrat_from_ints(field(scale, budget), BigInt(false, list(1)), field(scale, value)) { err(e) => err(e, IntervalQCalc), ok(t) =>
            cr_rounds(6, field(t, budget), field(x, nodes), n + 8, field(t, value)) } } })
    // Order decided by the bounds at precision n: -1, 1, 0 (the same exact point) or 2 (undecided).
    fn creal_compare(b: ExactBudget, x: CReal, y: CReal, n: Nat) -> Result<IntervalQOrder, Text> =
        match creal_approx(b, x, n) { err(e) => err(e, IntervalQOrder), ok(p) =>
        match creal_approx(field(p, budget), y, n) { err(e) => err(e, IntervalQOrder), ok(q) => ivq_compare(field(q, budget), field(p, value), field(q, value)) } }

    private fn cr_node_text(b: ExactBudget, node: CRealNode) -> Result<BigTextCalc, Text> =
        let arity = cr_arity(field(node, op)) in
        if_then_else(nat_eq(field(node, op), 14), ok(BigTextCalc("pi", b), Text),
        if_then_else(nat_eq(arity, 0), match bigrat_text(b, field(node, value)) { err(e) => err(e, BigTextCalc), ok(t) => ok(BigTextCalc(concat("c ", field(t, value)), field(t, budget)), Text) },
            ok(BigTextCalc(concat(cr_name(field(node, op)), concat(" ", concat(nat_to_text(field(node, a)), if_then_else(nat_eq(arity, 2), concat(" ", nat_to_text(field(node, b))), "")))), b), Text)))
    private fn cr_text_loop(n: Nat, b: ExactBudget, nodes: List<CRealNode>, acc: Text, first: Bool) -> Result<BigTextCalc, Text> decreasing n fuel 128 =
        match head(nodes) { none => ok(BigTextCalc(acc, b), Text), some(node) =>
            if_then_else(nat_eq(n, 0), err("computable real program must have 1..64 nodes", BigTextCalc),
            match cr_node_text(b, node) { err(e) => err(e, BigTextCalc), ok(t) =>
                cr_text_loop(pred(n), field(t, budget), tail(nodes), if_then_else(first, field(t, value), concat(acc, concat(" ; ", field(t, value)))), false) }) }
    // Canonical text: `c <q>` or `<op> <a> [<b>]` nodes separated by ` ; `.
    fn creal_text(b: ExactBudget, x: CReal) -> Result<BigTextCalc, Text> =
        match creal_check(b, x) { err(e) => err(e, BigTextCalc), ok(c) => cr_text_loop(64, field(c, budget), field(x, nodes), "", true) }

    private fn cr_index(t: Text) -> Option<Nat> = match text_to_nat(t) { none => none(Nat), some(i) => if_then_else(text_eq(nat_to_text(i), t), some(i), none(Nat)) }
    private fn cr_token(tokens: List<Text>, i: Nat) -> Text = unwrap_or(index(tokens, i), "")
    private fn cr_reverse(xs: List<CRealNode>, acc: List<CRealNode>) -> List<CRealNode> decreasing xs fuel 128 =
        if_then_else(is_empty(xs), acc, cr_reverse(tail(xs), cons(unwrap_or(head(xs), CRealNode(0, 0, 0, cr_zero())), acc)))
    private fn cr_parse_node(b: ExactBudget, op: Nat, first: Text, second: Text) -> Result<CRealNodeCalc, Text> =
        if_then_else(nat_eq(op, 14), ok(CRealNodeCalc(CRealNode(14, 0, 0, cr_zero()), b), Text),
        if_then_else(nat_eq(op, 0), match bigrat_parse(b, first) { err(e) => err(e, CRealNodeCalc), ok(q) => ok(CRealNodeCalc(CRealNode(0, 0, 0, field(q, value)), field(q, budget)), Text) },
        match cr_index(first) { none => err("invalid computable real text", CRealNodeCalc), some(a) =>
            if_then_else(nat_eq(cr_arity(op), 1), ok(CRealNodeCalc(CRealNode(op, a, 0, cr_zero()), b), Text),
            match cr_index(second) { none => err("invalid computable real text", CRealNodeCalc), some(c) => ok(CRealNodeCalc(CRealNode(op, a, c, cr_zero()), b), Text) }) }))
    private fn cr_parse_nodes(n: Nat, b: ExactBudget, tokens: List<Text>, at: Nat, acc: List<CRealNode>) -> Result<CRealCalc, Text> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), err("computable real program must have 1..64 nodes", CRealCalc),
        match cr_code(15, cr_token(tokens, at), 0) { none => err("invalid computable real text", CRealCalc), some(op) =>
            let width = if_then_else(nat_eq(op, 0), 1, cr_arity(op)) in
            if_then_else(not(length(tokens) > at + width), err("invalid computable real text", CRealCalc),
            match cr_parse_node(b, op, cr_token(tokens, at + 1), cr_token(tokens, at + 2)) { err(e) => err(e, CRealCalc), ok(node) =>
                let next = at + 1 + width in
                if_then_else(nat_eq(length(tokens), next), creal_check(field(node, budget), CReal(cr_reverse(cons(field(node, value), acc), empty_list(CRealNode)))),
                if_then_else(and(text_eq(cr_token(tokens, next), ";"), length(tokens) > next + 1),
                    cr_parse_nodes(pred(n), field(node, budget), tokens, next + 1, cons(field(node, value), acc)),
                    err("invalid computable real text", CRealCalc))) }) })
    // Strict text: single spaces between tokens, nodes separated by the token `;`, canonical indices.
    fn creal_parse(b: ExactBudget, text: Text) -> Result<CRealCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, CRealCalc), ok(valid) =>
            if_then_else(text_length(text) > mul(64, mul(2, field(b, bits)) + 16), err("UnknownWithinBudget: computable real input exceeded", CRealCalc),
                let tokens = words(text) in
                if_then_else(not(text_eq(join(tokens, " "), text)), err("invalid computable real text", CRealCalc),
                    cr_parse_nodes(64, valid, tokens, 0, empty_list(CRealNode)))) }

    // Closed legacy finite computations, not general convergence proofs.
    theorem exact_zero_divisor_is_refused: is_err(creal_approx(exact_default_budget(), CReal(list(CRealNode(0, 0, 0, cr_zero()), CRealNode(5, 0, 0, cr_zero()))), 4))
    theorem forward_operand_is_refused: is_err(creal_check(exact_default_budget(), CReal(list(CRealNode(4, 1, 0, cr_zero()), CRealNode(0, 0, 0, cr_zero())))))
}
