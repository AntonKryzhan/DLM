module examples.truth_from_provable_without_soundness

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let claim = provable_of(checked)
    let bad = truth_from_provable(claim)
}
