module examples.exact_scalar_operations
import std.exact

theory App extends StdExact {
    let budget = ExactBudget(512, 1000000000000000)
    fn integer_text(r: Result<BigIntCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn rational_text(r: Result<BigRationalCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn root_text(r: Result<BigSquareRoot, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match big_text(field(v, budget), field(v, root)) {
            err(e) => e,
            ok(s) => match big_text(field(s, budget), field(v, remainder)) {
                err(e) => e,
                ok(t) => concat(field(s, value), concat(",", field(t, value)))
            }
        }
    }
    fn lcm_demo(b: ExactBudget) -> Result<BigIntCalc, Text> =
        match big_from_int(b, -170141183460469231731687303715884105728) {
            err(e) => err(e, BigIntCalc),
            ok(v) => big_lcm(field(v, budget), field(v, value), BigInt(false, list(6)))
        }
    fn root_demo(b: ExactBudget) -> Result<BigSquareRoot, Text> =
        match big_parse(b, "115792089237316195423570985008687907853269984665640564039457584007913129639935") {
            err(e) => err(e, BigSquareRoot),
            ok(v) => big_sqrt_rem(field(v, budget), field(v, value))
        }
    print_line(integer_text(big_pow(budget, BigInt(true, list(2)), 129)))
    print_line(integer_text(lcm_demo(budget)))
    print_line(root_text(root_demo(budget)))
    print_line(rational_text(bigrat_pow(budget, BigRational(true, list(2), list(3)), -5)))
}
