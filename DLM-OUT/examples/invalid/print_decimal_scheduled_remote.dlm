module examples.print_decimal_scheduled_remote

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let value = 9
    let job = schedule_on(pool, x86, value)
    print_decimal(job)
}
