module examples.self_reference_axiom_rejected_by_trusted_only

theory Meta {
    let claim = godel_sentence()
    let assumption = self_reference_axiom(claim)
}
