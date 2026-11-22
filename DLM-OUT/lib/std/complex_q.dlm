// Exact Q(i) in ordinary DLM. Every rational intermediate uses the same budget.
module std.complex_q
import std.exact

theory StdComplexQ extends StdExact {
    record ComplexQ(real: BigRational, imag: BigRational)
    record ComplexQCalc(value: ComplexQ, budget: ExactBudget)
    record ComplexQEqual(value: Bool, budget: ExactBudget)

    private fn cq_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn cq_one() -> BigRational = BigRational(false, list(1), list(1))
    fn complexq_check(b: ExactBudget, a: ComplexQ) -> Result<ComplexQCalc, Text> =
        match bigrat_check(b, field(a, real)) { err(e) => err(e, ComplexQCalc), ok(r) =>
            match bigrat_check(field(r, budget), field(a, imag)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(a, field(i, budget)), Text) }
        }
    fn complexq_new(b: ExactBudget, real: BigRational, imag: BigRational) -> Result<ComplexQCalc, Text> = complexq_check(b, ComplexQ(real, imag))
    fn complexq_from_ints(b: ExactBudget, real: Int, imag: Int) -> Result<ComplexQCalc, Text> =
        match big_from_int(b, real) { err(e) => err(e, ComplexQCalc), ok(r) =>
            match big_from_int(field(r, budget), imag) { err(e) => err(e, ComplexQCalc), ok(i) =>
                match bigrat_from_ints(field(i, budget), field(r, value), BigInt(false, list(1))) { err(e) => err(e, ComplexQCalc), ok(qr) =>
                    match bigrat_from_ints(field(qr, budget), field(i, value), BigInt(false, list(1))) { err(e) => err(e, ComplexQCalc), ok(qi) => ok(ComplexQCalc(ComplexQ(field(qr, value), field(qi, value)), field(qi, budget)), Text) }
                }
            }
        }
    private fn cq_prepare(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ExactBudget, Text> =
        match complexq_check(b, a) { err(e) => err(e, ExactBudget), ok(x) =>
            match complexq_check(field(x, budget), c) { err(e) => err(e, ExactBudget), ok(y) => ok(field(y, budget), Text) }
        }
    fn complexq_equal(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQEqual, Text> =
        match cq_prepare(b, a, c) { err(e) => err(e, ComplexQEqual), ok(left) =>
            match bigrat_cmp(left, field(a, real), field(c, real)) { err(e) => err(e, ComplexQEqual), ok(r) =>
                match bigrat_cmp(field(r, budget), field(a, imag), field(c, imag)) { err(e) => err(e, ComplexQEqual), ok(i) => ok(ComplexQEqual(and(int_eq(field(r, order), int(0)), int_eq(field(i, order), int(0))), field(i, budget)), Text) }
            }
        }
    fn complexq_neg(b: ExactBudget, a: ComplexQ) -> Result<ComplexQCalc, Text> =
        match complexq_check(b, a) { err(e) => err(e, ComplexQCalc), ok(checked) =>
            match bigrat_neg(field(checked, budget), field(a, real)) { err(e) => err(e, ComplexQCalc), ok(r) =>
                match bigrat_neg(field(r, budget), field(a, imag)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(ComplexQ(field(r, value), field(i, value)), field(i, budget)), Text) }
            }
        }
    fn complexq_conj(b: ExactBudget, a: ComplexQ) -> Result<ComplexQCalc, Text> =
        match complexq_check(b, a) { err(e) => err(e, ComplexQCalc), ok(checked) =>
            match bigrat_neg(field(checked, budget), field(a, imag)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(ComplexQ(field(a, real), field(i, value)), field(i, budget)), Text) }
        }
    private fn cq_components(b: ExactBudget, a: ComplexQ, c: ComplexQ, f: Fn<ExactBudget, BigRational, BigRational, Result<BigRationalCalc, Text>>) -> Result<ComplexQCalc, Text> =
        match cq_prepare(b, a, c) { err(e) => err(e, ComplexQCalc), ok(left) =>
            match f(left, field(a, real), field(c, real)) { err(e) => err(e, ComplexQCalc), ok(r) =>
                match f(field(r, budget), field(a, imag), field(c, imag)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(ComplexQ(field(r, value), field(i, value)), field(i, budget)), Text) }
            }
        }
    fn complexq_add(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> = cq_components(b, a, c, bigrat_add)
    fn complexq_sub(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> = cq_components(b, a, c, bigrat_sub)
    private fn cq_product(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> =
        match bigrat_mul(b, field(a, real), field(c, real)) { err(e) => err(e, ComplexQCalc), ok(ac) =>
            match bigrat_mul(field(ac, budget), field(a, imag), field(c, imag)) { err(e) => err(e, ComplexQCalc), ok(bd) =>
                match bigrat_sub(field(bd, budget), field(ac, value), field(bd, value)) { err(e) => err(e, ComplexQCalc), ok(r) =>
                    match bigrat_mul(field(r, budget), field(a, real), field(c, imag)) { err(e) => err(e, ComplexQCalc), ok(ad) =>
                        match bigrat_mul(field(ad, budget), field(a, imag), field(c, real)) { err(e) => err(e, ComplexQCalc), ok(bc) =>
                            match bigrat_add(field(bc, budget), field(ad, value), field(bc, value)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(ComplexQ(field(r, value), field(i, value)), field(i, budget)), Text) }
                        }
                    }
                }
            }
        }
    fn complexq_mul(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> =
        match cq_prepare(b, a, c) { err(e) => err(e, ComplexQCalc), ok(left) => cq_product(left, a, c) }
    private fn cq_norm(b: ExactBudget, a: ComplexQ) -> Result<BigRationalCalc, Text> =
        match bigrat_mul(b, field(a, real), field(a, real)) { err(e) => err(e, BigRationalCalc), ok(aa) =>
            match bigrat_mul(field(aa, budget), field(a, imag), field(a, imag)) { err(e) => err(e, BigRationalCalc), ok(bb) => bigrat_add(field(bb, budget), field(aa, value), field(bb, value)) }
        }
    fn complexq_norm_squared(b: ExactBudget, a: ComplexQ) -> Result<BigRationalCalc, Text> =
        match complexq_check(b, a) { err(e) => err(e, BigRationalCalc), ok(checked) => cq_norm(field(checked, budget), a) }
    fn complexq_div(b: ExactBudget, a: ComplexQ, c: ComplexQ) -> Result<ComplexQCalc, Text> =
        match cq_prepare(b, a, c) { err(e) => err(e, ComplexQCalc), ok(left) =>
            if_then_else(and(is_empty(field(field(c, real), numerator)), is_empty(field(field(c, imag), numerator))), err("exact complex division by zero", ComplexQCalc),
                match cq_norm(left, c) { err(e) => err(e, ComplexQCalc), ok(d) =>
                    match bigrat_neg(field(d, budget), field(c, imag)) { err(e) => err(e, ComplexQCalc), ok(ci) =>
                        match cq_product(field(ci, budget), a, ComplexQ(field(c, real), field(ci, value))) { err(e) => err(e, ComplexQCalc), ok(n) =>
                            match bigrat_div(field(n, budget), field(field(n, value), real), field(d, value)) { err(e) => err(e, ComplexQCalc), ok(r) =>
                                match bigrat_div(field(r, budget), field(field(n, value), imag), field(d, value)) { err(e) => err(e, ComplexQCalc), ok(i) => ok(ComplexQCalc(ComplexQ(field(r, value), field(i, value)), field(i, budget)), Text) }
                            }
                        }
                    }
                })
        }
    fn complexq_inv(b: ExactBudget, a: ComplexQ) -> Result<ComplexQCalc, Text> = complexq_div(b, ComplexQ(cq_one(), cq_zero()), a)
    fn complexq_text(b: ExactBudget, a: ComplexQ) -> Result<BigTextCalc, Text> =
        match complexq_check(b, a) { err(e) => err(e, BigTextCalc), ok(checked) =>
            match bigrat_text(field(checked, budget), field(a, real)) { err(e) => err(e, BigTextCalc), ok(r) =>
                match bigrat_text(field(r, budget), field(a, imag)) { err(e) => err(e, BigTextCalc), ok(i) => ok(BigTextCalc(concat(field(r, value), concat(";", field(i, value))), field(i, budget)), Text) }
            }
        }

    // Closed regression computations, not general field-law proof terms.
    theorem imaginary_unit_squares_to_minus_one:
        match complexq_mul(exact_default_budget(), ComplexQ(cq_zero(), cq_one()), ComplexQ(cq_zero(), cq_one())) {
            err(e) => false, ok(v) => match complexq_equal(field(v, budget), field(v, value), ComplexQ(BigRational(true, list(1), list(1)), cq_zero())) {
                err(e) => false, ok(same) => field(same, value)
            }
        }
    theorem complex_zero_division_is_rejected:
        is_err(complexq_div(exact_default_budget(), ComplexQ(cq_one(), cq_zero()), ComplexQ(cq_zero(), cq_zero())))
    theorem malformed_complex_imaginary_is_rejected:
        is_err(complexq_equal(exact_default_budget(), ComplexQ(cq_zero(), BigRational(true, empty_list(Nat), list(1))), ComplexQ(cq_one(), cq_zero())))

    record BigComplex(re: BigRational, im: BigRational)
    record BigComplexCalc(value: BigComplex, budget: ExactBudget)
    record BigComplexFlag(value: Bool, budget: ExactBudget)
    private fn bc_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn bc_one() -> BigRational = BigRational(false, list(1), list(1))
    private fn bc_is_zero(z: BigComplex) -> Bool = and(is_empty(field(field(z, re), numerator)), is_empty(field(field(z, im), numerator)))
    private fn bc_done(re: BigRationalCalc, im: BigRationalCalc) -> Result<BigComplexCalc, Text> =
        ok(BigComplexCalc(BigComplex(field(re, value), field(im, value)), field(im, budget)), Text)
    private fn bc_split_block(n: Nat, text: Text, i: Nat, found: Option<Nat>) -> Option<Nat> decreasing n fuel 32 =
        if_then_else(nat_eq(n, 0), found,
            bc_split_block(pred(n), text, i + 1, if_then_else(and(i > 0, or(text_eq(substring(text, i, 1), "+"), text_eq(substring(text, i, 1), "-"))), some(i), found)))
    private fn bc_split(n: Nat, text: Text, i: Nat, found: Option<Nat>) -> Option<Nat> decreasing n fuel 8192 =
        if_then_else(nat_eq(n, 0), found, bc_split(pred(n), text, i + 16, bc_split_block(16, text, i, found)))
    private fn bc_norm(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigRationalCalc, Text> =
        match bigrat_mul(b, a, a) { err(e) => err(e, BigRationalCalc), ok(aa) =>
            match bigrat_mul(field(aa, budget), c, c) { err(e) => err(e, BigRationalCalc), ok(cc) => bigrat_add(field(cc, budget), field(aa, value), field(cc, value)) } }

    fn bigc_new(b: ExactBudget, re: BigRational, im: BigRational) -> Result<BigComplexCalc, Text> =
        match bigrat_check(b, re) { err(e) => err(e, BigComplexCalc), ok(x) =>
            match bigrat_check(field(x, budget), im) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } }
    fn bigc_check(b: ExactBudget, z: BigComplex) -> Result<BigComplexCalc, Text> = bigc_new(b, field(z, re), field(z, im))
    fn bigc_from_rational(b: ExactBudget, re: BigRational) -> Result<BigComplexCalc, Text> = bigc_new(b, re, bc_zero())
    fn bigc_i(b: ExactBudget) -> Result<BigComplexCalc, Text> = bigc_new(b, bc_zero(), bc_one())
    fn bigc_parse(b: ExactBudget, text: Text) -> Result<BigComplexCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, BigComplexCalc), ok(valid) =>
            if_then_else(text_length(text) > mul(4, field(b, bits)) + 8, err("UnknownWithinBudget: exact complex input exceeded", BigComplexCalc),
                if_then_else(or(nat_eq(text_length(text), 0), not(text_eq(substring(text, pred(text_length(text)), 1), "i"))), err("invalid exact complex text: expected <re>+<im>i or <re>-<im>i", BigComplexCalc),
                    let body = substring(text, 0, pred(text_length(text))) in
                    match bc_split(div(text_length(body) + 15, 16), body, 0, none(Nat)) {
                        none => err("invalid exact complex text: expected <re>+<im>i or <re>-<im>i", BigComplexCalc),
                        some(p) =>
                            let magnitude = substring(body, p + 1, text_length(body)) in
                            if_then_else(is_none(text_to_nat(substring(magnitude, 0, 1))), err("invalid exact complex text: expected <re>+<im>i or <re>-<im>i", BigComplexCalc),
                                match bigrat_parse(valid, substring(body, 0, p)) { err(e) => err(e, BigComplexCalc), ok(x) =>
                                    match bigrat_parse(field(x, budget), magnitude) { err(e) => err(e, BigComplexCalc), ok(m) =>
                                        match if_then_else(text_eq(substring(body, p, 1), "-"), bigrat_neg(field(m, budget), field(m, value)), ok(m, Text)) {
                                            err(e) => err(e, BigComplexCalc), ok(y) => bigc_new(field(y, budget), field(x, value), field(y, value)) } } })
                    })) }
    fn bigc_text(b: ExactBudget, z: BigComplex) -> Result<BigTextCalc, Text> =
        match bigrat_text(b, field(z, re)) { err(e) => err(e, BigTextCalc), ok(r) =>
            let negative = field(field(z, im), negative) in
            match if_then_else(negative, bigrat_neg(field(r, budget), field(z, im)), bigrat_check(field(r, budget), field(z, im))) { err(e) => err(e, BigTextCalc), ok(m) =>
                match bigrat_text(field(m, budget), field(m, value)) { err(e) => err(e, BigTextCalc), ok(t) =>
                    ok(BigTextCalc(concat(field(r, value), concat(if_then_else(negative, "-", "+"), concat(field(t, value), "i"))), field(t, budget)), Text) } } }
    fn bigc_eq(b: ExactBudget, z: BigComplex, w: BigComplex) -> Result<BigComplexFlag, Text> =
        match bigrat_cmp(b, field(z, re), field(w, re)) { err(e) => err(e, BigComplexFlag), ok(x) =>
            match bigrat_cmp(field(x, budget), field(z, im), field(w, im)) { err(e) => err(e, BigComplexFlag), ok(y) =>
                ok(BigComplexFlag(and(int_eq(field(x, order), int(0)), int_eq(field(y, order), int(0))), field(y, budget)), Text) } }
    fn bigc_add(b: ExactBudget, z: BigComplex, w: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigrat_add(b, field(z, re), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(x) =>
            match bigrat_add(field(x, budget), field(z, im), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } }
    fn bigc_sub(b: ExactBudget, z: BigComplex, w: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigrat_sub(b, field(z, re), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(x) =>
            match bigrat_sub(field(x, budget), field(z, im), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } }
    fn bigc_neg(b: ExactBudget, z: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigrat_neg(b, field(z, re)) { err(e) => err(e, BigComplexCalc), ok(x) =>
            match bigrat_neg(field(x, budget), field(z, im)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } }
    fn bigc_conj(b: ExactBudget, z: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigrat_check(b, field(z, re)) { err(e) => err(e, BigComplexCalc), ok(x) =>
            match bigrat_neg(field(x, budget), field(z, im)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } }
    // (a+bi)(c+di): ac, bd, ad, bc, then ac-bd and ad+bc.
    fn bigc_mul(b: ExactBudget, z: BigComplex, w: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigrat_mul(b, field(z, re), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(ac) =>
        match bigrat_mul(field(ac, budget), field(z, im), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(bd) =>
        match bigrat_mul(field(bd, budget), field(z, re), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(ad) =>
        match bigrat_mul(field(ad, budget), field(z, im), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(bc) =>
        match bigrat_sub(field(bc, budget), field(ac, value), field(bd, value)) { err(e) => err(e, BigComplexCalc), ok(x) =>
        match bigrat_add(field(x, budget), field(ad, value), field(bc, value)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } } } } } }
    // Squared modulus a*a + b*b.
    fn bigc_norm(b: ExactBudget, z: BigComplex) -> Result<BigRationalCalc, Text> = bc_norm(b, field(z, re), field(z, im))
    // (a+bi)/(c+di) = (ac+bd)/n + ((bc-ad)/n)i with n = cc+dd; zero divisor refused after validation.
    fn bigc_div(b: ExactBudget, z: BigComplex, w: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigc_check(b, z) { err(e) => err(e, BigComplexCalc), ok(zc) =>
        match bigc_check(field(zc, budget), w) { err(e) => err(e, BigComplexCalc), ok(wc) =>
        if_then_else(bc_is_zero(w), err("exact complex division by zero", BigComplexCalc),
        match bc_norm(field(wc, budget), field(w, re), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(n) =>
        match bigrat_mul(field(n, budget), field(z, re), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(ac) =>
        match bigrat_mul(field(ac, budget), field(z, im), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(bd) =>
        match bigrat_add(field(bd, budget), field(ac, value), field(bd, value)) { err(e) => err(e, BigComplexCalc), ok(s) =>
        match bigrat_mul(field(s, budget), field(z, im), field(w, re)) { err(e) => err(e, BigComplexCalc), ok(bc) =>
        match bigrat_mul(field(bc, budget), field(z, re), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(ad) =>
        match bigrat_sub(field(ad, budget), field(bc, value), field(ad, value)) { err(e) => err(e, BigComplexCalc), ok(t) =>
        match bigrat_div(field(t, budget), field(s, value), field(n, value)) { err(e) => err(e, BigComplexCalc), ok(x) =>
        match bigrat_div(field(x, budget), field(t, value), field(n, value)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } } } } } } } } }) } }
    // 1/(c+di) = c/n - (d/n)i with n = cc+dd; zero refused after validation.
    fn bigc_inv(b: ExactBudget, w: BigComplex) -> Result<BigComplexCalc, Text> =
        match bigc_check(b, w) { err(e) => err(e, BigComplexCalc), ok(wc) =>
        if_then_else(bc_is_zero(w), err("exact complex division by zero", BigComplexCalc),
        match bc_norm(field(wc, budget), field(w, re), field(w, im)) { err(e) => err(e, BigComplexCalc), ok(n) =>
        match bigrat_div(field(n, budget), field(w, re), field(n, value)) { err(e) => err(e, BigComplexCalc), ok(x) =>
        match bigrat_div(field(x, budget), field(w, im), field(n, value)) { err(e) => err(e, BigComplexCalc), ok(q) =>
        match bigrat_neg(field(q, budget), field(q, value)) { err(e) => err(e, BigComplexCalc), ok(y) => bc_done(x, y) } } } }) }

    // Closed legacy finite computations, not generic algebraic proofs.
    theorem i_squared_is_minus_one: text_eq(field(unwrap_or(bigc_text(exact_default_budget(), field(unwrap_or(bigc_mul(exact_default_budget(), BigComplex(bc_zero(), bc_one()), BigComplex(bc_zero(), bc_one())), BigComplexCalc(BigComplex(bc_zero(), bc_zero()), exact_default_budget())), value)), BigTextCalc("", exact_default_budget())), value), "-1/1+0/1i")
    theorem division_by_zero_is_refused: is_err(bigc_div(exact_default_budget(), BigComplex(bc_one(), bc_zero()), BigComplex(bc_zero(), bc_zero())))
}
