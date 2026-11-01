module examples.axiom_lemma_rejected_by_trusted_only

theory T {
    fn mystery(n: Nat) -> Nat = n
    axiom mystery_spec(n: Nat): nat_eq(mystery(n), n)
    theorem uses_axiom(n: Nat): nat_eq(mystery(n) + 1, n + 1) by normalize using mystery_spec
}
