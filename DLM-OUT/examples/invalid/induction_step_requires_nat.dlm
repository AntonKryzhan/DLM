module examples.induction_step_requires_nat

theory T {
    theorem lists(xs: List<Nat>): nat_eq(length(xs), length(xs)) by induction xs step 2
}
