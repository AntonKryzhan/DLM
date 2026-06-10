module examples.invalid.print_decimal_gpu_kernel

theory Cluster {
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    print_decimal(kernel)
}
