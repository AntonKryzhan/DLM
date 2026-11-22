module examples.checkpoint_gpu_requires_gpu_memory

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let snap = checkpoint_gpu_memory(gpool)
}
