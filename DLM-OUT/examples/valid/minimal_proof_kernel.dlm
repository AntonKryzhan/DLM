module examples.minimal_proof_kernel

theory Kernel {
    let truth = proof_true()
    let truth_checked = check_proof(truth)

    let a = 7
    let b = 3
    let gt = proof_gt(a, b)
    let gt_checked = check_proof(gt)

    print_symbolic(truth)
    print_symbolic(truth_checked)
    print_symbolic(gt)
    print_symbolic(gt_checked)
}
