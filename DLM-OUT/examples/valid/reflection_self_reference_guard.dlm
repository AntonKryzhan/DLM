module examples.reflection_self_reference_guard


bridge Meta_reflection : Meta -> Meta {
    kind = reflection
}

theory Meta {
    let term = proof_true()
    let checked = check_proof(term)
    let provable = provable_of(checked)

    let r_claim = reflection_claim(provable)
    let r_axiom = reflection_axiom(r_claim)

    let p = prop_true()
    let s_claim = self_reference(p)
    let s_axiom = self_reference_axiom(s_claim)

    let g = godel_sentence()
    let g_axiom = self_reference_axiom(g)

    print_symbolic(r_claim)
    print_symbolic(r_axiom)
    print_symbolic(s_claim)
    print_symbolic(s_axiom)
    print_symbolic(g)
    print_symbolic(g_axiom)
}
