// Cryptographic-size integers (v0.124, docs/BIGNUM.md): modular powers and inverses on BigNat,
// Baillie–PSW primality, deterministic random primes, RSA with BigNat keys, Mersenne numbers
// with the Lucas–Lehmer test, hex text. No constant-time or side-channel guarantees.
module std.bigprime
import std.spatial

theory StdBigPrime extends StdSpatial {
    record BigRsa(modulus: BigNat, enc: BigNat, dec: BigNat)

    private fn bp_one() -> BigNat = bn_from_nat(1)
    private fn bp_lt(a: BigNat, b: BigNat) -> Bool = bn_bits(bn_monus(b, a)) > 0
    fn big_prime(t: Text) -> Bool = match bn_from_text(t) { none => false, some(x) => bn_is_prime(x) }
    // the first prime above a deterministic `bits`-bit number from `seed`
    fn random_prime(seed: Nat, bits: Nat) -> Option<BigNat> = match bn_random(seed, bits) { none => none(BigNat), some(x) => bn_next_prime(x) }
    fn big_rsa_key(p: BigNat, q: BigNat, e: BigNat) -> Option<BigRsa> =
        if_then_else(and(and(bn_is_prime(p), bn_is_prime(q)), not(nat_eq(bn_bits(bn_monus(p, q)) + bn_bits(bn_monus(q, p)), 0))),
            match bn_mul(bn_monus(p, bp_one()), bn_monus(q, bp_one())) { none => none(BigRsa), some(phi) =>
                match bn_modinv(e, phi) { none => none(BigRsa), some(d) =>
                    match bn_mul(p, q) { none => none(BigRsa), some(n) => some(BigRsa(n, e, d)) } } },
            none(BigRsa))
    fn big_rsa_encrypt(k: BigRsa, m: BigNat) -> Option<BigNat> = if_then_else(bp_lt(m, field(k, modulus)), bn_powmod(m, field(k, enc), field(k, modulus)), none(BigNat))
    fn big_rsa_decrypt(k: BigRsa, c: BigNat) -> Option<BigNat> = bn_powmod(c, field(k, dec), field(k, modulus))
    fn big_rsa_sign(k: BigRsa, h: BigNat) -> Option<BigNat> = if_then_else(bp_lt(h, field(k, modulus)), bn_powmod(h, field(k, dec), field(k, modulus)), none(BigNat))
    fn big_rsa_verify(k: BigRsa, h: BigNat, s: BigNat) -> Bool = match bn_powmod(s, field(k, enc), field(k, modulus)) { none => false, some(v) => nat_eq(bn_bits(bn_monus(v, h)) + bn_bits(bn_monus(h, v)), 0) }
    // 2^p - 1
    fn mersenne(p: Nat) -> Option<BigNat> = match bn_shl(bp_one(), p) { none => none(BigNat), some(x) => some(bn_monus(x, bp_one())) }
    private fn ll_step(s: BigNat, m: BigNat) -> BigNat =
        let sq = unwrap_or(bn_mul(s, s), s) in unwrap_or(bn_mod(bn_monus(unwrap_or(bn_add(sq, m), sq), bn_from_nat(2)), m), s)
    private fn ll_loop(s: BigNat, m: BigNat, k: Nat) -> BigNat decreasing k = if_then_else(nat_eq(k, 0), s, ll_loop(ll_step(s, m), m, pred(k)))
    // Lucas–Lehmer: 2^p - 1 is prime (p odd prime; p = 2 handled directly)
    fn lucas_lehmer(p: Nat) -> Bool =
        if_then_else(nat_eq(p, 2), true, if_then_else(not(nt_is_prime(p)), false,
            match mersenne(p) { none => false, some(m) => nat_eq(bn_bits(ll_loop(bn_from_nat(4), m, pred(pred(p)))), 0) }))
    fn big_crt(a: BigNat, m: BigNat, b: BigNat, n: BigNat) -> Option<BigNat> =
        match bn_modinv(m, n) { none => none(BigNat), some(mi) =>
            match bn_mul(m, n) { none => none(BigNat), some(mn) =>
                let diff = unwrap_or(bn_mod(bn_monus(unwrap_or(bn_add(b, n), b), unwrap_or(bn_mod(a, n), a)), n), b) in
                match bn_mod(unwrap_or(bn_mul(diff, mi), diff), n) { none => none(BigNat), some(t) =>
                    match bn_mul(t, m) { none => none(BigNat), some(tm) => match bn_add(a, tm) { none => none(BigNat), some(x) => bn_mod(x, mn) } } } } }

    theorem mersenne_127_is_prime: bn_is_prime(unwrap_or(mersenne(127), bn_from_nat(0)))
}
