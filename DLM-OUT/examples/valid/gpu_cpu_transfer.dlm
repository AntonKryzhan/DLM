module examples.gpu_cpu_transfer

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)

    let payload = 55
    let on_gpu = copy_to_gpu(payload, vram)
    let back = copy_from_gpu(on_gpu)

    print_symbolic(on_gpu)
    print_decimal(back)
}
