module examples.reflection_summary_axiom

theory Core {
  let phi = prop_true()
  let s = self_reference(phi)
  let accepted = self_reference_axiom(s)
}
