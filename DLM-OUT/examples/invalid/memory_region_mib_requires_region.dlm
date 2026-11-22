module examples.memory_region_mib_requires_region

theory Cluster {
    let x86 = node_x86_64_with(8, 32768)
    let cap = memory_region_mib(x86)
}
