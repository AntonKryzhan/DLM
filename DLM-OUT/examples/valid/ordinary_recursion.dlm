module examples.ordinary_recursion

// v0.74: well-founded recursion with a declared measure.
// A recursive call must pass pred(n) / monus(n, k>=1) / tail(xs)
// under a guard that makes the measure positive / non-empty.
theory Recursion {
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn sum_to(n: Nat, acc: Nat) -> Nat decreasing n = if_then_else(n > 0, sum_to(pred(n), acc + n), acc)
    fn halve_steps(n: Nat) -> Nat decreasing n fuel 64 = if_then_else(n > 1, halve_steps(monus(n, 2)) + 1, 0)
    fn total(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + total(tail(xs)))
    fn count_down(n: Nat) -> List<Nat> decreasing n = if_then_else(nat_eq(n, 0), empty_list(Nat), cons(n, count_down(pred(n))))

    print_decimal(fact(20))
    print_decimal(sum_to(100, 0))
    print_decimal(halve_steps(9))
    print_decimal(total(list(3, 4, 5)))
    print_symbolic(count_down(5))
    print_decimal(total(count_down(100)))
}
