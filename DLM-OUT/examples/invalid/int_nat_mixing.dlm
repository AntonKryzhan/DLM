module examples.int_nat_mixing

// Int and Nat are never mixed implicitly: write int(n).
theory T {
    fn shift(i: Int, n: Nat) -> Int = i + n
}
