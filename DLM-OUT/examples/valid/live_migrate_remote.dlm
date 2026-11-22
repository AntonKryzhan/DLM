module examples.live_migrate_remote

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let payload = 13

    let job = schedule_on(pool, x86, payload)
    let moved = live_migrate(arm, job)

    print_symbolic(job)
    print_symbolic(moved)
}
