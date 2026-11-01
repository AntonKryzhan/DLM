module examples.function_value_type_mismatch

theory T {
    fn apply(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(x)
    let r = apply(fun(b: Bool, 1), 3)
}
