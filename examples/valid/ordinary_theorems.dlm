module examples.ordinary_theorems

// v0.76: statements, theorems and proofs.
//   theorem name: P                       proved by computation
//   theorem name(x: Nat) below N: P        proves the bounded statement by exhaustion
//   axiom name(x: T): P                    admitted, visibly Axiom-tainted
//   conjecture name(x: Nat) tested N: P    evidence only: an open goal, never a theorem
theory Arithmetic {
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn double(n: Nat) -> Nat = n + n
    fn total(xs: List<Nat>) -> Nat = fold(xs, 0, fun(acc: Nat, x: Nat, acc + x))

    theorem fact_5: nat_eq(fact(5), 120)
    theorem total_example: nat_eq(total(list(1, 2, 3, 4)), 10)
    theorem add_comm_below_50(n: Nat, m: Nat) below 50: nat_eq(n + m, m + n)
    theorem gauss_below_200(n: Nat) below 200: nat_eq(mul(2, sum_to(n)), mul(n, n + 1))
    theorem excluded_middle(b: Bool): or(b, not(b))
    axiom add_comm(n: Nat, m: Nat): nat_eq(n + m, m + n)
    conjecture double_monus(n: Nat) tested 1000: nat_eq(monus(double(n), n), n)

    print_symbolic(fact_5)
    print_symbolic(add_comm)
    print_symbolic(double_monus)
}
