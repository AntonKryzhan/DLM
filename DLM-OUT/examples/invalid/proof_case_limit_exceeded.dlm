module examples.proof_case_limit_exceeded

theory T {
    theorem assoc(a: Nat, b: Nat, c: Nat) below 100: nat_eq(a + b + c, a + c + b)
}
