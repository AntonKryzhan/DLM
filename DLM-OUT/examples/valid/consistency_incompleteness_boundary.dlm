module examples.consistency_incompleteness_boundary

theory Meta {
    let c = consistency_claim()
    let p = assume_consistency(c)

    print_symbolic(c)
    print_symbolic(p)
}
