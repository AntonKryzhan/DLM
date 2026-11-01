module examples.match_arm_unreachable

theory T {
    fn f(xs: List<Nat>) -> Nat =
        match xs {
            _ => 0,
            [] => 1
        }
}
