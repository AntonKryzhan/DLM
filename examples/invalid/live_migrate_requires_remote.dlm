module examples.live_migrate_requires_remote

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let payload = 7
    let moved = live_migrate(arm, payload)
}
