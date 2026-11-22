module examples.gpu_memory_mib_requires_gpu_region

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let cap = gpu_memory_mib(gpu0)
}
