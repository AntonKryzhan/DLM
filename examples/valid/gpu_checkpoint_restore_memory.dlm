module examples.gpu_checkpoint_restore_memory

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)
    let snap = checkpoint_gpu_memory(vram)
    let restored = restore_gpu_memory(snap)
    let cap = gpu_memory_mib(restored)

    print_decimal(cap)
    print_symbolic(snap)
    print_symbolic(restored)
}
