// Complex numbers with computable real components over std.creal (numeric criterion N09).
// Each operation builds the component programs of its result by the construction of the host
// (math::ccomplex, docs/COMPUTABLE_COMPLEX.md); creal_approx then gives both components with
// width <= 2^-n. Raw records are revalidated; budgets are threaded.
module std.ccomplex
import std.creal

theory StdCComplex extends StdCReal {
    record CComplex(re: CReal, im: CReal)
    record CComplexCalc(value: CComplex, budget: ExactBudget)
    record CComplexApprox(re: IntervalQ, im: IntervalQ, budget: ExactBudget)
    private record CcParts(nodes: List<CRealNode>, a: Nat, b: Nat, c: Nat, d: Nat)
    private fn cc_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    private fn cc_arity(op: Nat) -> Nat = if_then_else(or(nat_eq(op, 0), nat_eq(op, 14)), 0, if_then_else(or(or(nat_eq(op, 1), nat_eq(op, 2)), or(nat_eq(op, 3), nat_eq(op, 6))), 2, 1))
    private fn cc_shift(n: CRealNode, k: Nat) -> CRealNode =
        CRealNode(field(n, op), if_then_else(cc_arity(field(n, op)) > 0, field(n, a) + k, 0), if_then_else(cc_arity(field(n, op)) > 1, field(n, b) + k, 0), field(n, value))
    private fn cc_rev(xs: List<CRealNode>) -> List<CRealNode> = fold(xs, empty_list(CRealNode), fun(r: List<CRealNode>, x: CRealNode, cons(x, r)))
    private fn cc_cat(xs: List<CRealNode>, ys: List<CRealNode>) -> List<CRealNode> = fold(cc_rev(xs), ys, fun(r: List<CRealNode>, x: CRealNode, cons(x, r)))
    private fn cc_embed(xs: List<CRealNode>, p: CReal) -> List<CRealNode> = let k = length(xs) in cc_cat(xs, map(field(p, nodes), fun(n: CRealNode, cc_shift(n, k))))
    private fn cc_op(xs: List<CRealNode>, op: Nat, a: Nat, b: Nat) -> List<CRealNode> = cc_cat(xs, list(CRealNode(op, a, b, cc_zero())))
    private fn cc_last(xs: List<CRealNode>) -> Nat = pred(length(xs))
    private fn cc_two(p: CReal, q: CReal) -> CcParts =
        let n1 = cc_embed(empty_list(CRealNode), p) in let n2 = cc_embed(n1, q) in CcParts(n2, cc_last(n1), cc_last(n2), 0, 0)
    private fn cc_four(p: CReal, q: CReal, r: CReal, s: CReal) -> CcParts =
        let n1 = cc_embed(empty_list(CRealNode), p) in let n2 = cc_embed(n1, q) in let n3 = cc_embed(n2, r) in let n4 = cc_embed(n3, s) in
        CcParts(n4, cc_last(n1), cc_last(n2), cc_last(n3), cc_last(n4))
    // x^2 + y^2 appended to xs; its root is the last node
    private fn cc_modsq(xs: List<CRealNode>, x: Nat, y: Nat) -> List<CRealNode> =
        let s1 = cc_op(xs, 8, x, 0) in let s2 = cc_op(s1, 8, y, 0) in cc_op(s2, 1, cc_last(s1), cc_last(s2))
    private fn cc_bin(p: CReal, q: CReal, op: Nat) -> CReal = let t = cc_two(p, q) in CReal(cc_op(field(t, nodes), op, field(t, a), field(t, b)))
    private fn cc_un(p: CReal, op: Nat) -> CReal = let n = cc_embed(empty_list(CRealNode), p) in CReal(cc_op(n, op, cc_last(n), 0))
    // two products of parts and their sum (op 1) or difference (op 2)
    private fn cc_pp(t: CcParts, x1: Nat, y1: Nat, x2: Nat, y2: Nat, op: Nat) -> List<CRealNode> =
        let m1 = cc_op(field(t, nodes), 3, x1, y1) in let m2 = cc_op(m1, 3, x2, y2) in cc_op(m2, op, cc_last(m1), cc_last(m2))
    private fn cc_quot(num: List<CRealNode>, x: Nat, y: Nat) -> CReal = let q = cc_modsq(num, x, y) in CReal(cc_op(q, 6, cc_last(num), cc_last(q)))
    private fn cc_done(b: ExactBudget, re: CReal, im: CReal) -> Result<CComplexCalc, Text> =
        match creal_check(b, re) { err(e) => err(e, CComplexCalc), ok(r) => match creal_check(field(r, budget), im) { err(e) => err(e, CComplexCalc), ok(i) => ok(CComplexCalc(CComplex(re, im), field(i, budget)), Text) } }

    fn cc_check(b: ExactBudget, z: CComplex) -> Result<CComplexCalc, Text> = cc_done(b, field(z, re), field(z, im))
    fn cc_from_real(b: ExactBudget, x: CReal) -> Result<CComplexCalc, Text> = cc_done(b, x, CReal(list(CRealNode(0, 0, 0, cc_zero()))))
    private fn cc_check2(b: ExactBudget, z: CComplex, w: CComplex) -> Result<ExactBudget, Text> =
        match cc_check(b, z) { err(e) => err(e, ExactBudget), ok(c) => match cc_check(field(c, budget), w) { err(e) => err(e, ExactBudget), ok(d) => ok(field(d, budget), Text) } }
    fn cc_add(b: ExactBudget, z: CComplex, w: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check2(b, z, w) { err(e) => err(e, CComplexCalc), ok(c) => cc_done(c, cc_bin(field(z, re), field(w, re), 1), cc_bin(field(z, im), field(w, im), 1)) }
    fn cc_sub(b: ExactBudget, z: CComplex, w: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check2(b, z, w) { err(e) => err(e, CComplexCalc), ok(c) => cc_done(c, cc_bin(field(z, re), field(w, re), 2), cc_bin(field(z, im), field(w, im), 2)) }
    fn cc_neg(b: ExactBudget, z: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check(b, z) { err(e) => err(e, CComplexCalc), ok(c) => cc_done(field(c, budget), cc_un(field(z, re), 4), cc_un(field(z, im), 4)) }
    fn cc_conj(b: ExactBudget, z: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check(b, z) { err(e) => err(e, CComplexCalc), ok(c) => cc_done(field(c, budget), field(z, re), cc_un(field(z, im), 4)) }
    // (ac - bd) + i(ad + bc)
    fn cc_mul(b: ExactBudget, z: CComplex, w: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check2(b, z, w) { err(e) => err(e, CComplexCalc), ok(c) =>
            let t = cc_four(field(z, re), field(z, im), field(w, re), field(w, im)) in
            cc_done(c, CReal(cc_pp(t, field(t, a), field(t, c), field(t, b), field(t, d), 2)), CReal(cc_pp(t, field(t, a), field(t, d), field(t, b), field(t, c), 1))) }
    // a/(a^2 + b^2) - i b/(a^2 + b^2)
    fn cc_inv(b: ExactBudget, z: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check(b, z) { err(e) => err(e, CComplexCalc), ok(c) =>
            let t = cc_two(field(z, re), field(z, im)) in
            let q = cc_modsq(field(t, nodes), field(t, a), field(t, b)) in
            let im = cc_op(q, 6, field(t, b), cc_last(q)) in
            cc_done(field(c, budget), CReal(cc_op(q, 6, field(t, a), cc_last(q))), CReal(cc_op(im, 4, cc_last(im), 0))) }
    // ((ac + bd) + i(bc - ad)) / (c^2 + d^2)
    fn cc_div(b: ExactBudget, z: CComplex, w: CComplex) -> Result<CComplexCalc, Text> =
        match cc_check2(b, z, w) { err(e) => err(e, CComplexCalc), ok(c) =>
            let t = cc_four(field(z, re), field(z, im), field(w, re), field(w, im)) in
            cc_done(c, cc_quot(cc_pp(t, field(t, a), field(t, c), field(t, b), field(t, d), 1), field(t, c), field(t, d)),
                cc_quot(cc_pp(t, field(t, b), field(t, c), field(t, a), field(t, d), 2), field(t, c), field(t, d))) }
    fn cc_abs_sq(b: ExactBudget, z: CComplex) -> Result<CRealCalc, Text> =
        match cc_check(b, z) { err(e) => err(e, CRealCalc), ok(c) => let t = cc_two(field(z, re), field(z, im)) in creal_check(field(c, budget), CReal(cc_modsq(field(t, nodes), field(t, a), field(t, b)))) }
    fn cc_abs(b: ExactBudget, z: CComplex) -> Result<CRealCalc, Text> =
        match cc_check(b, z) { err(e) => err(e, CRealCalc), ok(c) =>
            let t = cc_two(field(z, re), field(z, im)) in let q = cc_modsq(field(t, nodes), field(t, a), field(t, b)) in
            creal_check(field(c, budget), CReal(cc_op(q, 7, cc_last(q), 0))) }
    // Both components with width <= 2^-n.
    fn cc_approx(b: ExactBudget, z: CComplex, n: Nat) -> Result<CComplexApprox, Text> =
        match creal_approx(b, field(z, re), n) { err(e) => err(e, CComplexApprox), ok(r) =>
        match creal_approx(field(r, budget), field(z, im), n) { err(e) => err(e, CComplexApprox), ok(i) => ok(CComplexApprox(field(r, value), field(i, value), field(i, budget)), Text) } }
    // order: 1 certainly different, 0 the same exact point, 2 undecided
    fn cc_equal(b: ExactBudget, z: CComplex, w: CComplex, n: Nat) -> Result<IntervalQOrder, Text> =
        match cc_approx(b, z, n) { err(e) => err(e, IntervalQOrder), ok(p) =>
        match cc_approx(field(p, budget), w, n) { err(e) => err(e, IntervalQOrder), ok(q) =>
        match ivq_compare(field(q, budget), field(p, re), field(q, re)) { err(e) => err(e, IntervalQOrder), ok(r) =>
        match ivq_compare(field(r, budget), field(p, im), field(q, im)) { err(e) => err(e, IntervalQOrder), ok(i) =>
            let apart = or(or(int_eq(field(r, order), -1), int_eq(field(r, order), int(1))), or(int_eq(field(i, order), -1), int_eq(field(i, order), int(1)))) in
            ok(IntervalQOrder(if_then_else(apart, int(1), if_then_else(and(int_eq(field(r, order), int(0)), int_eq(field(i, order), int(0))), int(0), int(2))), field(i, budget)), Text) } } } }

    private fn cc_bar(n: Nat, tokens: List<Text>, i: Nat) -> Nat decreasing n fuel 1200 =
        if_then_else(nat_eq(n, 0), i, if_then_else(text_eq(unwrap_or(index(tokens, i), "|"), "|"), i, cc_bar(pred(n), tokens, i + 1)))
    private fn cc_take(n: Nat, tokens: List<Text>, acc: List<Text>) -> List<Text> decreasing n fuel 1200 =
        if_then_else(nat_eq(n, 0), fold(acc, empty_list(Text), fun(r: List<Text>, x: Text, cons(x, r))), cc_take(pred(n), tail(tokens), cons(unwrap_or(head(tokens), ""), acc)))
    private fn cc_drop(n: Nat, tokens: List<Text>) -> List<Text> decreasing n fuel 1200 =
        if_then_else(nat_eq(n, 0), tokens, cc_drop(pred(n), tail(tokens)))
    // Strict text `<re program> | <im program>`.
    fn cc_parse(b: ExactBudget, text: Text) -> Result<CComplexCalc, Text> =
        match exact_budget(field(b, bits), field(b, work)) { err(e) => err(e, CComplexCalc), ok(valid) =>
            if_then_else(text_length(text) > mul(128, mul(2, field(b, bits)) + 16) + 3, err("UnknownWithinBudget: computable complex input exceeded", CComplexCalc),
                let tokens = words(text) in
                let bars = fold(tokens, 0, fun(c: Nat, t: Text, if_then_else(text_eq(t, "|"), c + 1, c))) in
                if_then_else(or(or(not(nat_eq(bars, 1)), length(tokens) > 1100), not(text_eq(join(tokens, " "), text))), err("invalid computable complex text", CComplexCalc),
                    let i = cc_bar(length(tokens), tokens, 0) in
                    match creal_parse(valid, join(cc_take(i, tokens, empty_list(Text)), " ")) { err(e) => err(e, CComplexCalc), ok(r) =>
                    match creal_parse(field(r, budget), join(cc_drop(i + 1, tokens), " ")) { err(e) => err(e, CComplexCalc), ok(m) =>
                        ok(CComplexCalc(CComplex(field(r, value), field(m, value)), field(m, budget)), Text) } })) }
    fn cc_text(b: ExactBudget, z: CComplex) -> Result<BigTextCalc, Text> =
        match creal_text(b, field(z, re)) { err(e) => err(e, BigTextCalc), ok(r) =>
        match creal_text(field(r, budget), field(z, im)) { err(e) => err(e, BigTextCalc), ok(i) => ok(BigTextCalc(concat(field(r, value), concat(" | ", field(i, value))), field(i, budget)), Text) } }

    // Closed legacy finite computations, not general field-law proofs.
    theorem inverse_of_zero_is_refused: is_err(cc_approx(exact_default_budget(), unwrap_or(match cc_inv(exact_default_budget(), CComplex(CReal(list(CRealNode(0, 0, 0, cc_zero()))), CReal(list(CRealNode(0, 0, 0, cc_zero()))))) { err(e) => none(CComplex), ok(v) => some(field(v, value)) }, CComplex(CReal(empty_list(CRealNode)), CReal(empty_list(CRealNode)))), 4))
}
