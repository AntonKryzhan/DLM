module examples.distributed_memory_region

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let arm = node_aarch64_with(16, 65536)
    let pool = virtual_pool(x86, arm)

    let mem = distributed_memory(pool, 49152)
    let cap = memory_region_mib(mem)

    print_decimal(cap)
    print_symbolic(mem)
}
