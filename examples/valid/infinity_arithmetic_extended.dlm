module examples.infinity_arithmetic_extended

theory InfinityMath {
    let c = aleph0()
    let c_next = cardinal_succ(c)
    let c_sum = cardinal_add(c, c_next)

    let o = omega()
    let o_next = ordinal_succ(o)
    let o_sum = ordinal_add(o, o_next)

    let lim = limit_omega()
    let pot = potential_infinity()
    let pot_next = potential_step(pot)

    let u0 = U0()
    let cls = class_of(u0)
    let class_inf = class_infinity(cls)
    let universe_inf = universe_infinity(u0)

    print_symbolic(c_sum)
    print_symbolic(o_sum)
    print_symbolic(lim)
    print_symbolic(pot_next)
    print_symbolic(class_inf)
    print_symbolic(universe_inf)
}
