module examples.restore_gpu_requires_checkpoint

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let restored = restore_gpu_memory(vram)
}
