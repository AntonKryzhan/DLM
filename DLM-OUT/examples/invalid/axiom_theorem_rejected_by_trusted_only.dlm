module examples.axiom_theorem_rejected_by_trusted_only

theory T {
    axiom add_comm(n: Nat, m: Nat): nat_eq(n + m, m + n)
}
