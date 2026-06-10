module examples.checkpoint_requires_memory

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let pool = virtual_pool(x86)
    let snap = checkpoint_memory(pool)
}
