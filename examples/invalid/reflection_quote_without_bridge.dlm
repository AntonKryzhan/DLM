module examples.reflection_quote_without_bridge

theory Core {
  let proof = prove(7 > 0)
  let p = provable_of(proof)
  let reflected = reflection_claim(p)
}
