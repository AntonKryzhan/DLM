module examples.run_overflow

theory Core {
    let a = 340282366920938463463374607431768211455
    let b = 1
    let c = a + b

    print_decimal(c)
}
