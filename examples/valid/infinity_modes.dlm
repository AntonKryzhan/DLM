module examples.infinity_modes

bridge Core_quote : Core -> Meta {
    kind = quote
}

theory Core {
    let c = aleph0()
    let c_next = cardinal_succ(c)
    let o = omega()
    let o_next = ordinal_succ(o)

    print_symbolic(c_next)
    print_symbolic(o_next)
}
