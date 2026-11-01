module examples.match_not_exhaustive

theory T {
    fn f(n: Nat) -> Nat =
        match n {
            0 => 1,
            1 => 2
        }
}
