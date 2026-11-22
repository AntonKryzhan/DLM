module examples.invalid.launch_kernel_requires_kernel

theory Cluster {
    let gpu0 = gpu_cuda_with(8192)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 4096)
    let payload = 77
    let result = launch_kernel(vram, payload)
}
