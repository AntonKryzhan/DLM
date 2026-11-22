module examples.exact_arithmetic
import std.exact

theory App extends StdExact {
    let budget = ExactBudget(512, 1000000000000000)
    let fallback = BigIntCalc(BigInt(false, empty_list(Nat)), budget)
    fn integer_text(r: Result<BigIntCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn rational_text(r: Result<BigRationalCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    let parsed = unwrap_or(big_parse(budget, "340282366920938463463374607431768211456"), fallback)
    let x = field(parsed, value)
    print_line(integer_text(big_add(field(parsed, budget), x, x)))
    print_line(integer_text(big_mul(field(parsed, budget), x, x)))
    print_line(rational_text(bigrat_parse(budget, "-340282366920938463463374607431768211456/3")))
    print_line(rational_text(bigrat_add(budget, BigRational(false, list(1), list(2)), BigRational(false, list(1), list(3)))))
    print_symbolic(is_err(big_add(ExactBudget(128, 0), BigInt(false, list(1)), BigInt(false, list(1)))))
}
