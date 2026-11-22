module examples.fn_return_type_mismatch

theory T {
    fn is_zero(n: Nat) -> Bool = n + 0
}
