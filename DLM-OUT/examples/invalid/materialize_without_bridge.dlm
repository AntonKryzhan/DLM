module examples.materialize_without_bridge

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 21
}

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(arm)
    let job = schedule_on(pool, arm, Local.payload)
}

theory Return {
    let back = materialize_remote(Cluster.job)
}
