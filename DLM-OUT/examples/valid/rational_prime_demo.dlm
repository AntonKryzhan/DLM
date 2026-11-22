module examples.rational_prime_demo
import std.rational
import std.homology_fp

theory RationalDemo extends StdRational {
    let half = Rational(false, 1, 2)
    let third = Rational(false, 1, 3)
    fn show(r: Result<Rational, Text>) -> Text = match r { err(e) => e, ok(q) => unwrap_or(rat_text(q), "invalid") }
    print_line(show(rat_add(half, third)))
    print_line(show(rat_sub(third, half)))
    print_line(show(rat_div(half, third)))
    print_symbolic(rat_cmp(half, third))
    print_symbolic(is_err(rat_inv(Rational(false, 0, 1))))
}
theory PrimeHomologyDemo extends StdHomologyFp {
    let d1 = FpMatrix(3, 3, 3, list(2, 2, 0, 1, 0, 2, 0, 1, 1))
    let d2 = FpMatrix(3, 3, 1, list(1, 2, 1))
    print_symbolic(homology_fp(d1, d2))
    print_symbolic(pmat_kernel(FpMatrix(5, 1, 2, list(2, 1))))
}
