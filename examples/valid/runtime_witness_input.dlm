module examples.runtime_witness_input

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
