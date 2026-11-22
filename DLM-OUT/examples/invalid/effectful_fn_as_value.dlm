module examples.effectful_fn_as_value

theory T {
    fn shout(n: Nat) -> Text = print_decimal(n)
    let handler = shout
}
