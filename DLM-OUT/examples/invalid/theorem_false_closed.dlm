module examples.theorem_false_closed

theory T {
    fn double(n: Nat) -> Nat = n + n
    theorem wrong: nat_eq(double(3), 7)
}
