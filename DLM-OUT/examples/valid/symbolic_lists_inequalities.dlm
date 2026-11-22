module examples.symbolic_lists_inequalities

theory Lists {
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    fn total(xs: List<Nat>) -> Nat = fold(xs, 0, fun(acc: Nat, x: Nat, acc + x))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))

    theorem fact_positive(n: Nat): fact(n) > 0 by induction n
    theorem sum_to_bound(n: Nat): not(sum_to(n) > mul(n, n)) by induction n
    theorem square_ge(n: Nat): not(n > mul(n, n)) by induction n
    theorem len_is_length(xs: List<Nat>): nat_eq(len(xs), length(xs)) by induction xs
    theorem map_length(xs: List<Nat>): nat_eq(length(inc_all(xs)), length(xs)) by induction xs
    theorem cons_length(xs: List<Nat>, x: Nat): nat_eq(length(cons(x, xs)), length(xs) + 1) by normalize
}
