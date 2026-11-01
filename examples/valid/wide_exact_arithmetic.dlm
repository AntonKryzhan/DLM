module examples.wide_exact_arithmetic
import std.exact

theory App extends StdExact {
    let wide = ExactBudget(16384, 18446744073709551615)
    fn zeros(n: Nat, xs: List<Nat>) -> List<Nat> decreasing n fuel 4096 =
        if_then_else(nat_eq(n, 0), xs, zeros(pred(n), cons(0, xs)))
    fn divide_wide(b: ExactBudget) -> Result<BigDivision, Text> =
        match big_from_words(b, false, zeros(1023, list(32768))) {
            err(e) => err(e, BigDivision),
            ok(v) => big_div_rem(field(v, budget), field(v, value), BigInt(false, list(3)))
        }
    fn summary(r: Result<BigDivision, Text>) -> Text = match r {
        err(e) => concat("ERR:", e),
        ok(v) => match big_text(field(v, budget), field(v, remainder)) {
            err(e) => concat("ERR:", e),
            ok(t) => concat(nat_to_text(length(field(field(v, quotient), words))), concat(":", field(t, value)))
        }
    }
    print_symbolic(is_ok(exact_budget(16384, 18446744073709551615)))
    print_line(summary(divide_wide(wide)))
    print_symbolic(is_err(exact_budget(1024, 1000000000000001)))
}
