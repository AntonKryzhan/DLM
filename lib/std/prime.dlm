// std.prime — exact scalar arithmetic in prime fields, bounded by p <= 251.
module std.prime

theory StdPrime {
    private fn fp_trial(n: Nat, p: Nat, d: Nat) -> Bool decreasing n =
        if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(mod(p, d), 0), false, fp_trial(pred(n), p, d + 1)))
    fn fp_supported_prime(p: Nat) -> Bool =
        if_then_else(or(not(p > 1), p > 251), false, fp_trial(monus(div(p, 2), 1), p, 2))
    fn fp_prime_test(p: Nat) -> Result<Bool, Text> =
        if_then_else(p > 251, err("primality test bound exceeded", Bool), ok(fp_supported_prime(p), Text))
    private fn fp_pair(p: Nat, a: Nat, b: Nat) -> Bool = and(p > a, p > b)
    private fn fp_power(n: Nat, p: Nat, a: Nat, acc: Nat) -> Nat decreasing n fuel 2048 =
        if_then_else(nat_eq(n, 0), acc, fp_power(pred(n), p, a, mod(mul(acc, a), p)))
    fn fp_add(p: Nat, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat), if_then_else(fp_pair(p, a, b), ok(mod(a + b, p), Text), err("field element out of range", Nat)))
    fn fp_sub(p: Nat, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat), if_then_else(fp_pair(p, a, b), ok(mod(monus(a + p, b), p), Text), err("field element out of range", Nat)))
    fn fp_mul(p: Nat, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat), if_then_else(fp_pair(p, a, b), ok(mod(mul(a, b), p), Text), err("field element out of range", Nat)))
    fn fp_neg(p: Nat, a: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat), if_then_else(p > a, ok(if_then_else(nat_eq(a, 0), 0, monus(p, a)), Text), err("field element out of range", Nat)))
    fn fp_pow(p: Nat, a: Nat, exponent: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat),
            if_then_else(not(p > a), err("field element out of range", Nat), if_then_else(exponent > 1024, err("field power exponent budget exceeded", Nat), ok(fp_power(exponent, p, a, 1), Text))))
    fn fp_inv(p: Nat, a: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat),
            if_then_else(not(p > a), err("field element out of range", Nat), if_then_else(nat_eq(a, 0), err("zero has no multiplicative inverse", Nat), ok(fp_power(monus(p, 2), p, a, 1), Text))))
    fn fp_div(p: Nat, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(fp_supported_prime(p)), err("unsupported prime modulus", Nat),
            if_then_else(not(fp_pair(p, a, b)), err("field element out of range", Nat), if_then_else(nat_eq(b, 0), err("zero has no multiplicative inverse", Nat), ok(mod(mul(a, fp_power(monus(p, 2), p, b, 1)), p), Text))))

    theorem prime_three: fp_supported_prime(3)
    theorem four_is_composite: not(fp_supported_prime(4))
    theorem inverse_mod_seven: nat_eq(unwrap_or(fp_inv(7, 3), 999), 5)
}
