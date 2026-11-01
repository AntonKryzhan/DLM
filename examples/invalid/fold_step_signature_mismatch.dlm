module examples.fold_step_signature_mismatch

theory T {
    fn double(n: Nat) -> Nat = n + n
    let total = fold(list(1, 2), 0, double)
}
