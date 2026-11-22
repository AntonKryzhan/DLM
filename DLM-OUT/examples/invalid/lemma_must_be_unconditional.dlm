module examples.lemma_must_be_unconditional

theory T {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    theorem small(n: Nat) below 10: nat_eq(mul(2, sum_to(n)), mul(n, n + 1))
    theorem general(n: Nat): nat_eq(mul(2, sum_to(n)), mul(n, n + 1)) by normalize using small
}
