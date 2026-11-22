module examples.deploy_on_requires_cluster

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let payload = 33
    let code = compile_portable(payload)
    let job = deploy_on(x86, x86, code)
}
