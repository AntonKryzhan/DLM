module examples.ordinary_products

// v0.73: product values and projections.
theory Products {
    fn swap(p: Product<Nat, Bool>) -> Product<Bool, Nat> = pair(snd(p), fst(p))
    fn first_of(p: Product<Product<Nat, Bool>, Nat>) -> Nat = fst(fst(p))

    let p = pair(7, true)
    let q = swap(p)
    let nested = pair(p, 3)
    let points = list(pair(1, 2), pair(3, 4))

    print_symbolic(p)
    print_symbolic(q)
    print_decimal(first_of(nested))
    print_decimal(snd(nested))
    print_symbolic(points)
}
