module examples.mutual_recursion

theory Parity {
    mutual {
        fn even_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, odd_n(pred(n)))
        fn odd_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), false, even_n(pred(n)))
    }
    mutual {
        fn even_pos(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + odd_pos(tail(xs)))
        fn odd_pos(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, even_pos(tail(xs)))
    }

    theorem even_double(n: Nat): even_n(mul(2, n)) by induction n
    theorem odd_next(n: Nat): odd_n(mul(2, n) + 1) by induction n
    theorem parity_split(n: Nat): or(even_n(n), odd_n(n)) by induction n step 2
    theorem even_not_odd(n: Nat): not(and(even_n(n), odd_n(n))) by induction n step 2

    let seven_odd = odd_n(7)
    print_decimal(even_pos(list(1, 2, 3, 4, 5)))
}
