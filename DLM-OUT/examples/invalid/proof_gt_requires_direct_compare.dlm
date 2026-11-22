module examples.proof_gt_requires_direct_compare

theory Kernel {
    let big = TREE(3)
    let p = proof_gt(big, 0)
}
