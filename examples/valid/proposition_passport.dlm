module examples.proposition_passport

theory Logic {
    let truth = prop_true()
    let a = 7
    let b = 3
    let gt = prop_gt(a, b)

    print_symbolic(truth)
    print_symbolic(gt)
}
