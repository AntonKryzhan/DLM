module examples.print_decimal_remote_checkpoint

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let payload = 7
    let job = schedule_on(pool, x86, payload)
    let snap = checkpoint_remote(job)
    print_decimal(snap)
}
