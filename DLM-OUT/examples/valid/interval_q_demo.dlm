module examples.interval_q_demo
import std.interval_q

// Closed rational intervals: every result encloses all exact results,
// square roots come with guaranteed bounds, zero divisors are refused.
theory App extends StdIntervalQ {
    let budget = ExactBudget(512, 1000000000000000)
    fn text(r: Result<IntervalQCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match ivq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn parsed(t: Text) -> IntervalQ = match ivq_parse(budget, t) {
        err(e) => IntervalQ(BigRational(false, empty_list(Nat), list(1)), BigRational(false, empty_list(Nat), list(1))),
        ok(v) => field(v, value)
    }
    let x = parsed("[-1/2,3/4]")
    let two = parsed("[2,2]")
    print_line(text(ivq_sqrt(budget, two, 32)))
    print_line(text(ivq_sub(budget, x, x)))
    print_line(text(ivq_square(budget, x)))
    print_line(text(ivq_mul(budget, x, x)))
    print_line(text(ivq_div(budget, parsed("[1,2]"), parsed("[3,4]"))))
    print_line(text(ivq_div(budget, parsed("[1,2]"), x)))
    print_line(text(ivq_hull(budget, x, parsed("[2,3]"))))
}
