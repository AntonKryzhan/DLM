module examples.inequality_induction_fails

theory T {
    theorem positive(n: Nat): n > 0 by induction n
}
