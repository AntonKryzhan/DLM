module examples.big_number_hierarchy

theory Growth {
    let g = Graham()
    let t = TREE(3)
    let bb = BB(1000)
    let f = fast_growing(5)
    let t_param = growth_parameter(t)

    print_symbolic(g)
    print_symbolic(t)
    print_symbolic(bb)
    print_symbolic(f)
    print_decimal(t_param)
}
