module examples.proof_gt_from_runtime_fails

theory Kernel {
    let n = read_nat()
    let p = proof_gt(n, 0)
}
