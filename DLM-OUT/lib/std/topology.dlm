// Integral simplicial homology and Vietoris–Rips persistence (v0.119, docs/LOGIC_TOPOLOGY.md).
module std.topology
import std.logic

theory StdTopology extends StdLogic {
    record Complex(faces: NatVec, ends: NatVec)
    record HomologyGroup(rank: Nat, torsion: NatVec)
    record Bar(dim: Nat, birth: Float, death: Float)

    fn complex() -> Complex = Complex(nv_empty(), nv_empty())
    fn add_simplex(k: Complex, vs: List<Nat>) -> Complex =
        let fs = fold(vs, field(k, faces), fun(acc: NatVec, v: Nat, unwrap_or(nv_push(acc, v), acc))) in
        Complex(fs, unwrap_or(nv_push(field(k, ends), nv_len(fs)), field(k, ends)))
    fn from_simplices(ss: List<List<Nat>>) -> Complex = fold(ss, complex(), add_simplex)
    private fn decode(k: Nat, code: NatVec, i: Nat, acc: List<HomologyGroup>) -> List<HomologyGroup> decreasing k =
        if_then_else(nat_eq(k, 0), acc, let r = unwrap_or(nv_get(code, i), 0) in let t = unwrap_or(nv_get(code, i + 1), 0) in
            decode(pred(k), code, i + 2 + t, cons(HomologyGroup(r, unwrap_or(nv_slice(code, i + 2, t), nv_empty())), acc)))
    private fn rev_groups(xs: List<HomologyGroup>) -> List<HomologyGroup> = fold(xs, empty_list(HomologyGroup), fun(acc: List<HomologyGroup>, x: HomologyGroup, cons(x, acc)))
    // H_0, H_1, ... over Z (rank and torsion coefficients)
    fn homology(k: Complex) -> Option<List<HomologyGroup>> =
        match tp_homology(field(k, faces), field(k, ends)) { none => none(List(HomologyGroup)), some(code) =>
            some(rev_groups(decode(unwrap_or(nv_get(code, 0), 0), code, 1, empty_list(HomologyGroup)))) }
    fn betti(k: Complex) -> Option<NatVec> = match homology(k) { none => none(NatVec), some(hs) => nv_from_list(map(hs, fun(h: HomologyGroup, field(h, rank)))) }
    // Euler characteristic as an Int
    fn euler_characteristic(k: Complex) -> Option<Int> =
        match betti(k) { none => none(Int), some(b) => some(fst(fold(nv_to_list(b), pair(int(0), 0), fun(acc: ProductType<Int*Nat>, x: Nat,
            pair(if_then_else(nat_eq(mod(snd(acc), 2), 0), fst(acc) + int(x), fst(acc) - int(x)), snd(acc) + 1))))) }
    fn has_torsion(k: Complex) -> Bool = match homology(k) { none => false, some(hs) => fold(hs, false, fun(acc: Bool, h: HomologyGroup, or(acc, nv_len(field(h, torsion)) > 0))) }
    fn group_text(h: HomologyGroup) -> Text =
        let free = if_then_else(nat_eq(field(h, rank), 0), "", if_then_else(nat_eq(field(h, rank), 1), "Z", concat("Z^", nat_to_text(field(h, rank))))) in
        let tors = join(map(nv_to_list(field(h, torsion)), fun(t: Nat, concat("Z/", nat_to_text(t)))), " + ") in
        if_then_else(and(text_eq(free, ""), text_eq(tors, "")), "0", if_then_else(text_eq(tors, ""), free, if_then_else(text_eq(free, ""), tors, concat(free, concat(" + ", tors)))))

    // ---- standard spaces ----
    private fn all_but(k: Nat, n: Nat, skip: Nat, acc: List<Nat>) -> List<Nat> decreasing k =
        if_then_else(nat_eq(k, 0), acc, let v = monus(k, 1) in all_but(pred(k), n, skip, if_then_else(nat_eq(v, skip), acc, cons(v, acc))))
    // boundary of the (n+1)-simplex: a triangulated n-sphere
    fn sphere(n: Nat) -> Complex = fold(nv_to_list(unwrap_or(nv_range(0, n + 2), nv_empty())), complex(), fun(k: Complex, skip: Nat, add_simplex(k, all_but(n + 2, n + 2, skip, empty_list(Nat)))))
    fn ball(n: Nat) -> Complex = add_simplex(complex(), nv_to_list(unwrap_or(nv_range(0, n + 1), nv_empty())))
    // Möbius–Császár 7-vertex torus: triangles {i, i+1, i+3} and {i, i+2, i+3} mod 7
    fn torus() -> Complex =
        fold(nv_to_list(unwrap_or(nv_range(0, 7), nv_empty())), complex(), fun(k: Complex, i: Nat,
            add_simplex(add_simplex(k, list(i, mod(i + 1, 7), mod(i + 3, 7))), list(i, mod(i + 2, 7), mod(i + 3, 7)))))
    // 6-vertex real projective plane
    fn projective_plane() -> Complex = from_simplices(list(list(0, 1, 2), list(0, 2, 3), list(0, 3, 4), list(0, 4, 5), list(0, 5, 1), list(1, 2, 4), list(2, 3, 5), list(3, 4, 1), list(4, 5, 2), list(5, 1, 3)))
    fn circle(n: Nat) -> Complex = fold(nv_to_list(unwrap_or(nv_range(0, n), nv_empty())), complex(), fun(k: Complex, i: Nat, add_simplex(k, list(i, mod(i + 1, n)))))
    fn wedge_of_circles(k: Nat) -> Complex =
        fold(nv_to_list(unwrap_or(nv_range(0, k), nv_empty())), complex(), fun(c: Complex, i: Nat, let a = 1 + mul(2, i) in add_simplex(add_simplex(add_simplex(c, list(0, a)), list(a, a + 1)), list(a + 1, 0))))

    // ---- persistence ----
    private fn bars_loop(k: Nat, v: FloatVec, i: Nat, acc: List<Bar>) -> List<Bar> decreasing k =
        if_then_else(nat_eq(k, 0), acc, bars_loop(pred(k), v, i + 3, cons(Bar(abs(unwrap_or(fl_to_int(unwrap_or(fv_get(v, i), 0.0)), int(0))), unwrap_or(fv_get(v, i + 1), 0.0), unwrap_or(fv_get(v, i + 2), 0.0)), acc)))
    private fn rev_bars(xs: List<Bar>) -> List<Bar> = fold(xs, empty_list(Bar), fun(acc: List<Bar>, x: Bar, cons(x, acc)))
    // Vietoris–Rips barcode of points (rows of `dim` coordinates) up to homological dimension maxdim <= 2
    fn rips_barcode(points: FloatVec, dim: Nat, maxdim: Nat, radius: Float) -> Option<List<Bar>> =
        match tp_rips(points, dim, maxdim, radius) { none => none(List(Bar)), some(v) => some(rev_bars(bars_loop(div(fv_len(v), 3), v, 0, empty_list(Bar)))) }
    fn bar_length(b: Bar) -> Float = fl_sub(field(b, death), field(b, birth))
    // number of bars of dimension d alive at scale r
    fn betti_at(bars: List<Bar>, d: Nat, r: Float) -> Nat =
        fold(bars, 0, fun(acc: Nat, b: Bar, if_then_else(and(nat_eq(field(b, dim), d), and(fl_le(field(b, birth), r), fl_lt(r, field(b, death)))), acc + 1, acc)))
    // bars of dimension d longer than `min_length`
    fn significant(bars: List<Bar>, d: Nat, min_length: Float) -> Nat =
        fold(bars, 0, fun(acc: Nat, b: Bar, if_then_else(and(nat_eq(field(b, dim), d), fl_lt(min_length, bar_length(b))), acc + 1, acc)))
    fn bar_text(b: Bar) -> Text = concat("H", concat(nat_to_text(field(b, dim)), concat(" [", concat(fl_to_text(field(b, birth)), concat(", ", concat(fl_to_text(field(b, death)), ")"))))))

    theorem circle_has_one_loop: nat_eq(unwrap_or(nv_get(unwrap_or(tp_homology(unwrap_or(nv_from_list(list(0, 1, 1, 2, 2, 0)), nv_empty()), unwrap_or(nv_from_list(list(2, 4, 6)), nv_empty())), nv_empty()), 3), 9), 1)
}
