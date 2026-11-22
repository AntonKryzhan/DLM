module examples.copy_from_gpu_requires_gpu_value

theory Cluster {
    let payload = 55
    let back = copy_from_gpu(payload)
}
