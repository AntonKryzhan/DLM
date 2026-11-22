// The category FinSet (v0.120, docs/GROEBNER.md): objects n = {0, ..., n-1}, morphisms as image vectors,
// with composition, (co)products, (co)equalizers, pullbacks, pushouts, images and characteristic maps.
module std.categories
import std.ideals

theory StdCategories extends StdIdeals {
    record FinMap(dom: Nat, cod: Nat, f: NatVec)
    record Span(apex: Nat, left: FinMap, right: FinMap)
    record Cospan(apex: Nat, left: FinMap, right: FinMap)

    private fn valid_map(m: FinMap) -> Bool = and(nat_eq(nv_len(field(m, f)), field(m, dom)),
        match nv_max(field(m, f)) { none => true, some(x) => field(m, cod) > x })
    private fn map_product(a: Nat, b: Nat) -> Option<Nat> =
        if_then_else(nat_eq(a, 0), some(0), if_then_else(b > div(340282366920938463463374607431768211455, a), none(Nat), some(mul(a, b))))
    fn fin_map(dom: Nat, cod: Nat, xs: List<Nat>) -> Option<FinMap> =
        match nv_from_list(xs) { none => none(FinMap), some(v) =>
            if_then_else(and(nat_eq(nv_len(v), dom), match nv_max(v) { none => true, some(m) => cod > m }), some(FinMap(dom, cod, v)), none(FinMap)) }
    fn apply_map(m: FinMap, x: Nat) -> Nat = unwrap_or(nv_get(field(m, f), x), 0)
    fn identity_map(n: Nat) -> FinMap = FinMap(n, n, unwrap_or(nv_range(0, n), nv_empty()))
    private fn range_list(n: Nat) -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, n), nv_empty()))
    private fn tabulate(dom: Nat, cod: Nat, h: Fn<Nat, Nat>) -> FinMap = FinMap(dom, cod, unwrap_or(nv_from_list(map(range_list(dom), h)), nv_empty()))
    // g ∘ f
    fn compose_maps(g: FinMap, f: FinMap) -> Option<FinMap> =
        if_then_else(and(and(valid_map(f), valid_map(g)), nat_eq(field(f, cod), field(g, dom))), some(tabulate(field(f, dom), field(g, cod), fun(x: Nat, apply_map(g, apply_map(f, x))))), none(FinMap))
    // distinct values, increasing
    fn image(m: FinMap) -> NatVec =
        let s = nv_sort(field(m, f)) in
        fold(nv_to_list(s), nv_empty(), fun(acc: NatVec, x: Nat, if_then_else(and(nv_len(acc) > 0, nat_eq(unwrap_or(nv_get(acc, monus(nv_len(acc), 1)), 0), x)), acc, unwrap_or(nv_push(acc, x), acc))))
    fn is_injective(m: FinMap) -> Bool = and(valid_map(m), nat_eq(nv_len(image(m)), field(m, dom)))
    fn is_surjective(m: FinMap) -> Bool = and(valid_map(m), nat_eq(nv_len(image(m)), field(m, cod)))
    fn is_iso(m: FinMap) -> Bool = and(is_injective(m), is_surjective(m))
    fn inverse_map(m: FinMap) -> Option<FinMap> =
        if_then_else(is_iso(m), some(tabulate(field(m, cod), field(m, dom), fun(y: Nat, unwrap_or(nv_find(field(m, f), y), 0)))), none(FinMap))
    fn equal_maps(a: FinMap, b: FinMap) -> Bool = and(and(nat_eq(field(a, dom), field(b, dom)), nat_eq(field(a, cod), field(b, cod))), equal(field(a, f), field(b, f)))

    // ---- products and coproducts ----
    fn product_object(a: Nat, b: Nat) -> Nat = mul(a, b)
    fn proj1(a: Nat, b: Nat) -> FinMap = tabulate(mul(a, b), a, fun(i: Nat, div(i, b)))
    fn proj2(a: Nat, b: Nat) -> FinMap = tabulate(mul(a, b), b, fun(i: Nat, mod(i, b)))
    // ⟨f, g⟩ : X → A × B
    fn pairing(f: FinMap, g: FinMap) -> Option<FinMap> =
        if_then_else(and(and(valid_map(f), valid_map(g)), nat_eq(field(f, dom), field(g, dom))),
            match map_product(field(f, cod), field(g, cod)) { none => none(FinMap), some(cod) =>
                some(tabulate(field(f, dom), cod, fun(x: Nat, mul(apply_map(f, x), field(g, cod)) + apply_map(g, x)))) }, none(FinMap))
    fn inj1(a: Nat, b: Nat) -> FinMap = tabulate(a, a + b, fun(i: Nat, i))
    fn inj2(a: Nat, b: Nat) -> FinMap = tabulate(b, a + b, fun(i: Nat, a + i))
    // [f, g] : A + B → X
    fn copairing(f: FinMap, g: FinMap) -> Option<FinMap> =
        if_then_else(and(valid_map(f), valid_map(g)), if_then_else(and(nat_eq(field(f, cod), field(g, cod)), not(field(f, dom) + field(g, dom) > 16777216)), some(tabulate(field(f, dom) + field(g, dom), field(f, cod), fun(i: Nat, if_then_else(field(f, dom) > i, apply_map(f, i), apply_map(g, monus(i, field(f, dom))))))), none(FinMap)), none(FinMap))
    fn terminal() -> Nat = 1
    fn initial() -> Nat = 0
    fn to_terminal(a: Nat) -> FinMap = tabulate(a, 1, fun(i: Nat, 0))
    fn hom_count(a: Nat, b: Nat) -> Nat = b ^ a

    // ---- limits and colimits ----
    // inclusion of {x | f(x) = g(x)} into the common domain
    fn equalizer(f: FinMap, g: FinMap) -> Option<FinMap> =
        if_then_else(and(and(valid_map(f), valid_map(g)), and(nat_eq(field(f, dom), field(g, dom)), nat_eq(field(f, cod), field(g, cod)))),
            let xs = fold(range_list(field(f, dom)), empty_list(Nat), fun(acc: List<Nat>, x: Nat, if_then_else(nat_eq(apply_map(f, x), apply_map(g, x)), cons(x, acc), acc))) in
            let v = nv_reverse(unwrap_or(nv_from_list(xs), nv_empty())) in some(FinMap(nv_len(v), field(f, dom), v)), none(FinMap))
    private fn find_root(k: Nat, parent: NatVec, x: Nat) -> Nat decreasing k =
        if_then_else(nat_eq(k, 0), x, let p = unwrap_or(nv_get(parent, x), x) in if_then_else(nat_eq(p, x), x, find_root(pred(k), parent, p)))
    private fn union(parent: NatVec, a: Nat, b: Nat) -> NatVec =
        let n = nv_len(parent) in let ra = find_root(n, parent, a) in let rb = find_root(n, parent, b) in
        if_then_else(nat_eq(ra, rb), parent, if_then_else(ra > rb, unwrap_or(nv_set(parent, ra, rb), parent), unwrap_or(nv_set(parent, rb, ra), parent)))
    // quotient map of n elements by the equivalence generated by pairs (a_i, b_i), classes numbered by first element
    fn quotient(n: Nat, pairs: List<ProductType<Nat*Nat>>) -> FinMap =
        let parent = fold(pairs, unwrap_or(nv_range(0, n), nv_empty()), fun(acc: NatVec, q: ProductType<Nat*Nat>, union(acc, fst(q), snd(q)))) in
        let roots = map(range_list(n), fun(x: Nat, find_root(n, parent, x))) in
        let labels = fold(roots, nv_empty(), fun(acc: NatVec, r: Nat, if_then_else(is_some(nv_find(acc, r)), acc, unwrap_or(nv_push(acc, r), acc)))) in
        FinMap(n, nv_len(labels), unwrap_or(nv_from_list(map(roots, fun(r: Nat, unwrap_or(nv_find(labels, r), 0)))), nv_empty()))
    fn coequalizer(f: FinMap, g: FinMap) -> Option<FinMap> =
        if_then_else(and(not(field(f, cod) > 16777216), and(and(valid_map(f), valid_map(g)), and(nat_eq(field(f, dom), field(g, dom)), nat_eq(field(f, cod), field(g, cod))))),
            some(quotient(field(f, cod), map(range_list(field(f, dom)), fun(x: Nat, pair(apply_map(f, x), apply_map(g, x)))))), none(FinMap))
    // A ×_C B for f : A → C, g : B → C
    fn pullback(f: FinMap, g: FinMap) -> Option<Span> =
        if_then_else(and(and(valid_map(f), valid_map(g)), nat_eq(field(f, cod), field(g, cod))),
            if_then_else(mul(field(f, dom), field(g, dom)) > 16777216, none(Span),
            let ps = fold(range_list(field(f, dom)), empty_list(Product(Nat, Nat)), fun(acc: List<ProductType<Nat*Nat>>, a: Nat,
                fold(range_list(field(g, dom)), acc, fun(acc2: List<ProductType<Nat*Nat>>, b: Nat, if_then_else(nat_eq(apply_map(f, a), apply_map(g, b)), cons(pair(a, b), acc2), acc2))))) in
            let qs = fold(ps, empty_list(Product(Nat, Nat)), fun(acc: List<ProductType<Nat*Nat>>, q: ProductType<Nat*Nat>, cons(q, acc))) in
            let n = length(qs) in
            some(Span(n, FinMap(n, field(f, dom), unwrap_or(nv_from_list(map(qs, fun(q: ProductType<Nat*Nat>, fst(q)))), nv_empty())),
                FinMap(n, field(g, dom), unwrap_or(nv_from_list(map(qs, fun(q: ProductType<Nat*Nat>, snd(q)))), nv_empty()))))), none(Span))
    // A +_C B for f : C → A, g : C → B
    fn pushout(f: FinMap, g: FinMap) -> Option<Cospan> =
        if_then_else(and(and(valid_map(f), valid_map(g)), nat_eq(field(f, dom), field(g, dom))),
            if_then_else(or(field(f, cod) > 16777216, field(g, cod) > 16777216), none(Cospan),
            let a = field(f, cod) in let b = field(g, cod) in
            if_then_else(a + b > 16777216, none(Cospan),
            let q = quotient(a + b, map(range_list(field(f, dom)), fun(c: Nat, pair(apply_map(f, c), a + apply_map(g, c))))) in
            some(Cospan(field(q, cod), tabulate(a, field(q, cod), fun(i: Nat, apply_map(q, i))), tabulate(b, field(q, cod), fun(i: Nat, apply_map(q, a + i))))))), none(Cospan))
    // characteristic map A → 2 of the image of m
    fn characteristic(m: FinMap) -> FinMap = let im = image(m) in tabulate(field(m, cod), 2, fun(y: Nat, if_then_else(is_some(nv_find(im, y)), 1, 0)))
    // factorization m = mono ∘ epi through the image
    fn epi_part(m: FinMap) -> FinMap = let im = image(m) in tabulate(field(m, dom), nv_len(im), fun(x: Nat, unwrap_or(nv_find(im, apply_map(m, x)), 0)))
    fn mono_part(m: FinMap) -> FinMap = let im = image(m) in FinMap(nv_len(im), field(m, cod), im)
    fn map_text(m: FinMap) -> Text = concat(nat_to_text(field(m, dom)), concat(" -> ", concat(nat_to_text(field(m, cod)), concat(" [", concat(join(map(nv_to_list(field(m, f)), fun(x: Nat, nat_to_text(x))), ","), "]")))))

    theorem identity_is_iso: is_iso(identity_map(3))
}
