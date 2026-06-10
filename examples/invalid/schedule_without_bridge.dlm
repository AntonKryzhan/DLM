module examples.schedule_without_bridge

theory Local {
    let payload = 9
}

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(arm)
    let job = schedule_on(pool, arm, Local.payload)
}
