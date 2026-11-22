module examples.let_alias_not_decreasing

theory T {
    fn f(n: Nat) -> Nat decreasing n =
        match n {
            0 => 0,
            succ(k) => let m = n in f(m)
        }
}
