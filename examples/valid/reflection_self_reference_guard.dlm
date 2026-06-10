module examples.reflection_self_reference_guard

bridge Core_reflection : Core -> Core {
  kind = reflection
}

theory Core {
  let proof = prove(7 > 0)
  let p = provable_of(proof)
  let reflected = reflection_claim(p)
  let accepted = reflection_axiom(reflected)
}
