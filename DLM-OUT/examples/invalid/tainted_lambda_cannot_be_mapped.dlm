module examples.tainted_lambda_cannot_be_mapped

theory T {
    let tainted = fun(n: Nat, n + axiom_nat())
    let ys = map(list(1, 2), tainted)
}
