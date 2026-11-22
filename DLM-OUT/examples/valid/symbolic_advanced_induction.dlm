module examples.symbolic_advanced_induction

theory Advanced {
    fn fib(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 1, fib(pred(n)) + fib(monus(n, 2))))
    fn half(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 0, half(monus(n, 2)) + 1))
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    fn add_acc(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add_acc(pred(a), b + 1))
    fn rev_len(xs: List<Nat>, acc: Nat) -> Nat decreasing xs = if_then_else(is_empty(xs), acc, rev_len(tail(xs), acc + 1))
    fn pick(b: Bool, n: Nat) -> Nat = if_then_else(b, n + 1, n)

    theorem fib_unfold(n: Nat): nat_eq(fib(n + 2), fib(n + 1) + fib(n)) by normalize
    theorem even_or_next(n: Nat): or(even(n), even(n + 1)) by induction n step 2
    theorem even_double(n: Nat): even(mul(2, n)) by induction n
    theorem half_double(n: Nat): nat_eq(half(mul(2, n)), n) by induction n
    theorem half_bound(n: Nat): not(mul(2, half(n)) > n) by induction n step 2
    theorem add_acc_spec(a: Nat, b: Nat): nat_eq(add_acc(a, b), a + b) by induction a generalizing b
    theorem rev_len_spec(xs: List<Nat>, acc: Nat): nat_eq(rev_len(xs, acc), length(xs) + acc) by induction xs generalizing acc
    theorem excluded_middle(b: Bool): or(b, not(b)) by normalize
    theorem pick_ge(b: Bool, n: Nat): not(n > pick(b, n)) by normalize
}
