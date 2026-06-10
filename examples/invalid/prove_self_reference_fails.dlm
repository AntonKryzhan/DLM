module examples.prove_self_reference_fails

theory Meta {
    let p = prop_true()
    let claim = self_reference(p)
    let bad = prove_self_reference(claim)
}
