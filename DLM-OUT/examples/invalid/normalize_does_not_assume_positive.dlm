module examples.normalize_does_not_assume_positive

theory T {
    theorem pred_cancels(n: Nat): nat_eq(pred(n) + 1, n) by normalize
}
