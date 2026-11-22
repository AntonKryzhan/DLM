module examples.reflection_axiom_requires_claim

theory Meta {
    let p = prop_true()
    let bad = reflection_axiom(p)
}
