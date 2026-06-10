module examples.gpu_memory_exceeds_pool

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 16384)
}
