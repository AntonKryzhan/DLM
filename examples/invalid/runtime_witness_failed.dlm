module examples.runtime_witness_failed

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
