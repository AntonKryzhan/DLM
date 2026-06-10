module examples.set_of_requires_universe

theory Foundations {
    let u0 = U0()
    let s0 = set_of(u0)
    let bad = set_of(s0)
}
