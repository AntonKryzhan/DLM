module examples.static_proof_from_runtime

theory Core {
    let n = read_nat()
    let p = prove(n > 0)
}
