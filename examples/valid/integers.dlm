module examples.integers

// v0.85: exact signed integers. `-` exists only on Int (Nat keeps monus),
// literals are Nat unless negated (`-7`) or converted (`int(3)`), and
// div/mod are total: div(a, 0) = 0, mod(a, 0) = a, Euclidean for Int.
theory Integers {
    fn sq(i: Int) -> Int = mul(i, i)
    fn diff(a: Nat, b: Nat) -> Int = int(a) - int(b)
    fn sign(i: Int) -> Int =
        match int_eq(i, int(0)) {
            true => int(0),
            false => if_then_else(i > int(0), int(1), -1)
        }
    fn balance(xs: List<Nat>, ys: List<Nat>) -> Int = int(length(xs)) - int(length(ys))

    theorem sub_self(i: Int): int_eq(i - i, int(0)) by normalize
    theorem neg_neg(i: Int): int_eq(-(-i), i) by normalize
    theorem square_expand(a: Int, b: Int): int_eq(sq(a - b), sq(a) - mul(int(2), mul(a, b)) + sq(b)) by normalize
    theorem diff_antisymmetric(a: Nat, b: Nat): int_eq(diff(a, b), -diff(b, a)) by normalize
    theorem abs_example: nat_eq(abs(-7) + abs(int(3)), 10)
    theorem euclid: int_eq(mul(div(-7, int(2)), int(2)) + mod(-7, int(2)), -7)
    theorem div_by_zero(n: Nat): nat_eq(div(n, 0), 0) by normalize
    theorem abs_symmetric(a: Int, b: Int): nat_eq(abs(a - b), abs(b - a)) by normalize
    theorem step_up(i: Int): i + int(1) > i by normalize

    print_decimal(diff(3, 10))
    print_line(int_to_text(sq(-12)))
    print_symbolic(list(div(-7, int(2)), mod(-7, int(2)), div(int(7), -2), mod(int(7), -2)))
    print_symbolic(text_to_int("-42"))
    print_decimal(sign(diff(2, 5)) + sign(int(9)))
    print_decimal(div(17, 5) + mod(17, 5))

    // Exit status 0 when 4 - 11 is negative.
    fn main() -> Nat = if_then_else(int_eq(sign(diff(4, 11)), -1), 0, 1)
}
