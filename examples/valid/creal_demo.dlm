module examples.creal_demo
import std.creal

// Computable reals: an interval of width <= 2^-n that is guaranteed to contain
// the number, domain errors only when the bounds prove them, and comparisons
// that say "undecided" (2) instead of guessing.
theory App extends StdCReal {
    let budget = ExactBudget(512, 1000000000000000)
    fn show(r: Result<IntervalQCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn order(r: Result<IntervalQOrder, Text>) -> Text = match r { err(e) => e, ok(v) => int_to_text(field(v, order)) }
    fn real(t: Text) -> CReal = match creal_parse(budget, t) {
        err(e) => CReal(empty_list(CRealNode)),
        ok(v) => field(v, value)
    }
    let root2 = real("c 2 ; sqrt 0")
    let phi = real("c 5 ; sqrt 0 ; c 1 ; add 1 2 ; c 2 ; div 3 4")
    print_line(show(creal_approx(budget, root2, 32)))
    print_line(show(creal_approx(budget, phi, 24)))
    print_line(order(creal_compare(budget, root2, real("c 7/5"), 8)))
    print_line(order(creal_compare(budget, real("c 2 ; sqrt 0 ; mul 1 1"), real("c 2"), 16)))
    print_line(order(creal_compare(budget, real("c 9 ; sqrt 0"), real("c 3"), 16)))
    print_line(show(creal_approx(budget, real("c 1 ; c 3 ; c 3 ; sub 1 2 ; div 0 3"), 8)))
    print_line(show(creal_approx(budget, real("c -2 ; sqrt 0"), 8)))
    print_line(show(creal_approx(ExactBudget(64, 1000000000000000), real("c 2 ; sqrt 0 ; sub 1 1 ; inv 2"), 8)))
}
