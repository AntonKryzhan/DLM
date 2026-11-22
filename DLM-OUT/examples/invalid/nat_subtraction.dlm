module examples.nat_subtraction

// Nat has no subtraction: use monus(a, b) or int(a) - int(b).
theory T {
    fn gap(a: Nat, b: Nat) -> Nat = a - b
}
