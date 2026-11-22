module examples.truth_axiom_rejected_by_trusted_only

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let claim = provable_of(checked)
    let truth = truth_from_provable_axiom(claim)
}
