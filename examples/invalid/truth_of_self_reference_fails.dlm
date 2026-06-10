module examples.truth_of_self_reference_fails

theory Meta {
    let p = prop_true()
    let claim = self_reference(p)
    let bad = truth_of_self_reference(claim)
}
