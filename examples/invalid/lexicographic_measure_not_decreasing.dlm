module examples.lexicographic_measure_not_decreasing

theory T {
    fn f(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(n, 0), 0, f(m + 1, pred(n)))
}
