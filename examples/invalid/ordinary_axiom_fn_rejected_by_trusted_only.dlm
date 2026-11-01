module examples.ordinary_axiom_fn_rejected_by_trusted_only

theory T {
    fn tainted(n: Nat) -> Nat = n + axiom_nat()
    let x = tainted(1)
}
