module examples.finite_algebra
import std.finite_algebra

theory AlgebraDemo extends StdFiniteAlgebra {
    let invalid_group = FiniteGroup(0, 0, empty_list(Nat))
    let c4 = unwrap_or(group_cyclic(4), invalid_group)
    let c2 = unwrap_or(group_cyclic(2), invalid_group)
    let projection = list(0, 1, 0, 1)
    print_symbolic(group_mul(c4, 3, 3))
    print_symbolic(group_inverse(c4, 3))
    print_symbolic(group_kernel(c4, c2, projection))
    print_symbolic(group_quotient(c4, list(0, 2)))
    print_symbolic(is_ok(group_first_isomorphism(c4, c2, projection)))

    let invalid_ring = FiniteRing(0, 0, 0, empty_list(Nat), empty_list(Nat))
    let z4 = unwrap_or(ring_residues(4), invalid_ring)
    let f2 = unwrap_or(ring_residues(2), invalid_ring)
    print_symbolic(ring_is_field(z4))
    print_symbolic(ring_is_field(f2))
    print_symbolic(ring_homomorphism(z4, f2, projection))
    let regular = unwrap_or(module_regular(z4), FiniteModule(0, 0, empty_list(Nat), empty_list(Nat)))
    print_symbolic(module_scale(z4, regular, 2, 3))
    print_symbolic(fp_mul(7, 5, 3))
    print_symbolic(fp_inv(7, 3))
    print_symbolic(fp_inv(7, 0))
}
