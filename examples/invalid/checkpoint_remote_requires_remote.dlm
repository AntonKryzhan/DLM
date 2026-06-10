module examples.checkpoint_remote_requires_remote

theory Cluster {
    let payload = 7
    let snap = checkpoint_remote(payload)
}
