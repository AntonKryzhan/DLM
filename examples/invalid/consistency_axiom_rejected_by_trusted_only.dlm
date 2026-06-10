module examples.consistency_axiom_rejected_by_trusted_only

theory Meta {
    let c = consistency_claim()
    let p = assume_consistency(c)
}
