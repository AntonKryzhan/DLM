module examples.reflection_claim_requires_bridge

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let provable = provable_of(checked)
    let bad = reflection_claim(provable)
}
