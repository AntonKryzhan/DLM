module examples.let_in_operator_needs_parentheses

theory T {
    fn f(n: Nat) -> Nat = 1 + let x = n in x + x
}
