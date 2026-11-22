module examples.ccomplex_demo
import std.ccomplex

// Complex numbers with computable real components: both parts come with
// guaranteed intervals of width <= 2^-n; equality is decided only by the bounds.
theory App extends StdCComplex {
    let budget = ExactBudget(512, 1000000000000000)
    fn show(r: Result<CComplexApprox, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match ivq_text(field(v, budget), field(v, re)) { err(e) => e, ok(a) =>
            match ivq_text(field(a, budget), field(v, im)) { err(e) => e, ok(c) => concat(field(a, value), concat(" | ", field(c, value))) } }
    }
    fn rshow(r: Result<CRealCalc, Text>) -> Text = match r { err(e) => e, ok(x) =>
        match creal_approx(field(x, budget), field(x, value), 16) { err(e) => e, ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } } }
    fn z(t: Text) -> CComplex = match cc_parse(budget, t) {
        err(e) => CComplex(CReal(empty_list(CRealNode)), CReal(empty_list(CRealNode))),
        ok(v) => field(v, value)
    }
    fn calc(r: Result<CComplexCalc, Text>) -> CComplex = match r {
        err(e) => CComplex(CReal(empty_list(CRealNode)), CReal(empty_list(CRealNode))),
        ok(v) => field(v, value)
    }
    let i = z("c 0 | c 1")
    let w = z("c 2 ; sqrt 0 | c 1/3")
    print_line(show(cc_approx(budget, calc(cc_mul(budget, i, i)), 8)))
    print_line(show(cc_approx(budget, calc(cc_mul(budget, w, calc(cc_conj(budget, w)))), 20)))
    print_line(show(cc_approx(budget, calc(cc_div(budget, z("c 1 | c 2"), z("c 3 | c -4"))), 8)))
    print_line(rshow(cc_abs(budget, w)))
    print_line(int_to_text(match cc_equal(budget, calc(cc_mul(budget, i, i)), z("c -1 | c 0"), 8) { err(e) => int(9), ok(o) => field(o, order) }))
    print_line(show(cc_approx(budget, calc(cc_inv(budget, z("c 0 | c 0"))), 8)))
}
