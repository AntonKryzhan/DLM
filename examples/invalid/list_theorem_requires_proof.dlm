module examples.list_theorem_requires_proof

theory T {
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    theorem len_is_length(xs: List<Nat>): nat_eq(len(xs), length(xs))
}
