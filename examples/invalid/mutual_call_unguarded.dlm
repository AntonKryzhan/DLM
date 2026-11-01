module examples.mutual_call_unguarded

theory T {
    mutual {
        fn ping(n: Nat) -> Nat decreasing n = pong(pred(n))
        fn pong(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, ping(pred(n)))
    }
}
