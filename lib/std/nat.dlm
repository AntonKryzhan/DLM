// std.nat — natural-number functions with proved specifications.
module std.nat

theory StdNat {
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    fn double(n: Nat) -> Nat = n + n
    fn square(n: Nat) -> Nat = mul(n, n)
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn max(a: Nat, b: Nat) -> Nat = if_then_else(a > b, a, b)
    fn min(a: Nat, b: Nat) -> Nat = if_then_else(a > b, b, a)
    fn le(a: Nat, b: Nat) -> Bool = not(a > b)
    fn half(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 0, half(monus(n, 2)) + 1))
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    private fn add_acc(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add_acc(pred(a), b + 1))
    fn add_fast(a: Nat, b: Nat) -> Nat = add_acc(a, b)

    theorem add_spec(a: Nat, b: Nat): nat_eq(add(a, b), a + b) by induction a
    theorem double_spec(n: Nat): nat_eq(double(n), mul(2, n)) by normalize
    theorem gauss(n: Nat): nat_eq(mul(2, sum_to(n)), mul(n, n + 1)) by induction n
    theorem fact_positive(n: Nat): fact(n) > 0 by induction n
    theorem square_ge(n: Nat): not(n > square(n)) by induction n
    theorem sum_to_le_square(n: Nat): not(sum_to(n) > square(n)) by induction n
    theorem half_double(n: Nat): nat_eq(half(mul(2, n)), n) by induction n
    theorem half_bound(n: Nat): not(mul(2, half(n)) > n) by induction n step 2
    theorem even_double(n: Nat): even(mul(2, n)) by induction n
    theorem even_or_next(n: Nat): or(even(n), even(n + 1)) by induction n step 2
    private theorem add_acc_spec(a: Nat, b: Nat): nat_eq(add_acc(a, b), a + b) by induction a generalizing b
    theorem add_fast_spec(a: Nat, b: Nat): nat_eq(add_fast(a, b), a + b) by normalize using add_acc_spec
}
