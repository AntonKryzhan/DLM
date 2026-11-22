module examples.recursion_measure_not_decreasing

theory T {
    fn f(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, f(n + 1))
}
