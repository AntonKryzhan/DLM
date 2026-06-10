module examples.soundness_summary_clean

theory Kernel {
    let truth = proof_true()
    let checked = check_proof(truth)
}
