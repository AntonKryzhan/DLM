module examples.print_decimal_gpu_checkpoint

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let snap = checkpoint_gpu_memory(vram)
    print_decimal(snap)
}
