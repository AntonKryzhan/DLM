module examples.print_decimal_portable_code

theory Cluster {
    let payload = 33
    let code = compile_portable(payload)
    print_decimal(code)
}
