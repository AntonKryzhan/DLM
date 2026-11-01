module examples.ordinary_collections

// v0.73: finite collections with bounded map/fold over verified contracts.
theory Collections {
    fn double(n: Nat) -> Nat = n + n
    fn add(acc: Nat, x: Nat) -> Nat = acc + x
    fn wrap(n: Nat) -> Option<Nat> = some(n)

    let xs = list(1, 2, 3, 4)
    let doubled = map(xs, double)
    let total = fold(doubled, 0, add)
    let wrapped = map(xs, wrap)
    let steps = sequence(10, 20, 30)
    let step_sum = fold(steps, 0, add)
    let nothing = empty_list(Nat)
    let mixed = list(some(1), none(Nat))

    print_symbolic(doubled)
    print_decimal(total)
    print_symbolic(wrapped)
    print_decimal(length(xs))
    print_symbolic(index(xs, 2))
    print_symbolic(index(xs, 9))
    print_symbolic(head(nothing))
    print_decimal(length(nothing))
    print_symbolic(steps)
    print_decimal(step_sum)
    print_symbolic(mixed)
}
