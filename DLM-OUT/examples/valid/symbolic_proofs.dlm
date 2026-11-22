module examples.symbolic_proofs

// v0.77: symbolic proofs of unbounded statements.
//   by normalize         unfold definitions, normalize Nat polynomials, beta-reduce lambdas
//   by induction n       base case n = 0, step case n = k + 1 with the hypothesis for k
//   using lemma, ...     earlier unconditional theorems or axioms as rewrite rules
theory Symbolic {
    fn double(n: Nat) -> Nat = n + n
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn sum_odds(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, monus(mul(2, n), 1) + sum_odds(pred(n)))
    fn pow2(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(2, pow2(pred(n))))
    fn geom(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, pow2(pred(n)) + geom(pred(n)))
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    fn twice_add(a: Nat) -> Nat = add(a, a)
    fn twice(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(f(x))

    theorem add_zero(n: Nat): nat_eq(n + 0, n) by normalize
    theorem double_is_twice(n: Nat): nat_eq(double(n), mul(2, n)) by normalize
    theorem binomial(a: Nat, b: Nat): nat_eq(mul(a + b, a + b), mul(a, a) + mul(2, mul(a, b)) + mul(b, b)) by normalize
    theorem twice_inc(n: Nat): nat_eq(twice(fun(x: Nat, x + 1), n), n + 2) by normalize
    theorem succ_positive(n: Nat): n + 1 > 0 by normalize
    theorem gauss(n: Nat): nat_eq(mul(2, sum_to(n)), mul(n, n + 1)) by induction n
    theorem odd_squares(n: Nat): nat_eq(sum_odds(n), mul(n, n)) by induction n
    theorem geometric(n: Nat): nat_eq(geom(n) + 1, pow2(n)) by induction n
    theorem add_spec(a: Nat, b: Nat): nat_eq(add(a, b), a + b) by induction a
    theorem twice_add_spec(a: Nat): nat_eq(twice_add(a), mul(2, a)) by normalize using add_spec

    print_symbolic(gauss)
}
