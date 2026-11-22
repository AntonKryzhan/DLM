module examples.lambda_must_be_pure

theory T {
    let shout = fun(n: Nat, print_decimal(n))
}
