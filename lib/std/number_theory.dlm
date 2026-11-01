// Number theory, finite fields GF(p^k) and permutation groups (v0.118, docs/ALGEBRA.md) on top of the
// native nt_*, pf_*, pm_* builtins.
module std.number_theory
import std.optimize

theory StdNumberTheory extends StdOptimize {
    record Zmod(value: Nat, modulus: Nat)
    record Gf(coeffs: NatVec, modulus: NatVec, p: Nat)
    record RsaKey(n: Nat, e: Nat, d: Nat)
    record PermGroup(n: Nat, gens: NatVec)

    // ---- integers modulo m ----
    fn zmod(x: Nat, m: Nat) -> Option<Zmod> = if_then_else(nat_eq(m, 0), none(Zmod), some(Zmod(mod(x, m), m)))
    private fn same(a: Zmod, b: Zmod) -> Bool = nat_eq(field(a, modulus), field(b, modulus))
    fn zm_add(a: Zmod, b: Zmod) -> Option<Zmod> = if_then_else(same(a, b), zmod(field(a, value) + field(b, value), field(a, modulus)), none(Zmod))
    fn zm_sub(a: Zmod, b: Zmod) -> Option<Zmod> = if_then_else(same(a, b), zmod(monus(field(a, value) + field(a, modulus), field(b, value)), field(a, modulus)), none(Zmod))
    fn zm_mul(a: Zmod, b: Zmod) -> Option<Zmod> =
        if_then_else(same(a, b), match nt_mod_mul(field(a, value), field(b, value), field(a, modulus)) { none => none(Zmod), some(v) => some(Zmod(v, field(a, modulus))) }, none(Zmod))
    fn zm_pow(a: Zmod, e: Nat) -> Zmod = Zmod(unwrap_or(nt_mod_pow(field(a, value), e, field(a, modulus)), 0), field(a, modulus))
    fn zm_inv(a: Zmod) -> Option<Zmod> = match nt_mod_inv(field(a, value), field(a, modulus)) { none => none(Zmod), some(v) => some(Zmod(v, field(a, modulus))) }
    fn zm_div(a: Zmod, b: Zmod) -> Option<Zmod> = match zm_inv(b) { none => none(Zmod), some(i) => zm_mul(a, i) }

    // ---- arithmetic functions ----
    fn is_perfect_square(n: Nat) -> Bool = let r = nt_isqrt(n) in nat_eq(mul(r, r), n)
    fn sigma(n: Nat) -> Option<Nat> = match nt_divisors(n) { none => none(Nat), some(ds) => nv_sum(ds) }
    fn divisor_count(n: Nat) -> Nat = match nt_divisors(n) { none => 0, some(ds) => nv_len(ds) }
    private fn squarefree_loop(k: Nat, f: NatVec, i: Nat) -> Bool decreasing k =
        if_then_else(nat_eq(k, 0), true, if_then_else(nat_eq(unwrap_or(nv_get(f, i), 0), unwrap_or(nv_get(f, i + 1), 1)), false, squarefree_loop(pred(k), f, i + 1)))
    // Möbius function as 0, 1 or 2 (= -1)
    fn mobius(n: Nat) -> Nat =
        match nt_factor(n) { none => 0, some(f) => if_then_else(squarefree_loop(nv_len(f), f, 0), if_then_else(nat_eq(mod(nv_len(f), 2), 0), 1, 2), 0) }
    fn distinct_prime_factors(n: Nat) -> NatVec =
        match nt_factor(n) { none => nv_empty(), some(f) => fold(nv_to_list(f), nv_empty(), fun(acc: NatVec, p: Nat, if_then_else(is_some(nv_find(acc, p)), acc, unwrap_or(nv_push(acc, p), acc)))) }
    private fn is_generator(g: Nat, p: Nat, qs: List<Nat>) -> Bool decreasing qs =
        if_then_else(is_empty(qs), true, match head(qs) { none => true, some(q) => if_then_else(nat_eq(unwrap_or(nt_mod_pow(g, div(monus(p, 1), q), p), 1), 1), false, is_generator(g, p, tail(qs))) })
    private fn root_search(k: Nat, g: Nat, p: Nat, qs: List<Nat>) -> Option<Nat> decreasing k =
        if_then_else(nat_eq(k, 0), none(Nat), if_then_else(is_generator(g, p, qs), some(g), root_search(pred(k), g + 1, p, qs)))
    // smallest primitive root modulo a prime p
    fn primitive_root(p: Nat) -> Option<Nat> =
        if_then_else(not(nt_is_prime(p)), none(Nat), if_then_else(nat_eq(p, 2), some(1), root_search(100000, 2, p, nv_to_list(distinct_prime_factors(monus(p, 1))))))
    private fn dlog_loop(k: Nat, g: Nat, h: Nat, p: Nat, x: Nat, acc: Nat) -> Option<Nat> decreasing k =
        if_then_else(nat_eq(k, 0), none(Nat), if_then_else(nat_eq(acc, mod(h, p)), some(x), dlog_loop(pred(k), g, h, p, x + 1, unwrap_or(nt_mod_mul(acc, g, p), 0))))
    // discrete logarithm by exhaustive search up to `limit` exponents
    fn discrete_log(g: Nat, h: Nat, p: Nat, limit: Nat) -> Option<Nat> = if_then_else(nat_eq(p, 0), none(Nat), dlog_loop(limit, g, h, p, 0, mod(1, p)))

    // ---- RSA over two primes ----
    fn rsa_key(p: Nat, q: Nat, e: Nat) -> Option<RsaKey> =
        if_then_else(and(and(nt_is_prime(p), nt_is_prime(q)), not(nat_eq(p, q))),
            match nt_mod_inv(e, mul(monus(p, 1), monus(q, 1))) { none => none(RsaKey), some(d) => some(RsaKey(mul(p, q), e, d)) }, none(RsaKey))
    fn rsa_encrypt(k: RsaKey, m: Nat) -> Option<Nat> = if_then_else(field(k, n) > m, nt_mod_pow(m, field(k, e), field(k, n)), none(Nat))
    fn rsa_decrypt(k: RsaKey, c: Nat) -> Option<Nat> = nt_mod_pow(c, field(k, d), field(k, n))

    // ---- finite fields GF(p^k) ----
    private fn digits(k: Nat, t: Nat, p: Nat, acc: List<Nat>) -> List<Nat> decreasing k =
        if_then_else(nat_eq(k, 0), acc, digits(pred(k), div(t, p), p, cons(mod(t, p), acc)))
    private fn rev_nats(xs: List<Nat>) -> List<Nat> = fold(xs, empty_list(Nat), fun(acc: List<Nat>, x: Nat, cons(x, acc)))
    private fn candidate(k: Nat, t: Nat, p: Nat) -> NatVec = unwrap_or(nv_from_list(rev_nats(cons(1, digits(k, t, p, empty_list(Nat))))), nv_empty())
    private fn irreducible_search(budget: Nat, k: Nat, t: Nat, p: Nat) -> Option<NatVec> decreasing budget =
        if_then_else(nat_eq(budget, 0), none(NatVec), let f = candidate(k, t, p) in
            if_then_else(unwrap_or(pf_irreducible(f, p), false), some(f), irreducible_search(pred(budget), k, t + 1, p)))
    // the first monic irreducible polynomial of degree k over F_p in base-p order of its lower coefficients
    fn irreducible(p: Nat, k: Nat) -> Option<NatVec> = if_then_else(nat_eq(k, 0), none(NatVec), irreducible_search(100000, k, 0, p))
    fn gf_element(coeffs: NatVec, modulus: NatVec, p: Nat) -> Option<Gf> = match pf_rem(coeffs, modulus, p) { none => none(Gf), some(c) => some(Gf(c, modulus, p)) }
    fn gf_from(x: Gf, coeffs: NatVec) -> Option<Gf> = gf_element(coeffs, field(x, modulus), field(x, p))
    fn gf_order(x: Gf) -> Nat = unwrap_or(nt_mod_pow(field(x, p), monus(nv_len(field(x, modulus)), 1), 340282366920938463463374607431768211455), 0)
    private fn gf_same(a: Gf, b: Gf) -> Bool = and(nat_eq(field(a, p), field(b, p)), equal(field(a, modulus), field(b, modulus)))
    fn gf_add(a: Gf, b: Gf) -> Option<Gf> = if_then_else(gf_same(a, b), match pf_add(field(a, coeffs), field(b, coeffs), field(a, p)) { none => none(Gf), some(c) => gf_from(a, c) }, none(Gf))
    fn gf_sub(a: Gf, b: Gf) -> Option<Gf> = if_then_else(gf_same(a, b), match pf_sub(field(a, coeffs), field(b, coeffs), field(a, p)) { none => none(Gf), some(c) => gf_from(a, c) }, none(Gf))
    fn gf_mul(a: Gf, b: Gf) -> Option<Gf> = if_then_else(gf_same(a, b), match pf_mul(field(a, coeffs), field(b, coeffs), field(a, p)) { none => none(Gf), some(c) => gf_from(a, c) }, none(Gf))
    fn gf_pow(a: Gf, e: Nat) -> Option<Gf> = match pf_powmod(field(a, coeffs), e, field(a, modulus), field(a, p)) { none => none(Gf), some(c) => some(Gf(c, field(a, modulus), field(a, p))) }
    fn field_size(a: Gf) -> Nat = gf_order(a)
    fn gf_is_zero(a: Gf) -> Bool = nat_eq(nv_len(field(a, coeffs)), 0)
    // inverse a^(q-2) in a field of q elements
    fn gf_inv(a: Gf) -> Option<Gf> = if_then_else(gf_is_zero(a), none(Gf), gf_pow(a, monus(gf_order(a), 2)))
    fn gf_div(a: Gf, b: Gf) -> Option<Gf> = match gf_inv(b) { none => none(Gf), some(i) => gf_mul(a, i) }

    // ---- permutation groups ----
    fn perm(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn group(n: Nat, gens: List<NatVec>) -> PermGroup = PermGroup(n, fold(gens, nv_empty(), fun(acc: NatVec, g: NatVec, unwrap_or(nv_concat(acc, g), acc))))
    fn group_order(g: PermGroup) -> Option<Nat> = pm_group_order(field(g, gens), field(g, n))
    fn orbit(g: PermGroup, x: Nat) -> Option<NatVec> = pm_orbit(field(g, gens), field(g, n), x)
    fn is_transitive(g: PermGroup) -> Bool = match orbit(g, 0) { none => nat_eq(field(g, n), 0), some(o) => nat_eq(nv_len(o), field(g, n)) }
    private fn shift(n: Nat) -> NatVec = unwrap_or(nv_concat(unwrap_or(nv_range(1, monus(n, 1)), nv_empty()), perm(list(0))), nv_empty())
    private fn swap01(n: Nat) -> NatVec = unwrap_or(nv_concat(perm(list(1, 0)), unwrap_or(nv_range(2, monus(n, 2)), nv_empty())), nv_empty())
    private fn reflection(n: Nat) -> NatVec = nv_reverse(unwrap_or(nv_range(0, n), nv_empty()))
    fn cyclic_group(n: Nat) -> PermGroup = group(n, list(shift(n)))
    fn symmetric_group(n: Nat) -> PermGroup = if_then_else(2 > n, PermGroup(n, nv_empty()), group(n, list(shift(n), swap01(n))))
    fn dihedral_group(n: Nat) -> PermGroup = group(n, list(shift(n), reflection(n)))
    private fn three_cycle(n: Nat, i: Nat) -> NatVec = unwrap_or(nv_set(unwrap_or(nv_set(unwrap_or(nv_set(unwrap_or(nv_range(0, n), nv_empty()), 0, 1), nv_empty()), 1, i), nv_empty()), i, 0), nv_empty())
    private fn alt_gens(k: Nat, n: Nat, i: Nat, acc: List<NatVec>) -> List<NatVec> decreasing k =
        if_then_else(nat_eq(k, 0), acc, alt_gens(pred(k), n, i + 1, cons(three_cycle(n, i), acc)))
    // generated by the 3-cycles (0 1 i)
    fn alternating_group(n: Nat) -> PermGroup = if_then_else(3 > n, PermGroup(n, nv_empty()), group(n, alt_gens(monus(n, 2), n, 2, empty_list(NatVec))))

    theorem fermat_little_theorem_example: nat_eq(unwrap_or(nt_mod_pow(3, 1000000006, 1000000007), 0), 1)
}
