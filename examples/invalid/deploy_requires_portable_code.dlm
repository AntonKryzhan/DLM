module examples.deploy_requires_portable_code

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let payload = 33
    let job = deploy_portable(arm, payload)
}
