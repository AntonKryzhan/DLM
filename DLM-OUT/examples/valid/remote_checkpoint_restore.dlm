module examples.remote_checkpoint_restore

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 11
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)

    let job = schedule_on(pool, arm, Local.payload)
    let snap = checkpoint_remote(job)
    let restored = restore_remote(x86, snap)

    print_symbolic(snap)
    print_symbolic(restored)
}
