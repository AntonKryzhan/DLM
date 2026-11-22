module examples.false_axiom_caught_by_crosscheck

theory T {
    fn half(n: Nat) -> Nat = n
    axiom half_spec(n: Nat): nat_eq(half(n), 0)
    theorem half_zero(n: Nat): nat_eq(half(n) + 1, 1) by normalize using half_spec
}
