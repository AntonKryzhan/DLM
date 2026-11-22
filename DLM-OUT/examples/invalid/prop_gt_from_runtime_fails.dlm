module examples.prop_gt_from_runtime_fails

theory Meta {
    let n = read_nat()
    let bad = prop_gt(n, 0)
}
