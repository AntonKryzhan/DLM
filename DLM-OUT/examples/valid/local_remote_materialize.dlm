module examples.local_remote_materialize

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let payload = 17
    let job = schedule_on(pool, x86, payload)
    let back = materialize_remote(job)

    print_symbolic(job)
    print_decimal(back)
}
