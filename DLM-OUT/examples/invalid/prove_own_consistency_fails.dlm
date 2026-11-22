module examples.prove_own_consistency_fails

theory Meta {
    let c = consistency_claim()
    let p = prove_consistency(c)
}
