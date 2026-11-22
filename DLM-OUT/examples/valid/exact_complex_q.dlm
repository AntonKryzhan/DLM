module examples.exact_complex_q
import std.complex_q

theory App extends StdComplexQ {
    fn show(r: Result<ComplexQCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match complexq_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    fn qshow(r: Result<BigRationalCalc, Text>) -> Text = match r {
        err(e) => e,
        ok(v) => match bigrat_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) }
    }
    let b = ExactBudget(256, 1000000000000000)
    let z = ComplexQ(BigRational(false, list(1), list(2)), BigRational(false, list(3), list(4)))
    let w = ComplexQ(BigRational(true, list(2), list(1)), BigRational(false, list(1), list(3)))
    print_line(show(complexq_mul(b, z, w)))
    print_line(show(complexq_div(b, z, w)))
    print_line(show(complexq_conj(b, z)))
    print_line(qshow(complexq_norm_squared(b, z)))
    print_symbolic(is_err(complexq_div(b, z, ComplexQ(BigRational(false, empty_list(Nat), list(1)), BigRational(false, empty_list(Nat), list(1))))))
}
