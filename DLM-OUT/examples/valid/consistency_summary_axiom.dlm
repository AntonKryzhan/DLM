module examples.consistency_summary_axiom

theory Meta {
    let c = consistency_claim()
    let p = consistency_axiom(c)
}
