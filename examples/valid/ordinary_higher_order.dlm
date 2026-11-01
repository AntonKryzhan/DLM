module examples.ordinary_higher_order

// v0.75: lambdas, function values, higher-order functions, closures.
// Function values are pure and total by construction.
theory HigherOrder {
    fn twice(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(f(x))
    fn compose(f: Fn<Nat, Nat>, g: Fn<Nat, Nat>) -> Fn<Nat, Nat> = fun(x: Nat, f(g(x)))
    fn adder(k: Nat) -> Fn<Nat, Nat> = fun(n: Nat, n + k)
    fn double(n: Nat) -> Nat = n + n
    fn iterate(f: Fn<Nat, Nat>, n: Nat, x: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), x, iterate(f, pred(n), f(x)))

    let inc = fun(n: Nat, n + 1)
    let add5 = adder(5)
    let pipeline = compose(double, add5)
    let curry = fun(n: Nat, fun(m: Nat, mul(n, m)))
    let times3 = curry(3)
    let xs = list(1, 2, 3)
    let shifted = map(xs, fun(n: Nat, n + 10))
    let total = fold(xs, 0, fun(acc: Nat, x: Nat, acc + x))
    let fs = list(inc, double, add5, times3)
    let applied = map(fs, fun(f: Fn<Nat, Nat>, f(100)))
    let chosen = if_then_else(total > 5, double, inc)

    print_decimal(twice(inc, 0))
    print_decimal(add5(1))
    print_decimal(pipeline(1))
    print_symbolic(shifted)
    print_decimal(total)
    print_symbolic(applied)
    print_decimal(iterate(double, 10, 1))
    print_decimal(chosen(21))
    print_decimal(case(inl(4, Bool), fun(n: Nat, n), fun(b: Bool, if_then_else(b, 1, 0))))
    print_symbolic(inc)
}
