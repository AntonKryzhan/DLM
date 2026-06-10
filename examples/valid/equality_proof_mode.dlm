module examples.equality_proof_mode

theory Core {
    let c = aleph0()
    let c_next = cardinal_succ(c)
    let comparable_by_proof = eq_proof(c, c_next)
}
