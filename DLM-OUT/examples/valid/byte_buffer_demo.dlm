module examples.byte_buffer_demo

// Owned mutable byte buffers (stage D): a sieve of Eratosthenes in one buffer,
// changed in place. The checker proves that every buffer has one owner on
// every path; the compiled program releases each buffer at the end of its
// owner's scope (see docs/OWNERSHIP.md).
theory App {
    fn strike(b: ByteBuf, i: Nat, step: Nat, n: Nat) -> ByteBuf decreasing n =
        if_then_else(nat_eq(n, 0), b, strike(buf_set(b, i, 0), i + step, step, pred(n)))
    // Strikes the multiples of every prime p with p * p < limit.
    fn sieve(b: ByteBuf, p: Nat, limit: Nat, steps: Nat) -> ByteBuf decreasing steps =
        if_then_else(nat_eq(steps, 0), b,
            if_then_else(mul(p, p) > pred(limit), b,
                if_then_else(nat_eq(unwrap_or(buf_get(b, p), 0), 1),
                    sieve(strike(b, mul(p, p), p, div(monus(pred(limit), mul(p, p)), p) + 1), p + 1, limit, pred(steps)),
                    sieve(b, p + 1, limit, pred(steps)))))
    fn count(b: &ByteBuf, i: Nat, n: Nat, acc: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), acc, count(b, i + 1, pred(n), acc + unwrap_or(buf_get(b, i), 0)))
    fn primes_below(limit: Nat) -> Nat =
        let b = buf_set(buf_set(buf_new(limit, 1), 0, 0), 1, 0) in
        let s = sieve(b, 2, limit, limit) in
        count(s, 0, limit, 0)
    fn render(xs: List<Nat>) -> Text = fold(xs, "", fun(acc: Text, x: Nat, concat(acc, concat(" ", nat_to_text(x)))))
    print_line(nat_to_text(primes_below(100)))
    print_line(nat_to_text(primes_below(1000)))
    print_line(render(buf_to_list(buf_push(buf_from_list(list(68, 76)), 77))))
    print_line(nat_to_text(let b = buf_new(3, 7) in unwrap_or(buf_get(b, 2), 0) + unwrap_or(buf_get(b, 3), 100)))
}
