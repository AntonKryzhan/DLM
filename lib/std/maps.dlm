// Combinators over the native ordered maps NatMap and TextMap (v0.122, docs/MAPS.md).
module std.maps
import std.categories

theory StdMaps extends StdCategories {
    record Entry(key: Nat, value: Nat)
    record TextEntry(key: Text, value: Nat)

    fn km_entries(m: NatMap) -> List<Entry> =
        let ks = nv_to_list(km_keys(m)) in map(ks, fun(k: Nat, Entry(k, unwrap_or(km_get(m, k), 0))))
    fn tm_entries(m: TextMap) -> List<TextEntry> = map(tm_keys(m), fun(k: Text, TextEntry(k, unwrap_or(tm_get(m, k), 0))))
    fn km_map_values(m: NatMap, f: Fn<Nat, Nat>) -> NatMap = unwrap_or(km_from(km_keys(m), unwrap_or(nv_from_list(map(nv_to_list(km_values(m)), f)), nv_empty())), m)
    fn km_filter(m: NatMap, keep: Fn<Nat, Nat, Bool>) -> NatMap =
        fold(km_entries(m), km_empty(), fun(acc: NatMap, e: Entry, if_then_else(keep(field(e, key), field(e, value)), unwrap_or(km_insert(acc, field(e, key), field(e, value)), acc), acc)))
    fn km_fold(m: NatMap, init: Nat, f: Fn<Nat, Nat, Nat, Nat>) -> Nat = fold(km_entries(m), init, fun(acc: Nat, e: Entry, f(acc, field(e, key), field(e, value))))
    fn km_sum_values(m: NatMap) -> Option<Nat> = nv_sum(km_values(m))
    // value -> key (for equal values the largest key wins)
    fn km_invert(m: NatMap) -> NatMap = unwrap_or(km_from(km_values(m), km_keys(m)), km_empty())
    // pointwise sum of counters
    fn km_add_all(a: NatMap, b: NatMap) -> Option<NatMap> =
        fold(km_entries(b), some(a), fun(acc: Option<NatMap>, e: Entry, match acc { none => none(NatMap), some(m) => km_add(m, field(e, key), field(e, value)) }))
    fn tm_add_all(a: TextMap, b: TextMap) -> Option<TextMap> =
        fold(tm_entries(b), some(a), fun(acc: Option<TextMap>, e: TextEntry, match acc { none => none(TextMap), some(m) => tm_add(m, field(e, key), field(e, value)) }))
    // multiplicities of the elements of a vector
    fn counts(xs: NatVec) -> NatMap = fold(nv_to_list(xs), km_empty(), fun(acc: NatMap, x: Nat, unwrap_or(km_add(acc, x, 1), acc)))
    fn text_counts(xs: List<Text>) -> TextMap = fold(xs, tm_empty(), fun(acc: TextMap, x: Text, unwrap_or(tm_add(acc, x, 1), acc)))
    private fn insert_by_count(es: List<TextEntry>, e: TextEntry) -> List<TextEntry> decreasing es =
        if_then_else(is_empty(es), list(e), match head(es) { none => list(e), some(h) =>
            if_then_else(field(e, value) > field(h, value), cons(e, es), cons(h, insert_by_count(tail(es), e))) })
    private fn take_texts(k: Nat, es: List<TextEntry>) -> List<Text> decreasing k =
        if_then_else(nat_eq(k, 0), empty_list(Text), match head(es) { none => empty_list(Text), some(e) => cons(field(e, key), take_texts(pred(k), tail(es))) })
    // the k keys with the largest values (ties in key order)
    fn most_common(m: TextMap, k: Nat) -> List<Text> = take_texts(k, fold(tm_entries(m), empty_list(TextEntry), insert_by_count))
    fn km_argmax(m: NatMap) -> Option<Nat> =
        fold(km_entries(m), none(Nat), fun(acc: Option<Nat>, e: Entry, match acc { none => some(field(e, key)), some(b) => if_then_else(field(e, value) > unwrap_or(km_get(m, b), 0), some(field(e, key)), acc) }))
    // group positions by value: value -> number of positions (a histogram) and value -> first position
    fn first_positions(xs: NatVec) -> NatMap =
        fold(nv_to_list(unwrap_or(nv_range(0, nv_len(xs)), nv_empty())), km_empty(), fun(acc: NatMap, i: Nat,
            let x = unwrap_or(nv_get(xs, i), 0) in if_then_else(km_contains(acc, x), acc, unwrap_or(km_insert(acc, x, i), acc))))

    theorem counting_is_exact: nat_eq(unwrap_or(km_get(counts(unwrap_or(nv_from_list(list(3, 1, 3)), nv_empty())), 3), 0), 2)
}
