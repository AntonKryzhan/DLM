module examples.recursion_inside_lambda

theory T {
    fn f(n: Nat) -> List<Nat> decreasing n = if_then_else(nat_eq(n, 0), empty_list(Nat), map(list(n), fun(k: Nat, length(f(pred(k))))))
}
