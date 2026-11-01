module examples.symbolic_proof_fails

theory T {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    theorem sum_is_n(n: Nat): nat_eq(sum_to(n), n) by induction n
}
