module examples.gpu_memory_requires_gpu_pool

theory Cluster {
    let cpu = node_x86_64_with(8, 32768)
    let mem = distributed_gpu_memory(cpu, 1024)
}
