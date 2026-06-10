module examples.reflection_axiom_rejected_by_trusted_only


bridge Meta_reflection : Meta -> Meta {
    kind = reflection
}

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let provable = provable_of(checked)
    let claim = reflection_claim(provable)
    let assumption = reflection_axiom(claim)
}
