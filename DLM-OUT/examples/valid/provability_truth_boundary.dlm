module examples.provability_truth_boundary

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let claim = provable_of(checked)
    let truth = truth_from_provable_axiom(claim)

    print_symbolic(claim)
    print_symbolic(truth)
}
