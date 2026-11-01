module examples.forward_call_without_mutual

theory T {
    fn ping(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, pong(pred(n)))
    fn pong(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, ping(pred(n)))
}
