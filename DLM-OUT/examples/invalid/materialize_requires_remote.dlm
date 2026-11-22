module examples.materialize_requires_remote

theory Core {
    let n = 7
    let back = materialize_remote(n)
}
