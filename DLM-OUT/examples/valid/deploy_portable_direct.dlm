module examples.deploy_portable_direct

theory Cluster {
    let arm = node_aarch64_with(8, 16384)
    let payload = 44
    let code = compile_portable(payload)
    let job = deploy_portable(arm, code)

    print_symbolic(job)
}
