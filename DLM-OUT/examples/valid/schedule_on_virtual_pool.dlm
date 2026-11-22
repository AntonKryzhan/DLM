module examples.schedule_on_virtual_pool

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 9
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)

    let job = schedule_on(pool, arm, Local.payload)
    print_symbolic(job)
}
