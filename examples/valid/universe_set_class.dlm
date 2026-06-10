module examples.universe_set_class

theory Foundations {
    let u0 = U0()
    let u1 = universe_succ(u0)
    let s0 = set_of(u0)
    let c0 = class_of(u0)
    let lives = set_lives_in(s0)
    let level = class_level(c0)

    print_symbolic(u0)
    print_symbolic(u1)
    print_symbolic(s0)
    print_symbolic(c0)
    print_decimal(lives)
    print_decimal(level)
}
