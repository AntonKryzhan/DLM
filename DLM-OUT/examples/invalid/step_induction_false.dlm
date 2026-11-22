module examples.step_induction_false

theory T {
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    theorem all_even(n: Nat): even(n) by induction n step 2
}
