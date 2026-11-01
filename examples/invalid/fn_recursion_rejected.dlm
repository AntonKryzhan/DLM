module examples.fn_recursion_rejected

theory T {
    fn loop(n: Nat) -> Nat = loop(n + 1)
}
