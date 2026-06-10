module examples.copy_to_gpu_requires_gpu_memory

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let ram = distributed_memory(pool, 4096)
    let payload = 55
    let on_gpu = copy_to_gpu(payload, ram)
}
