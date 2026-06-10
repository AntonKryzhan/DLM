module examples.schedule_requires_cluster

theory Cluster {
    let node = node_x86_64_with(4, 8192)
    let value = 9
    let job = schedule_on(node, node, value)
}
