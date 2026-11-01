module examples.recursion_unguarded_call

theory T {
    fn f(n: Nat) -> Nat decreasing n = f(pred(n))
}
