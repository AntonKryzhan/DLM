module examples.ordinary_functions

// v0.73: ordinary non-recursive functions with checked contracts.
theory Arith {
    fn double(n: Nat) -> Nat = n + n
    fn square(n: Nat) -> Nat = mul(n, n)
    fn is_big(n: Nat) -> Bool = n > 10
    fn clamp(n: Nat) -> Nat = if_then_else(is_big(n), 10, n)
    fn distance(a: Nat, b: Nat) -> Nat = monus(a, b) + monus(b, a)
    fn same(a: Nat, b: Nat) -> Bool = nat_eq(a, b)

    let a = double(21)
    let b = square(12)
    let c = clamp(b)
    let d = distance(3, 10)
    let e = pred(a)
    let f = and(same(a, 42), not(is_big(5)))

    print_decimal(a)
    print_decimal(b)
    print_decimal(c)
    print_decimal(d)
    print_decimal(e)
    print_symbolic(f)
}
