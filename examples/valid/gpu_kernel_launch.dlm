module examples.gpu_kernel_launch

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    let result = launch_kernel(vram, kernel)
    let back = copy_from_gpu(result)

    print_symbolic(kernel)
    print_symbolic(result)
    print_decimal(back)
}
