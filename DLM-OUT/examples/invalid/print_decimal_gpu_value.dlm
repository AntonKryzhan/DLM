module examples.print_decimal_gpu_value

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let payload = 55
    let on_gpu = copy_to_gpu(payload, vram)
    print_decimal(on_gpu)
}
