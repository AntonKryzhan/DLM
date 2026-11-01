module examples.map_requires_verified_contract

theory T {
    fn noisy(n: Nat) -> Text = print_decimal(n)
    let xs = map(list(1, 2), noisy)
}
