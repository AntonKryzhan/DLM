module examples.restore_remote_requires_checkpoint

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let payload = 7
    let restored = restore_remote(x86, payload)
}
