module examples.option_rejects_static_proof

theory T {
    let proof = prove(7 > 0)
    let boxed = some(proof)
}
