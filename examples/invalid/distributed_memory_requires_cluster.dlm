module examples.distributed_memory_requires_cluster

theory Cluster {
    let node = node_x86_64_with(8, 32768)
    let mem = distributed_memory(node, 1024)
}
