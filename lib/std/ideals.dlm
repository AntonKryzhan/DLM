// Multivariate polynomials over F_p, Gröbner bases and ideal operations (v0.120, docs/GROEBNER.md).
// A polynomial stores its terms [c, e_1, ..., e_n] in a NatVec (normalized by the mv_* builtins).
module std.ideals
import std.topology

theory StdIdeals extends StdTopology {
    record MPoly(vars: Nat, p: Nat, terms: NatVec)
    record Basis(polys: List<MPoly>, order: Nat)

    fn lex() -> Nat = 0
    fn grlex() -> Nat = 1
    fn grevlex() -> Nat = 2
    fn mv_zero(n: Nat, p: Nat) -> MPoly = MPoly(n, p, nv_empty())
    fn mv_const(n: Nat, p: Nat, c: Nat) -> MPoly = MPoly(n, p, unwrap_or(mv_normalize(unwrap_or(nv_concat(unwrap_or(nv_from_list(list(mod(c, p))), nv_empty()), unwrap_or(nv_new(n, 0), nv_empty())), nv_empty()), n, p, 2), nv_empty()))
    // c · x^exps
    fn mv_term(n: Nat, p: Nat, c: Nat, exps: List<Nat>) -> MPoly =
        MPoly(n, p, if_then_else(nat_eq(length(exps), n), unwrap_or(mv_normalize(unwrap_or(nv_from_list(cons(mod(c, p), exps)), nv_empty()), n, p, 2), nv_empty()), nv_empty()))
    fn mv_var(n: Nat, p: Nat, i: Nat) -> MPoly = mv_term(n, p, 1, nv_to_list(unwrap_or(nv_set(unwrap_or(nv_new(n, 0), nv_empty()), i, 1), unwrap_or(nv_new(n, 0), nv_empty()))))
    private fn compatible(a: MPoly, b: MPoly) -> Bool = and(nat_eq(field(a, vars), field(b, vars)), nat_eq(field(a, p), field(b, p)))
    fn mv_plus(a: MPoly, b: MPoly) -> Option<MPoly> = if_then_else(compatible(a, b), match mv_add(field(a, terms), field(b, terms), field(a, vars), field(a, p)) { none => none(MPoly), some(t) => some(MPoly(field(a, vars), field(a, p), t)) }, none(MPoly))
    fn mv_minus(a: MPoly, b: MPoly) -> Option<MPoly> = if_then_else(compatible(a, b), match mv_sub(field(a, terms), field(b, terms), field(a, vars), field(a, p)) { none => none(MPoly), some(t) => some(MPoly(field(a, vars), field(a, p), t)) }, none(MPoly))
    fn mv_times(a: MPoly, b: MPoly) -> Option<MPoly> = if_then_else(compatible(a, b), match mv_mul(field(a, terms), field(b, terms), field(a, vars), field(a, p)) { none => none(MPoly), some(t) => some(MPoly(field(a, vars), field(a, p), t)) }, none(MPoly))
    private fn mpow_loop(k: Nat, a: MPoly, acc: MPoly) -> MPoly decreasing k = if_then_else(nat_eq(k, 0), acc, mpow_loop(pred(k), a, unwrap_or(mv_times(acc, a), acc)))
    fn mv_power(a: MPoly, k: Nat) -> MPoly = mpow_loop(k, a, mv_const(field(a, vars), field(a, p), 1))
    fn mv_sum(xs: List<MPoly>, n: Nat, p: Nat) -> MPoly = fold(xs, mv_zero(n, p), fun(acc: MPoly, x: MPoly, unwrap_or(mv_plus(acc, x), acc)))
    fn mv_is_zero(a: MPoly) -> Bool = nat_eq(nv_len(field(a, terms)), 0)
    fn mv_term_count(a: MPoly) -> Nat = div(nv_len(field(a, terms)), field(a, vars) + 1)
    private fn eval_term(a: MPoly, i: Nat, xs: NatVec) -> Nat =
        let w = field(a, vars) + 1 in
        fold(nv_to_list(unwrap_or(nv_range(0, field(a, vars)), nv_empty())), unwrap_or(nv_get(field(a, terms), mul(i, w)), 0), fun(acc: Nat, k: Nat,
            unwrap_or(nt_mod_mul(acc, unwrap_or(nt_mod_pow(unwrap_or(nv_get(xs, k), 0), unwrap_or(nv_get(field(a, terms), mul(i, w) + 1 + k), 0), field(a, p)), 0), field(a, p)), 0)))
    // value at a point of F_p^n
    fn mv_eval(a: MPoly, xs: NatVec) -> Nat =
        fold(nv_to_list(unwrap_or(nv_range(0, mv_term_count(a)), nv_empty())), 0, fun(acc: Nat, i: Nat, mod(acc + eval_term(a, i, xs), field(a, p))))
    private fn term_text(a: MPoly, i: Nat) -> Text =
        let w = field(a, vars) + 1 in let c = unwrap_or(nv_get(field(a, terms), mul(i, w)), 0) in
        let mono = join(fold(nv_to_list(unwrap_or(nv_range(0, field(a, vars)), nv_empty())), empty_list(Text), fun(acc: List<Text>, k: Nat,
            let e = unwrap_or(nv_get(field(a, terms), mul(i, w) + 1 + k), 0) in
            if_then_else(nat_eq(e, 0), acc, cons(concat("x", concat(nat_to_text(k + 1), if_then_else(nat_eq(e, 1), "", concat("^", nat_to_text(e))))), acc)))), "*") in
        if_then_else(text_eq(mono, ""), nat_to_text(c), if_then_else(nat_eq(c, 1), mono, concat(nat_to_text(c), concat("*", mono))))
    fn mv_text(a: MPoly) -> Text =
        if_then_else(mv_is_zero(a), "0", join(map(nv_to_list(unwrap_or(nv_range(0, mv_term_count(a)), nv_empty())), fun(i: Nat, term_text(a, i))), " + "))

    // ---- systems and Gröbner bases ----
    private fn system_terms(xs: List<MPoly>) -> NatVec = fold(xs, nv_empty(), fun(acc: NatVec, x: MPoly, unwrap_or(nv_concat(acc, field(x, terms)), acc)))
    private fn system_ends(xs: List<MPoly>) -> NatVec =
        snd(fold(xs, pair(0, nv_empty()), fun(acc: ProductType<Nat*NatVec>, x: MPoly, let e = fst(acc) + nv_len(field(x, terms)) in pair(e, unwrap_or(nv_push(snd(acc), e), snd(acc))))))
    private fn decode_basis(k: Nat, code: NatVec, n: Nat, p: Nat, i: Nat, off: Nat, acc: List<MPoly>) -> List<MPoly> decreasing k =
        if_then_else(nat_eq(k, 0), acc, let len = unwrap_or(nv_get(code, i), 0) in
            decode_basis(pred(k), code, n, p, i + 1, off + len, cons(MPoly(n, p, unwrap_or(nv_slice(code, off, len), nv_empty())), acc)))
    private fn rev_polys(xs: List<MPoly>) -> List<MPoly> = fold(xs, empty_list(MPoly), fun(acc: List<MPoly>, x: MPoly, cons(x, acc)))
    // reduced Gröbner basis of the ideal generated by gens (all in n variables over F_p)
    fn groebner(gens: List<MPoly>, n: Nat, p: Nat, order: Nat) -> Option<Basis> =
        if_then_else(fold(gens, true, fun(ok: Bool, f: MPoly, and(ok, and(nat_eq(field(f, vars), n), nat_eq(field(f, p), p))))),
            match gb_basis(system_terms(gens), system_ends(gens), n, p, order) { none => none(Basis), some(code) =>
                let k = unwrap_or(nv_get(code, 0), 0) in some(Basis(rev_polys(decode_basis(k, code, n, p, 1, k + 1, empty_list(MPoly))), order)) }, none(Basis))
    fn normal_form(f: MPoly, b: Basis) -> Option<MPoly> =
        if_then_else(fold(field(b, polys), true, fun(ok: Bool, g: MPoly, and(ok, compatible(f, g)))),
            match gb_reduce(field(f, terms), system_terms(field(b, polys)), system_ends(field(b, polys)), field(f, vars), field(f, p), field(b, order)) { none => none(MPoly), some(t) => some(MPoly(field(f, vars), field(f, p), t)) }, none(MPoly))
    fn ideal_contains(gens: List<MPoly>, f: MPoly, order: Nat) -> Option<Bool> =
        match groebner(gens, field(f, vars), field(f, p), order) { none => none(Bool), some(b) => match normal_form(f, b) { none => none(Bool), some(r) => some(mv_is_zero(r)) } }
    // the ideal is (1): the system has no solution over the algebraic closure of F_p
    fn is_unit_ideal(b: Basis) -> Bool = match head(field(b, polys)) { none => false, some(g) => and(nat_eq(mv_term_count(g), 1), nat_eq(fold(nv_to_list(field(g, terms)), 0, fun(acc: Nat, x: Nat, acc + x)), 1)) }
    fn basis_equal(a: Basis, b: Basis) -> Bool = and(nat_eq(field(a, order), field(b, order)), equal(field(a, polys), field(b, polys)))
    private fn uses_first(f: MPoly, k: Nat) -> Bool =
        let w = field(f, vars) + 1 in
        fold(nv_to_list(unwrap_or(nv_range(0, mv_term_count(f)), nv_empty())), false, fun(acc: Bool, i: Nat,
            or(acc, fold(nv_to_list(unwrap_or(nv_range(0, k), nv_empty())), false, fun(a2: Bool, v: Nat, or(a2, unwrap_or(nv_get(field(f, terms), mul(i, w) + 1 + v), 0) > 0))))))
    // generators of the elimination ideal I ∩ F_p[x_{k+1}, ..., x_n] (lex basis)
    fn eliminate(gens: List<MPoly>, n: Nat, p: Nat, k: Nat) -> Option<List<MPoly>> =
        match groebner(gens, n, p, lex()) { none => none(List(MPoly)), some(b) => some(fold(rev_polys(field(b, polys)), empty_list(MPoly), fun(acc: List<MPoly>, f: MPoly, if_then_else(uses_first(f, k), acc, cons(f, acc))))) }
    fn basis_text(b: Basis) -> Text = join(map(field(b, polys), mv_text), ", ")

    theorem unit_ideal_from_one: nat_eq(unwrap_or(nv_get(unwrap_or(gb_basis(unwrap_or(nv_from_list(list(1, 0)), nv_empty()), unwrap_or(nv_from_list(list(2)), nv_empty()), 1, 7, 0), nv_empty()), 0), 0), 1)
}
