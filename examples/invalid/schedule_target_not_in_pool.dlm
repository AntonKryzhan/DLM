module examples.schedule_target_not_in_pool

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86)
    let value = 9
    let job = schedule_on(pool, arm, value)
    print_symbolic(job)
}
