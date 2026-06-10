module examples.portable_code_deploy

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)
    let payload = 33

    let code = compile_portable(payload)
    let job = deploy_on(pool, arm, code)

    print_symbolic(code)
    print_symbolic(job)
}
