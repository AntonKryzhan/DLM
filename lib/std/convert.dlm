// Explicit conversions between numeric families (N11) and the portable numeric text (N12), the host's
// math::convert / math::portable. Rounding modes: 0 floor, 1 ceil, 2 trunc (toward zero), 3 half_even.
// Computable reals are converted only when both bounds of an enclosure round alike (precisions 0, 8, 16, ...
// below max_n, then max_n); otherwise UnknownWithinBudget. See docs/NUMERIC_CONVERSIONS.md.
module std.convert
import std.ccomplex

theory StdConvert extends StdCComplex {
    private fn cv_big(r: Result<BigIntCalc, Text>) -> BigInt = match r { err(e) => BigInt(false, empty_list(Nat)), ok(v) => field(v, value) }
    private fn cv_odd(x: BigInt) -> Bool = nat_eq(mod(unwrap_or(head(field(x, words)), 0), 2), 1)
    // away from zero for the magnitude |q| = f + r/d
    private fn cv_away(b: ExactBudget, q: BigRational, f: BigInt, r: BigInt, mode: Nat) -> Result<IntervalQFlag, Text> =
        if_then_else(is_empty(field(r, words)), ok(IntervalQFlag(false, b), Text),
        if_then_else(nat_eq(mode, 0), ok(IntervalQFlag(field(q, negative), b), Text),
        if_then_else(nat_eq(mode, 1), ok(IntervalQFlag(not(field(q, negative)), b), Text),
        if_then_else(nat_eq(mode, 2), ok(IntervalQFlag(false, b), Text),
        match big_mul(b, r, BigInt(false, list(2))) { err(e) => err(e, IntervalQFlag), ok(t) =>
        match big_cmp(field(t, budget), field(t, value), BigInt(false, field(q, denominator))) { err(e) => err(e, IntervalQFlag), ok(o) =>
            ok(IntervalQFlag(if_then_else(int_eq(field(o, order), int(0)), cv_odd(f), int_eq(field(o, order), int(1))), field(o, budget)), Text) } }))))
    // round(q) to an integer by mode
    fn cv_round(b: ExactBudget, q: BigRational, mode: Nat) -> Result<BigIntCalc, Text> =
        if_then_else(mode > 3, err("unknown rounding mode", BigIntCalc),
        match bigrat_check(b, q) { err(e) => err(e, BigIntCalc), ok(c) =>
        match big_div_rem(field(c, budget), BigInt(false, field(q, numerator)), BigInt(false, field(q, denominator))) { err(e) => err(e, BigIntCalc), ok(d) =>
        match cv_away(field(d, budget), q, field(d, quotient), field(d, remainder), mode) { err(e) => err(e, BigIntCalc), ok(a) =>
        match if_then_else(field(a, value), big_add(field(a, budget), field(d, quotient), BigInt(false, list(1))), ok(BigIntCalc(field(d, quotient), field(a, budget)), Text)) { err(e) => err(e, BigIntCalc), ok(m) =>
            big_from_words(field(m, budget), field(q, negative), field(field(m, value), words)) } } } })
    fn cv_integer_exact(b: ExactBudget, q: BigRational) -> Result<BigIntCalc, Text> =
        match bigrat_check(b, q) { err(e) => err(e, BigIntCalc), ok(c) =>
            if_then_else(and(nat_eq(length(field(q, denominator)), 1), nat_eq(unwrap_or(head(field(q, denominator)), 0), 1)), cv_round(field(c, budget), q, 2), err("rational is not an integer", BigIntCalc)) }
    // round(q s) for a positive integer scale s, and the scaled value as a rational over s
    private fn cv_scaled(b: ExactBudget, q: BigRational, s: BigInt, mode: Nat) -> Result<BigIntCalc, Text> =
        match big_mul(b, BigInt(false, field(q, numerator)), s) { err(e) => err(e, BigIntCalc), ok(n) =>
        match bigrat_from_ints(field(n, budget), BigInt(field(q, negative), field(field(n, value), words)), BigInt(false, field(q, denominator))) { err(e) => err(e, BigIntCalc), ok(v) =>
            cv_round(field(v, budget), field(v, value), mode) } }
    // round(q 2^p)/2^p
    fn cv_dyadic(b: ExactBudget, q: BigRational, p: Nat, mode: Nat) -> Result<BigRationalCalc, Text> =
        match bigrat_check(b, q) { err(e) => err(e, BigRationalCalc), ok(c) =>
        match big_pow(field(c, budget), BigInt(false, list(2)), p) { err(e) => err(e, BigRationalCalc), ok(s) =>
        match cv_scaled(field(s, budget), q, field(s, value), mode) { err(e) => err(e, BigRationalCalc), ok(i) => bigrat_from_ints(field(i, budget), field(i, value), field(s, value)) } } }
    private fn cv_zeros(n: Nat, acc: Text) -> Text decreasing n fuel 4200 = if_then_else(nat_eq(n, 0), acc, cv_zeros(pred(n), concat("0", acc)))
    // sign, integer digits, "." and exactly d fractional digits of i / 10^d
    private fn cv_fixed(b: ExactBudget, i: BigInt, d: Nat) -> Result<BigTextCalc, Text> =
        match big_text(b, BigInt(false, field(i, words))) { err(e) => err(e, BigTextCalc), ok(t) =>
            let sign = if_then_else(field(i, negative), "-", "") in
            let m = if_then_else(text_length(field(t, value)) > d, field(t, value), cv_zeros(monus(d + 1, text_length(field(t, value))), field(t, value))) in
            ok(BigTextCalc(if_then_else(nat_eq(d, 0), concat(sign, m),
                concat(sign, concat(substring(m, 0, monus(text_length(m), d)), concat(".", substring(m, monus(text_length(m), d), d))))), field(t, budget)), Text) }
    // q rounded by mode to exactly d fractional decimal digits
    fn cv_decimal(b: ExactBudget, q: BigRational, d: Nat, mode: Nat) -> Result<BigTextCalc, Text> =
        if_then_else(d > 4096, err("decimal digit count exceeds 4096", BigTextCalc),
        match bigrat_check(b, q) { err(e) => err(e, BigTextCalc), ok(c) =>
        match big_pow(field(c, budget), BigInt(false, list(10)), d) { err(e) => err(e, BigTextCalc), ok(s) =>
        match cv_scaled(field(s, budget), q, field(s, value), mode) { err(e) => err(e, BigTextCalc), ok(i) => cv_fixed(field(i, budget), field(i, value), d) } } })
    private record CvFactors(rest: BigInt, count: Nat, budget: ExactBudget)
    private fn cv_strip(n: Nat, b: ExactBudget, x: BigInt, p: Nat, count: Nat) -> Result<CvFactors, Text> decreasing n fuel 16500 =
        if_then_else(nat_eq(n, 0), ok(CvFactors(x, count, b), Text),
        match big_div_rem(b, x, BigInt(false, list(p))) { err(e) => err(e, CvFactors), ok(d) =>
            if_then_else(is_empty(field(field(d, remainder), words)), cv_strip(pred(n), field(d, budget), field(d, quotient), p, count + 1), ok(CvFactors(x, count, field(d, budget)), Text)) })
    // the shortest exact decimal, or an error when q is not a finite decimal
    fn cv_decimal_exact(b: ExactBudget, q: BigRational) -> Result<BigTextCalc, Text> =
        match bigrat_check(b, q) { err(e) => err(e, BigTextCalc), ok(c) =>
        match cv_strip(16400, field(c, budget), BigInt(false, field(q, denominator)), 2, 0) { err(e) => err(e, BigTextCalc), ok(t) =>
        match cv_strip(16400, field(t, budget), field(t, rest), 5, 0) { err(e) => err(e, BigTextCalc), ok(f) =>
            if_then_else(not(and(nat_eq(length(field(field(f, rest), words)), 1), nat_eq(unwrap_or(head(field(field(f, rest), words)), 0), 1))), err("rational is not a finite decimal", BigTextCalc),
                cv_decimal(field(f, budget), q, if_then_else(field(t, count) > field(f, count), field(t, count), field(f, count)), 2)) } } }

    private fn cv_digits(n: Nat, s: Text, i: Nat) -> Bool decreasing n fuel 40000 =
        if_then_else(nat_eq(n, 0), true, and(is_some(text_to_nat(substring(s, i, 1))), cv_digits(pred(n), s, i + 1)))
    private fn cv_is_digits(s: Text) -> Bool = and(text_length(s) > 0, cv_digits(text_length(s), s, 0))
    private fn cv_find(n: Nat, s: Text, c: Text, i: Nat) -> Option<Nat> decreasing n fuel 40000 =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(text_eq(substring(s, i, 1), c), some(i), cv_find(pred(n), s, c, i + 1)))
    private fn cv_index(s: Text, c: Text) -> Option<Nat> = cv_find(text_length(s), s, c, 0)
    // exact value of [-]D+[.D+][e[-]D+] with |exponent| <= 4096
    fn cv_parse_decimal(b: ExactBudget, text: Text) -> Result<BigRationalCalc, Text> =
        if_then_else(text_length(text) > mul(2, field(b, bits)) + 16, err("UnknownWithinBudget: decimal input exceeded", BigRationalCalc),
            let neg = text_eq(substring(text, 0, 1), "-") in
            let body = if_then_else(neg, substring(text, 1, text_length(text)), text) in
            let epos = cv_index(body, "e") in
            let mant = match epos { none => body, some(i) => substring(body, 0, i) } in
            let ex = match epos { none => "", some(i) => substring(body, i + 1, text_length(body)) } in
            let eneg = text_eq(substring(ex, 0, 1), "-") in
            let edigits = if_then_else(eneg, substring(ex, 1, text_length(ex)), ex) in
            let dpos = cv_index(mant, ".") in
            let ip = match dpos { none => mant, some(i) => substring(mant, 0, i) } in
            let fp = match dpos { none => "", some(i) => substring(mant, i + 1, text_length(mant)) } in
            if_then_else(or(not(cv_is_digits(ip)), and(is_some(dpos), not(cv_is_digits(fp)))), err("invalid decimal number", BigRationalCalc),
            if_then_else(and(is_some(epos), or(not(cv_is_digits(edigits)), text_length(edigits) > 5)), err("invalid decimal number", BigRationalCalc),
                let e = unwrap_or(text_to_nat(edigits), 0) in
                if_then_else(e > 4096, err("decimal exponent out of range", BigRationalCalc),
                match bigrat_parse(b, concat(ip, fp)) { err(err0) => err(err0, BigRationalCalc), ok(n) =>
                    let up = and(not(eneg), not(text_length(fp) > e)) in
                    let shift = if_then_else(up, monus(e, text_length(fp)), if_then_else(eneg, e + text_length(fp), monus(text_length(fp), e))) in
                    match big_pow(field(n, budget), BigInt(false, list(10)), shift) { err(err0) => err(err0, BigRationalCalc), ok(p) =>
                    match if_then_else(up, big_mul(field(p, budget), BigInt(false, field(field(n, value), numerator)), field(p, value)), ok(BigIntCalc(BigInt(false, field(field(n, value), numerator)), field(p, budget)), Text)) { err(err0) => err(err0, BigRationalCalc), ok(m) =>
                        bigrat_from_ints(field(m, budget), BigInt(and(neg, not(is_empty(field(field(m, value), words)))), field(field(m, value), words)), if_then_else(up, BigInt(false, list(1)), field(p, value))) } } }))))

    private fn cv_next(n: Nat, max: Nat) -> Nat = if_then_else(nat_eq(n, 0), if_then_else(max > 8, 8, max), if_then_else(max > mul(2, n), mul(2, n), max))
    private fn cv_undecided(max: Nat) -> Text = concat("UnknownWithinBudget: rounding undecided at precision ", nat_to_text(max))
    private fn cv_round_at(steps: Nat, b: ExactBudget, x: CReal, mode: Nat, n: Nat, max: Nat) -> Result<BigIntCalc, Text> decreasing steps fuel 16 =
        if_then_else(nat_eq(steps, 0), err(cv_undecided(max), BigIntCalc),
        match creal_approx(b, x, n) { err(e) => err(e, BigIntCalc), ok(v) =>
        match cv_round(field(v, budget), field(field(v, value), lo), mode) { err(e) => err(e, BigIntCalc), ok(l) =>
        match cv_round(field(l, budget), field(field(v, value), hi), mode) { err(e) => err(e, BigIntCalc), ok(h) =>
        match big_cmp(field(h, budget), field(l, value), field(h, value)) { err(e) => err(e, BigIntCalc), ok(o) =>
            if_then_else(int_eq(field(o, order), int(0)), ok(BigIntCalc(field(l, value), field(o, budget)), Text),
                if_then_else(nat_eq(n, max), err(cv_undecided(max), BigIntCalc), cv_round_at(pred(steps), field(o, budget), x, mode, cv_next(n, max), max))) } } } })
    // round(x) decided by the enclosures of x up to precision max_n
    fn cv_creal_round(b: ExactBudget, x: CReal, mode: Nat, max_n: Nat) -> Result<BigIntCalc, Text> =
        if_then_else(max_n > 4096, err("computable real precision exceeds 4096 bits", BigIntCalc), cv_round_at(16, b, x, mode, 0, max_n))
    private fn cv_decimal_at(steps: Nat, b: ExactBudget, x: CReal, d: Nat, mode: Nat, n: Nat, max: Nat) -> Result<BigTextCalc, Text> decreasing steps fuel 16 =
        if_then_else(nat_eq(steps, 0), err(cv_undecided(max), BigTextCalc),
        match creal_approx(b, x, n) { err(e) => err(e, BigTextCalc), ok(v) =>
        match cv_decimal(field(v, budget), field(field(v, value), lo), d, mode) { err(e) => err(e, BigTextCalc), ok(l) =>
        match cv_decimal(field(l, budget), field(field(v, value), hi), d, mode) { err(e) => err(e, BigTextCalc), ok(h) =>
            if_then_else(text_eq(field(l, value), field(h, value)), ok(h, Text),
                if_then_else(nat_eq(n, max), err(cv_undecided(max), BigTextCalc), cv_decimal_at(pred(steps), field(h, budget), x, d, mode, cv_next(n, max), max))) } } })
    fn cv_creal_decimal(b: ExactBudget, x: CReal, d: Nat, mode: Nat, max_n: Nat) -> Result<BigTextCalc, Text> =
        if_then_else(max_n > 4096, err("computable real precision exceeds 4096 bits", BigTextCalc), cv_decimal_at(16, b, x, d, mode, 0, max_n))

    // machine integers (Nat: 0..2^128 - 1, Int: -2^127..2^127 - 1)
    private fn cv_value(xs: List<Nat>) -> Nat = fold(fold(xs, empty_list(Nat), fun(r: List<Nat>, w: Nat, cons(w, r))), 0, fun(acc: Nat, w: Nat, mul(acc, 65536) + w))
    fn cv_to_nat(b: ExactBudget, x: BigInt) -> Result<Nat, Text> =
        if_then_else(or(field(x, negative), length(field(x, words)) > 8), err("integer does not fit in Nat", Nat), ok(cv_value(field(x, words)), Text))
    private fn cv_top(xs: List<Nat>) -> Nat = fold(xs, 0, fun(acc: Nat, w: Nat, w))
    fn cv_to_int(b: ExactBudget, x: BigInt) -> Result<Int, Text> =
        let n = length(field(x, words)) in
        let top = cv_top(field(x, words)) in
        if_then_else(or(n > 8, and(nat_eq(n, 8), not(32768 > top))),
            if_then_else(and(field(x, negative), and(nat_eq(n, 8), and(nat_eq(top, 32768), nat_eq(fold(field(x, words), 0, fun(acc: Nat, w: Nat, acc + w)), 32768)))),
                ok(int(0) - int(pred(cv_value(field(x, words)))) - int(1), Text), err("integer does not fit in Int", Int)),
            if_then_else(field(x, negative), ok(int(0) - int(cv_value(field(x, words))), Text), ok(int(cv_value(field(x, words))), Text)))

    // ---- portable text dlm1:<family>:<payload> ----
    private fn pn_with(family: Text, r: Result<BigTextCalc, Text>) -> Result<BigTextCalc, Text> =
        match r { err(e) => err(e, BigTextCalc), ok(t) => ok(BigTextCalc(concat("dlm1:", concat(family, concat(":", field(t, value)))), field(t, budget)), Text) }
    fn pn_int(b: ExactBudget, x: BigInt) -> Result<BigTextCalc, Text> = pn_with("int", big_text(b, x))
    fn pn_rat(b: ExactBudget, x: BigRational) -> Result<BigTextCalc, Text> = pn_with("rat", bigrat_text(b, x))
    fn pn_ivq(b: ExactBudget, x: IntervalQ) -> Result<BigTextCalc, Text> = pn_with("ivq", ivq_text(b, x))
    fn pn_cq(b: ExactBudget, re: BigRational, im: BigRational) -> Result<BigTextCalc, Text> =
        match bigrat_text(b, re) { err(e) => err(e, BigTextCalc), ok(r) => match bigrat_text(field(r, budget), im) { err(e) => err(e, BigTextCalc), ok(i) =>
            pn_with("cq", ok(BigTextCalc(concat(field(r, value), concat(";", field(i, value))), field(i, budget)), Text)) } }
    fn pn_creal(b: ExactBudget, x: CReal) -> Result<BigTextCalc, Text> = pn_with("creal", creal_text(b, x))
    fn pn_ccomplex(b: ExactBudget, x: CComplex) -> Result<BigTextCalc, Text> = pn_with("ccomplex", cc_text(b, x))
    // canonical text of the payload parsed as family, or the parse error
    private fn pn_reformat(b: ExactBudget, family: Text, p: Text) -> Result<BigTextCalc, Text> =
        if_then_else(text_eq(family, "int"), match big_parse(b, p) { err(e) => err(e, BigTextCalc), ok(v) => pn_int(field(v, budget), field(v, value)) },
        if_then_else(text_eq(family, "rat"), match bigrat_parse(b, p) { err(e) => err(e, BigTextCalc), ok(v) => pn_rat(field(v, budget), field(v, value)) },
        if_then_else(text_eq(family, "ivq"), match ivq_parse(b, p) { err(e) => err(e, BigTextCalc), ok(v) => pn_ivq(field(v, budget), field(v, value)) },
        if_then_else(text_eq(family, "cq"), match cv_index(p, ";") { none => err("invalid portable number", BigTextCalc), some(i) =>
            match bigrat_parse(b, substring(p, 0, i)) { err(e) => err(e, BigTextCalc), ok(r) => match bigrat_parse(field(r, budget), substring(p, i + 1, text_length(p))) { err(e) => err(e, BigTextCalc), ok(m) =>
                pn_cq(field(m, budget), field(r, value), field(m, value)) } } },
        if_then_else(text_eq(family, "creal"), match creal_parse(b, p) { err(e) => err(e, BigTextCalc), ok(v) => pn_creal(field(v, budget), field(v, value)) },
        if_then_else(text_eq(family, "ccomplex"), match cc_parse(b, p) { err(e) => err(e, BigTextCalc), ok(v) => pn_ccomplex(field(v, budget), field(v, value)) },
            err(concat("unknown portable number family `", concat(family, "`")), BigTextCalc)))))))
    // Validates a portable text: ok(canonical text) or the refusal
    fn pn_check(b: ExactBudget, text: Text) -> Result<BigTextCalc, Text> =
        match cv_index(text, ":") { none => err("invalid portable number", BigTextCalc), some(i) =>
            let version = substring(text, 0, i) in let rest = substring(text, i + 1, text_length(text)) in
            if_then_else(not(text_eq(version, "dlm1")),
                if_then_else(and(text_eq(substring(version, 0, 3), "dlm"), cv_is_digits(substring(version, 3, text_length(version)))), err("unsupported portable number version", BigTextCalc), err("invalid portable number", BigTextCalc)),
            match cv_index(rest, ":") { none => err("invalid portable number", BigTextCalc), some(j) =>
                match pn_reformat(b, substring(rest, 0, j), substring(rest, j + 1, text_length(rest))) { err(e) => err(e, BigTextCalc), ok(t) =>
                    if_then_else(text_eq(field(t, value), text), ok(t, Text), err("non-canonical portable number", BigTextCalc)) } }) }
    fn pn_category(b: ExactBudget, text: Text) -> Text = match pn_check(b, text) {
        ok(t) => "ok",
        err(e) => if_then_else(text_eq(substring(e, 0, 21), "UnknownWithinBudget: "), "budget", if_then_else(text_eq(e, "non-canonical portable number"), "noncanonical", if_then_else(text_eq(e, "unsupported portable number version"), "version",
            if_then_else(text_eq(substring(e, 0, 30), "unknown portable number family"), "family", "invalid"))))
    }
    // Closed finite computations, not general rounding-law proofs.
    theorem half_even_ties_to_even: text_eq(match cv_decimal(exact_default_budget(), BigRational(false, list(5), list(2)), 0, 3) { err(e) => e, ok(t) => field(t, value) }, "2")
    theorem noncanonical_portable_is_refused: text_eq(pn_category(exact_default_budget(), "dlm1:rat:6/8"), "noncanonical")
}
