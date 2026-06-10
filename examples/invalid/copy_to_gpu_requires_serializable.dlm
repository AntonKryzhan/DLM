module examples.copy_to_gpu_requires_serializable

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let bad = copy_to_gpu(gpool, vram)
}
