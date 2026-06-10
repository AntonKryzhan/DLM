module examples.distributed_memory_zero

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let mem = distributed_memory(pool, 0)
}
