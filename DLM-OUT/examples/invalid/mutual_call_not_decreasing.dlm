module examples.mutual_call_not_decreasing

theory T {
    mutual {
        fn ping(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, pong(n))
        fn pong(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, ping(pred(n)))
    }
}
