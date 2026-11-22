module examples.schedule_target_requires_node

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let not_node = 1
    let value = 9
    let job = schedule_on(pool, not_node, value)
}
