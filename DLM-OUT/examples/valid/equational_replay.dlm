module equational_replay

theory Equations {
    theorem identity(n: Nat): nat_eq(mul(1, n + 0), n) by normalize
    theorem commutative(a: Nat, b: Nat): nat_eq(a + b, b + a) by normalize
    theorem distribute(a: Nat, b: Nat, c: Nat):
        nat_eq(mul(a, b + c), mul(a, b) + mul(a, c)) by normalize

    // This proof keeps the legacy trusted path until definition-unfolding
    // witnesses are supported by the independent replay kernel.
    fn identity_fn(n: Nat) -> Nat = n
    theorem legacy(n: Nat): nat_eq(identity_fn(n), n) by normalize

    print_decimal(mul(1, 5 + 0))
}
