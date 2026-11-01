module examples.ordinary_lexicographic

// v0.75: lexicographic measures. A recursive call may decrease an earlier
// measured parameter freely, or keep it unchanged and decrease a later one.
theory Lexicographic {
    fn ack(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(m, 0), n + 1, if_then_else(nat_eq(n, 0), ack(pred(m), 1), ack(pred(m), ack(m, pred(n)))))
    fn grid(rows: Nat, cols: Nat, width: Nat) -> Nat decreasing (rows, cols) = if_then_else(nat_eq(rows, 0), 0, if_then_else(nat_eq(cols, 0), grid(pred(rows), width, width), grid(rows, pred(cols), width) + 1))

    print_decimal(ack(2, 3))
    print_decimal(ack(3, 3))
    print_decimal(grid(4, 5, 5))
}
