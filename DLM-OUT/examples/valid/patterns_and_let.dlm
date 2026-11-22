module examples.patterns_and_let

theory Patterns {
    // Multi-line declarations: a logical line continues while brackets are open
    // or after `=`, `,`, `=>`, `+`, ` in`.
    fn fact(n: Nat) -> Nat decreasing n =
        match n {
            0 => 1,
            succ(k) => mul(n, fact(k))
        }

    fn sum_list(xs: List<Nat>) -> Nat decreasing xs =
        match xs {
            [] => 0,
            cons(h, t) => h + sum_list(t)
        }

    fn fib(n: Nat) -> Nat decreasing n =
        match n {
            0 => 0,
            1 => 1,
            succ(k) => fib(k) + fib(pred(k))
        }

    fn or_zero(o: Option<Nat>) -> Nat =
        match o {
            none => 0,
            some(x) => x
        }

    fn describe(b: Bool) -> Nat = match b { true => 1, false => 0 }

    fn quad(n: Nat) -> Nat =
        let d = n + n in
        let q = d + d in
        q

    fn clamp(n: Nat) -> Nat =
        match n + 0 {
            0 => 0,
            1 => 1,
            _ => 2
        }

    theorem fact_positive(n: Nat): fact(n) > 0 by induction n
    theorem sum_list_inc(xs: List<Nat>):
        nat_eq(sum_list(map(xs, fun(x: Nat, x + 1))), sum_list(xs) + length(xs))
        by induction xs
    theorem quad_spec(n: Nat): nat_eq(quad(n), mul(4, n)) by normalize
    theorem fib_unfold(n: Nat): nat_eq(fib(n + 2), fib(n + 1) + fib(n)) by normalize
    theorem clamp_five: nat_eq(clamp(5), 2)
    theorem clamp_le(n: Nat): not(clamp(n) > 2) by normalize

    let total = sum_list(list(1, 2, 3, 4))
    print_decimal(total)
    print_decimal(fact(5))
    print_decimal(fib(10))
    print_decimal(or_zero(some(7)) + describe(true))
    print_decimal(let a = 20 in (let b = 22 in a + b))
}
