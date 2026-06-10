module examples.remote_materialize

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

bridge Cluster_to_Return : Cluster -> Return {
    kind = materialize
}

theory Local {
    let payload = 21
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let job = schedule_on(pool, arm, Local.payload)
    print_symbolic(job)
}

theory Return {
    let back = materialize_remote(Cluster.job)
    print_decimal(back)
}
