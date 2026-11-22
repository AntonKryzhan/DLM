module examples.map_rejects_axiom_tainted_function

theory T {
    fn tainted(n: Nat) -> Nat = n + axiom_nat()
    let xs = map(list(1, 2), tainted)
}
