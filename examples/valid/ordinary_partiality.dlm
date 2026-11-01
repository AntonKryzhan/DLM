module examples.ordinary_partiality

// v0.73: Option / Result values instead of null or exceptions.
theory Partial {
    fn nonzero(n: Nat) -> Option<Nat> = if_then_else(n > 0, some(n), none(Nat))
    fn checked(n: Nat) -> Result<Nat, Nat> = if_then_else(n > 100, err(n, Nat), ok(n, Nat))

    let maybe = nonzero(8)
    let nothing = nonzero(0)
    let value = unwrap_or(maybe, 0)
    let fallback = unwrap_or(nothing, 7)
    let good = checked(5)
    let bad = checked(500)
    let flags = list(is_some(maybe), is_none(nothing), is_ok(good), is_err(bad))

    print_symbolic(maybe)
    print_symbolic(nothing)
    print_decimal(value)
    print_decimal(fallback)
    print_symbolic(good)
    print_symbolic(bad)
    print_decimal(unwrap_or(good, 0))
    print_decimal(unwrap_or(bad, 0))
    print_symbolic(flags)
}
