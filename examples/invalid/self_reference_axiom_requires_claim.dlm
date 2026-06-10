module examples.self_reference_axiom_requires_claim

theory Meta {
    let p = prop_true()
    let bad = self_reference_axiom(p)
}
