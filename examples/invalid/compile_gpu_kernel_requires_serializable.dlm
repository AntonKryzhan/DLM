module examples.invalid.compile_gpu_kernel_requires_serializable

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let kernel = compile_gpu_kernel(gpool)
}
