module examples.recursion_fuel_exhausted

theory T {
    fn depth(n: Nat) -> Nat decreasing n fuel 3 = if_then_else(nat_eq(n, 0), 0, depth(pred(n)) + 1)
    print_decimal(depth(10))
}
