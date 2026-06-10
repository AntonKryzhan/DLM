module examples.invalid.launch_kernel_requires_gpu_memory

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let ram = distributed_memory(pool, 4096)
    let payload = 77
    let kernel = compile_gpu_kernel(payload)
    let result = launch_kernel(ram, kernel)
}
