module examples.virtual_cluster_pool

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let arm = node_aarch64_with(16, 65536)
    let pool = virtual_pool(x86, arm)
    let cores = pool_cores(pool)
    let memory = pool_memory_mib(pool)

    print_decimal(cores)
    print_decimal(memory)
    print_symbolic(pool)
}
