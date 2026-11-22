module examples.effectful_function_in_theorem

theory T {
    fn loud(n: Nat) -> Nat = let _ = print_line("side effect") in n
    theorem loud_id(n: Nat): nat_eq(loud(n), n) by normalize
}
