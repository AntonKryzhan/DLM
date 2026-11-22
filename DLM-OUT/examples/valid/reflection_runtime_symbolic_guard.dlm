module examples.reflection_runtime_symbolic_guard

theory Core {
  let phi = prop_true()
  let self = self_reference(phi)
  print_symbolic(self)
}
