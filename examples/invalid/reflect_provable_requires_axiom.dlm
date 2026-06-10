module examples.reflect_provable_requires_axiom

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let provable = provable_of(checked)
    let bad = reflect_provable(provable)
}
