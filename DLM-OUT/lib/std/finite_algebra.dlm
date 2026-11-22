// std.finite_algebra — ordinary DLM reference algorithms, order <= 8.
// Raw records are untrusted descriptions; every public operation revalidates.
module std.finite_algebra
import std.prime

theory StdFiniteAlgebra extends StdPrime {
    record FiniteGroup(order: Nat, unit: Nat, operation: List<Nat>)
    record FiniteRing(order: Nat, zero: Nat, one: Nat, addition: List<Nat>, multiplication: List<Nat>)
    record FiniteModule(order: Nat, zero: Nat, addition: List<Nat>, action: List<Nat>)
    record FiniteSubgroup(group: FiniteGroup, embedding: List<Nat>)
    record FiniteQuotient(group: FiniteGroup, projection: List<Nat>)

    record FiniteGroupIsomorphism(quotient: FiniteQuotient, image: FiniteSubgroup, induced: List<Nat>)

    private fn fa_size(n: Nat) -> Bool = and(n > 0, not(n > 8))
    private fn fa_all(n: Nat, f: Fn<Nat, Bool>) -> Bool decreasing n =
        if_then_else(nat_eq(n, 0), true, and(f(pred(n)), fa_all(pred(n), f)))
    private fn fa_values(n: Nat, i: Nat, f: Fn<Nat, Nat>) -> List<Nat> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), empty_list(Nat), cons(f(i), fa_values(pred(n), i + 1, f)))
    private fn fa_select(n: Nat, i: Nat, f: Fn<Nat, Bool>) -> List<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(Nat), if_then_else(f(i), cons(i, fa_select(pred(n), i + 1, f)), fa_select(pred(n), i + 1, f)))
    private fn fa_table(n: Nat, t: List<Nat>) -> Bool =
        and(nat_eq(length(t), mul(n, n)), fold(t, true, fun(acc: Bool, x: Nat, and(acc, n > x))))
    private fn fa_get(t: List<Nat>, n: Nat, a: Nat, b: Nat) -> Nat = unwrap_or(index(t, mul(a, n) + b), n)
    private fn fa_at(xs: List<Nat>, i: Nat) -> Nat = unwrap_or(index(xs, i), 999)
    private fn fa_has(xs: List<Nat>, a: Nat) -> Bool = fold(xs, false, fun(acc: Bool, x: Nat, or(acc, nat_eq(x, a))))
    private fn fa_position(xs: List<Nat>, x: Nat, i: Nat) -> Nat decreasing xs =
        if_then_else(is_empty(xs), 999, if_then_else(nat_eq(unwrap_or(head(xs), 999), x), i, fa_position(tail(xs), x, i + 1)))
    private fn fa_distinct(xs: List<Nat>) -> Bool decreasing xs =
        if_then_else(is_empty(xs), true, and(not(fa_has(tail(xs), unwrap_or(head(xs), 999))), fa_distinct(tail(xs))))
    private fn fa_op(g: FiniteGroup, a: Nat, b: Nat) -> Nat = fa_get(field(g, operation), field(g, order), a, b)
    private fn fa_inverse(n: Nat, g: FiniteGroup, a: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), 999,
            if_then_else(and(nat_eq(fa_op(g, a, pred(n)), field(g, unit)), nat_eq(fa_op(g, pred(n), a), field(g, unit))), pred(n), fa_inverse(pred(n), g, a)))
    private fn fa_group_laws(g: FiniteGroup) -> Bool =
        let n = field(g, order) in let e = field(g, unit) in
        fa_all(n, fun(a: Nat,
            and(and(nat_eq(fa_op(g, e, a), a), nat_eq(fa_op(g, a, e), a)),
                and(n > fa_inverse(n, g, a), fa_all(n, fun(b: Nat, fa_all(n, fun(c: Nat, nat_eq(fa_op(g, fa_op(g, a, b), c), fa_op(g, a, fa_op(g, b, c)))))))))))

    fn group_valid(g: FiniteGroup) -> Bool =
        if_then_else(not(fa_size(field(g, order))), false,
            if_then_else(not(field(g, order) > field(g, unit)), false,
                if_then_else(fa_table(field(g, order), field(g, operation)), fa_group_laws(g), false)))
    fn group_check(g: FiniteGroup) -> Result<FiniteGroup, Text> =
        if_then_else(group_valid(g), ok(g, Text), err("invalid finite group or order budget", FiniteGroup))
    fn group_cyclic(n: Nat) -> Result<FiniteGroup, Text> =
        if_then_else(not(fa_size(n)), err("finite algebra order must be 1..=8", FiniteGroup),
            group_check(FiniteGroup(n, 0, fa_values(mul(n, n), 0, fun(i: Nat, mod(div(i, n) + mod(i, n), n))))))
    fn group_mul(g: FiniteGroup, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(group_valid(g)), err("invalid finite group", Nat),
            if_then_else(and(field(g, order) > a, field(g, order) > b), ok(fa_op(g, a, b), Text), err("group element out of range", Nat)))
    fn group_inverse(g: FiniteGroup, a: Nat) -> Result<Nat, Text> =
        if_then_else(not(group_valid(g)), err("invalid finite group", Nat), if_then_else(field(g, order) > a, ok(fa_inverse(field(g, order), g, a), Text), err("group element out of range", Nat)))
    fn group_abelian(g: FiniteGroup) -> Bool =
        if_then_else(not(group_valid(g)), false, fa_all(field(g, order), fun(a: Nat, fa_all(field(g, order), fun(b: Nat, nat_eq(fa_op(g, a, b), fa_op(g, b, a)))))))
    private fn fa_group_map(g: FiniteGroup, h: FiniteGroup, mapping: List<Nat>) -> Bool =
        and(nat_eq(fa_at(mapping, field(g, unit)), field(h, unit)),
            fa_all(field(g, order), fun(a: Nat, fa_all(field(g, order), fun(b: Nat, nat_eq(fa_at(mapping, fa_op(g, a, b)), fa_op(h, fa_at(mapping, a), fa_at(mapping, b))))))))
    fn group_homomorphism(g: FiniteGroup, h: FiniteGroup, mapping: List<Nat>) -> Bool =
        if_then_else(not(and(group_valid(g), group_valid(h))), false,
            if_then_else(not(nat_eq(length(mapping), field(g, order))), false,
                if_then_else(fold(mapping, true, fun(acc: Bool, x: Nat, and(acc, field(h, order) > x))), fa_group_map(g, h, mapping), false)))

    fn group_kernel(g: FiniteGroup, h: FiniteGroup, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(group_homomorphism(g, h, mapping)), err("invalid group homomorphism", List(Nat)),
            ok(fa_select(field(g, order), 0, fun(x: Nat, nat_eq(fa_at(mapping, x), field(h, unit)))), Text))
    fn group_image(g: FiniteGroup, h: FiniteGroup, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(group_homomorphism(g, h, mapping)), err("invalid group homomorphism", List(Nat)),
            ok(fa_select(field(h, order), 0, fun(x: Nat, fa_has(mapping, x))), Text))

    private fn fa_subgroup_members(g: FiniteGroup, members: List<Nat>) -> Bool =
        let n = field(g, order) in let k = length(members) in
        and(fa_has(members, field(g, unit)), fa_all(k, fun(i: Nat,
            and(fa_has(members, fa_inverse(n, g, fa_at(members, i))), fa_all(k, fun(j: Nat, fa_has(members, fa_op(g, fa_at(members, i), fa_at(members, j)))))))))
    private fn fa_subgroup_valid(g: FiniteGroup, members: List<Nat>) -> Bool =
        if_then_else(not(group_valid(g)), false,
            if_then_else(or(is_empty(members), length(members) > field(g, order)), false,
                if_then_else(and(fa_distinct(members), fold(members, true, fun(acc: Bool, x: Nat, and(acc, field(g, order) > x)))), fa_subgroup_members(g, members), false)))
    fn group_subgroup(g: FiniteGroup, members: List<Nat>) -> Result<FiniteSubgroup, Text> =
        if_then_else(not(fa_subgroup_valid(g, members)), err("invalid subgroup", FiniteSubgroup),
            let ordered = fa_select(field(g, order), 0, fun(x: Nat, fa_has(members, x))) in
            let k = length(ordered) in
            let t = fa_values(mul(k, k), 0, fun(i: Nat, fa_position(ordered, fa_op(g, fa_at(ordered, div(i, k)), fa_at(ordered, mod(i, k))), 0))) in
            ok(FiniteSubgroup(FiniteGroup(k, fa_position(ordered, field(g, unit), 0), t), ordered), Text))
    fn group_normal(g: FiniteGroup, members: List<Nat>) -> Bool =
        if_then_else(not(fa_subgroup_valid(g, members)), false,
            fa_all(field(g, order), fun(a: Nat, fa_all(length(members), fun(i: Nat, fa_has(members, fa_op(g, fa_op(g, a, fa_at(members, i)), fa_inverse(field(g, order), g, a))))))))
    private fn fa_coset_min(xs: List<Nat>, g: FiniteGroup, x: Nat, best: Nat) -> Nat decreasing xs =
        if_then_else(is_empty(xs), best,
            let candidate = fa_op(g, x, unwrap_or(head(xs), 999)) in
            fa_coset_min(tail(xs), g, x, if_then_else(best > candidate, candidate, best)))
    private fn fa_leaders(n: Nat, i: Nat, g: FiniteGroup, members: List<Nat>) -> List<Nat> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(Nat),
            if_then_else(nat_eq(fa_coset_min(members, g, i, field(g, order)), i), cons(i, fa_leaders(pred(n), i + 1, g, members)), fa_leaders(pred(n), i + 1, g, members)))
    fn group_quotient(g: FiniteGroup, members: List<Nat>) -> Result<FiniteQuotient, Text> =
        if_then_else(not(group_normal(g, members)), err("quotient requires a valid normal subgroup", FiniteQuotient),
            let reps = fa_leaders(field(g, order), 0, g, members) in let k = length(reps) in
            let projection = fa_values(field(g, order), 0, fun(x: Nat, fa_position(reps, fa_coset_min(members, g, x, field(g, order)), 0))) in
            let table = fa_values(mul(k, k), 0, fun(i: Nat, fa_at(projection, fa_op(g, fa_at(reps, div(i, k)), fa_at(reps, mod(i, k)))))) in
            ok(FiniteQuotient(FiniteGroup(k, fa_at(projection, field(g, unit)), table), projection), Text))

    fn group_first_isomorphism(g: FiniteGroup, h: FiniteGroup, mapping: List<Nat>) -> Result<FiniteGroupIsomorphism, Text> =
        match group_kernel(g, h, mapping) {
            err(e) => err(e, FiniteGroupIsomorphism),
            ok(kernel) => match group_image(g, h, mapping) {
                err(e) => err(e, FiniteGroupIsomorphism),
                ok(image) => match group_quotient(g, kernel) {
                    err(e) => err(e, FiniteGroupIsomorphism),
                    ok(quotient) => match group_subgroup(h, image) {
                        err(e) => err(e, FiniteGroupIsomorphism),
                        ok(subgroup) =>
                            let q = field(quotient, group) in
                            let induced = fa_values(field(q, order), 0, fun(i: Nat, fa_position(image, fa_at(mapping, fa_position(field(quotient, projection), i, 0)), 0))) in
                            if_then_else(and(group_homomorphism(q, field(subgroup, group), induced), and(fa_distinct(induced), nat_eq(length(induced), length(image)))), ok(FiniteGroupIsomorphism(quotient, subgroup, induced), Text), err("induced map is not an isomorphism", FiniteGroupIsomorphism))
                    }
                }
            }
        }

    private fn fa_plus(r: FiniteRing, a: Nat, b: Nat) -> Nat = fa_get(field(r, addition), field(r, order), a, b)
    private fn fa_times(r: FiniteRing, a: Nat, b: Nat) -> Nat = fa_get(field(r, multiplication), field(r, order), a, b)
    private fn fa_additive(r: FiniteRing) -> FiniteGroup = FiniteGroup(field(r, order), field(r, zero), field(r, addition))
    private fn fa_ring_laws(r: FiniteRing) -> Bool =
        let n = field(r, order) in let z = field(r, zero) in let e = field(r, one) in
        fa_all(n, fun(a: Nat,
            and(and(nat_eq(fa_plus(r, a, z), a), nat_eq(fa_times(r, e, a), a)),
                and(and(nat_eq(fa_times(r, a, e), a), and(nat_eq(fa_times(r, z, a), z), nat_eq(fa_times(r, a, z), z))),
                    fa_all(n, fun(b: Nat,
                        and(nat_eq(fa_plus(r, a, b), fa_plus(r, b, a)), fa_all(n, fun(c: Nat,
                            and(nat_eq(fa_times(r, fa_times(r, a, b), c), fa_times(r, a, fa_times(r, b, c))),
                                and(nat_eq(fa_times(r, a, fa_plus(r, b, c)), fa_plus(r, fa_times(r, a, b), fa_times(r, a, c))),
                                    nat_eq(fa_times(r, fa_plus(r, a, b), c), fa_plus(r, fa_times(r, a, c), fa_times(r, b, c))))))))))))))

    fn ring_valid(r: FiniteRing) -> Bool =
        if_then_else(not(fa_size(field(r, order))), false,
            if_then_else(not(field(r, order) > field(r, one)), false,
                if_then_else(and(group_valid(fa_additive(r)), fa_table(field(r, order), field(r, multiplication))), fa_ring_laws(r), false)))
    fn ring_check(r: FiniteRing) -> Result<FiniteRing, Text> =
        if_then_else(ring_valid(r), ok(r, Text), err("invalid finite ring or order budget", FiniteRing))
    fn ring_residues(n: Nat) -> Result<FiniteRing, Text> =
        if_then_else(not(fa_size(n)), err("finite algebra order must be 1..=8", FiniteRing),
            ring_check(FiniteRing(n, 0, mod(1, n), fa_values(mul(n, n), 0, fun(i: Nat, mod(div(i, n) + mod(i, n), n))), fa_values(mul(n, n), 0, fun(i: Nat, mod(mul(div(i, n), mod(i, n)), n))))))
    fn ring_add(r: FiniteRing, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(ring_valid(r)), err("invalid finite ring", Nat), if_then_else(and(field(r, order) > a, field(r, order) > b), ok(fa_plus(r, a, b), Text), err("ring element out of range", Nat)))
    fn ring_mul(r: FiniteRing, a: Nat, b: Nat) -> Result<Nat, Text> =
        if_then_else(not(ring_valid(r)), err("invalid finite ring", Nat), if_then_else(and(field(r, order) > a, field(r, order) > b), ok(fa_times(r, a, b), Text), err("ring element out of range", Nat)))
    fn ring_neg(r: FiniteRing, a: Nat) -> Result<Nat, Text> =
        if_then_else(not(ring_valid(r)), err("invalid finite ring", Nat), group_inverse(fa_additive(r), a))
    private fn fa_mult_inverse(n: Nat, r: FiniteRing, a: Nat) -> Nat decreasing n =
        if_then_else(nat_eq(n, 0), 999,
            if_then_else(and(nat_eq(fa_times(r, a, pred(n)), field(r, one)), nat_eq(fa_times(r, pred(n), a), field(r, one))), pred(n), fa_mult_inverse(pred(n), r, a)))
    fn ring_is_field(r: FiniteRing) -> Bool =
        if_then_else(not(ring_valid(r)), false,
            if_then_else(nat_eq(field(r, zero), field(r, one)), false,
                fa_all(field(r, order), fun(a: Nat,
                    and(if_then_else(nat_eq(a, field(r, zero)), true, field(r, order) > fa_mult_inverse(field(r, order), r, a)),
                        fa_all(field(r, order), fun(b: Nat, nat_eq(fa_times(r, a, b), fa_times(r, b, a)))))))))
    fn field_check(r: FiniteRing) -> Result<FiniteRing, Text> = if_then_else(ring_is_field(r), ok(r, Text), err("ring is not a field", FiniteRing))
    fn field_inverse(r: FiniteRing, a: Nat) -> Result<Nat, Text> =
        if_then_else(not(ring_is_field(r)), err("ring is not a field", Nat),
            if_then_else(or(not(field(r, order) > a), nat_eq(a, field(r, zero))), err("invalid nonzero field element", Nat), ok(fa_mult_inverse(field(r, order), r, a), Text)))
    fn ring_homomorphism(r: FiniteRing, s: FiniteRing, mapping: List<Nat>) -> Bool =
        if_then_else(not(and(ring_valid(r), ring_valid(s))), false,
            if_then_else(not(group_homomorphism(fa_additive(r), fa_additive(s), mapping)), false,
                and(nat_eq(fa_at(mapping, field(r, one)), field(s, one)), fa_all(field(r, order), fun(a: Nat, fa_all(field(r, order), fun(b: Nat, nat_eq(fa_at(mapping, fa_times(r, a, b)), fa_times(s, fa_at(mapping, a), fa_at(mapping, b))))))))))

    fn ring_kernel(r: FiniteRing, s: FiniteRing, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(ring_homomorphism(r, s, mapping)), err("invalid ring homomorphism", List(Nat)), group_kernel(fa_additive(r), fa_additive(s), mapping))
    fn ring_image(r: FiniteRing, s: FiniteRing, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(ring_homomorphism(r, s, mapping)), err("invalid ring homomorphism", List(Nat)), group_image(fa_additive(r), fa_additive(s), mapping))

    private fn fa_mod_group(m: FiniteModule) -> FiniteGroup = FiniteGroup(field(m, order), field(m, zero), field(m, addition))
    private fn fa_scale(m: FiniteModule, r: Nat, x: Nat) -> Nat = fa_get(field(m, action), field(m, order), r, x)
    private fn fa_module_laws(r: FiniteRing, m: FiniteModule) -> Bool =
        let n = field(m, order) in let g = fa_mod_group(m) in
        and(group_abelian(g), fa_all(n, fun(x: Nat,
            and(and(nat_eq(fa_scale(m, field(r, one), x), x), nat_eq(fa_scale(m, field(r, zero), x), field(m, zero))),
                fa_all(field(r, order), fun(a: Nat,
                    and(nat_eq(fa_scale(m, a, field(m, zero)), field(m, zero)),
                        and(fa_all(n, fun(y: Nat, nat_eq(fa_scale(m, a, fa_op(g, x, y)), fa_op(g, fa_scale(m, a, x), fa_scale(m, a, y))))),
                            fa_all(field(r, order), fun(b: Nat,
                                and(nat_eq(fa_scale(m, fa_plus(r, a, b), x), fa_op(g, fa_scale(m, a, x), fa_scale(m, b, x))),
                                    nat_eq(fa_scale(m, fa_times(r, a, b), x), fa_scale(m, a, fa_scale(m, b, x))))))))))))))

    fn module_valid(r: FiniteRing, m: FiniteModule) -> Bool =
        if_then_else(not(ring_valid(r)), false,
            if_then_else(not(fa_size(field(m, order))), false,
                if_then_else(not(nat_eq(length(field(m, action)), mul(field(r, order), field(m, order)))), false,
                    if_then_else(and(group_valid(fa_mod_group(m)), fold(field(m, action), true, fun(acc: Bool, x: Nat, and(acc, field(m, order) > x)))), fa_module_laws(r, m), false))))
    fn module_check(r: FiniteRing, m: FiniteModule) -> Result<FiniteModule, Text> =
        if_then_else(module_valid(r, m), ok(m, Text), err("invalid finite module or order budget", FiniteModule))
    fn module_regular(r: FiniteRing) -> Result<FiniteModule, Text> =
        if_then_else(not(ring_valid(r)), err("invalid base ring", FiniteModule), module_check(r, FiniteModule(field(r, order), field(r, zero), field(r, addition), field(r, multiplication))))
    fn module_scale(r: FiniteRing, m: FiniteModule, a: Nat, x: Nat) -> Result<Nat, Text> =
        if_then_else(not(module_valid(r, m)), err("invalid finite module", Nat),
            if_then_else(and(field(r, order) > a, field(m, order) > x), ok(fa_scale(m, a, x), Text), err("module scalar or element out of range", Nat)))
    fn module_homomorphism(r: FiniteRing, m: FiniteModule, n: FiniteModule, mapping: List<Nat>) -> Bool =
        if_then_else(not(and(module_valid(r, m), module_valid(r, n))), false,
            if_then_else(not(group_homomorphism(fa_mod_group(m), fa_mod_group(n), mapping)), false,
                fa_all(field(r, order), fun(a: Nat, fa_all(field(m, order), fun(x: Nat, nat_eq(fa_at(mapping, fa_scale(m, a, x)), fa_scale(n, a, fa_at(mapping, x)))))))))

    fn module_kernel(r: FiniteRing, m: FiniteModule, n: FiniteModule, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(module_homomorphism(r, m, n, mapping)), err("invalid module homomorphism", List(Nat)), group_kernel(fa_mod_group(m), fa_mod_group(n), mapping))
    fn module_image(r: FiniteRing, m: FiniteModule, n: FiniteModule, mapping: List<Nat>) -> Result<List<Nat>, Text> =
        if_then_else(not(module_homomorphism(r, m, n, mapping)), err("invalid module homomorphism", List(Nat)), group_image(fa_mod_group(m), fa_mod_group(n), mapping))

    theorem cyclic_two: is_ok(group_cyclic(2))
    theorem residues_two_is_field: ring_is_field(unwrap_or(ring_residues(2), FiniteRing(0, 0, 0, empty_list(Nat), empty_list(Nat))))
}
