module examples.int_positivity_unproved

// The kernel decides Int comparisons only on constant differences: `a + a > a`
// is false for a <= 0 and must be rejected.
theory T {
    theorem doubling(a: Int): a + a > a by normalize
}
