module examples.recursion_without_measure

theory T {
    fn f(n: Nat) -> Nat = if_then_else(nat_eq(n, 0), 0, f(pred(n)))
}
