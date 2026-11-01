module examples.complex_q_demo
import std.complex_q

// Exact Gaussian rationals Q(i): every result is exact, budgets are threaded,
// errors are values.
theory App extends StdComplexQ {
    let budget = ExactBudget(256, 1000000000000000)
    fn text(r: Result<BigComplexCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match bigc_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn rational_text(r: Result<BigRationalCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn parsed(t: Text) -> BigComplex = match bigc_parse(budget, t) {
        err(e) => BigComplex(BigRational(false, empty_list(Nat), list(1)), BigRational(false, empty_list(Nat), list(1))),
        ok(v) => field(v, value)
    }
    fn conj_value(a: BigComplex) -> BigComplex = match bigc_conj(budget, a) { err(e) => a, ok(v) => field(v, value) }
    let z = parsed("3+4i")
    let w = parsed("1/2-2/3i")
    let i = parsed("0+1i")
    print_line(text(bigc_mul(budget, i, i)))
    print_line(rational_text(bigc_norm(budget, z)))
    print_line(text(bigc_inv(budget, z)))
    print_line(text(bigc_div(budget, z, w)))
    print_line(text(bigc_mul(budget, z, conj_value(z))))
    print_line(text(bigc_div(budget, z, parsed("0+0i"))))
    print_line(text(bigc_parse(budget, "1/2+3")))
}
