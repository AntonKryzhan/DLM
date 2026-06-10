module examples.universe_infinity_requires_universe

theory InfinityMath {
    let u0 = U0()
    let cls = class_of(u0)
    let bad = universe_infinity(cls)
}
