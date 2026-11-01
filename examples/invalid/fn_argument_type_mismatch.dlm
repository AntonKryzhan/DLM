module examples.fn_argument_type_mismatch

theory T {
    fn double(n: Nat) -> Nat = n + n
    let x = double(true)
}
