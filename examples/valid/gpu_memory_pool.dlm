module examples.gpu_memory_pool

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpu1 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0, gpu1)
    let vram = distributed_gpu_memory(gpool, 32768)
    let cap = gpu_memory_mib(vram)

    print_decimal(cap)
    print_symbolic(gpool)
    print_symbolic(vram)
}
