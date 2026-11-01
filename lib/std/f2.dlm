// std.f2 — exact F2 represented by Bool; no integer overflow or invalid residue.
module std.f2

theory StdF2 {
    fn f2_add(a: Bool, b: Bool) -> Bool = if_then_else(a, not(b), b)
    fn f2_mul(a: Bool, b: Bool) -> Bool = and(a, b)
    fn f2_neg(a: Bool) -> Bool = a
    fn f2_inv(a: Bool) -> Option<Bool> = if_then_else(a, some(true), none(Bool))
    theorem f2_self_cancel(a: Bool): not(f2_add(a, a)) by normalize
    theorem f2_add_commutes(a: Bool, b: Bool): not(f2_add(f2_add(a, b), f2_add(b, a))) by normalize
    theorem f2_mul_unit(a: Bool): not(f2_add(f2_mul(a, true), a)) by normalize
    theorem f2_distributes(a: Bool, b: Bool, c: Bool): not(f2_add(f2_mul(a, f2_add(b, c)), f2_add(f2_mul(a, b), f2_mul(a, c)))) by normalize
}
