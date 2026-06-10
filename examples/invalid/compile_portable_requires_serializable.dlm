module examples.compile_portable_requires_serializable

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let pool = virtual_pool(x86)
    let code = compile_portable(pool)
}
