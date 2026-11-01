module examples.generalize_induction_variable

theory T {
    theorem self_general(n: Nat): nat_eq(n, n) by induction n generalizing n
}
