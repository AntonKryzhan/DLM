module examples.pool_cores_requires_cluster

theory Cluster {
    let node = node_x86_64_with(8, 32768)
    let cores = pool_cores(node)
}
