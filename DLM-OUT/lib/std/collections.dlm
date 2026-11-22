// Generic collections (stage E-min, docs/GENERICS.md): templates instantiated with explicit type arguments,
// e.g. list_reverse<Nat>(xs), Stack<Text>, map_get<Text, Nat>(m, k, text_eq).
// Equality and ordering are passed as function values, or come from the bounds Eq / Ord (v0.111: `equal`, `less`);
// type arguments may be omitted when the arguments determine them: list_reverse(list(1, 2)).
module std.collections

theory StdCollections {
    record Duo<A, B>(left: A, right: B)
    record Stack<T>(items: List<T>)
    record Queue<T>(front: List<T>, back: List<T>)
    record Entry<K, V>(key: K, value: V)
    record AssocMap<K, V>(entries: List<Entry<K, V>>)

    fn list_reverse<T>(xs: List<T>) -> List<T> = fold(xs, empty_list(T), fun(acc: List<T>, x: T, cons(x, acc)))
    fn list_append<T>(xs: List<T>, ys: List<T>) -> List<T> = fold(list_reverse<T>(xs), ys, fun(acc: List<T>, x: T, cons(x, acc)))
    fn list_filter<T>(xs: List<T>, keep: Fn<T, Bool>) -> List<T> =
        list_reverse<T>(fold(xs, empty_list(T), fun(acc: List<T>, x: T, if_then_else(keep(x), cons(x, acc), acc))))
    fn list_any<T>(xs: List<T>, p: Fn<T, Bool>) -> Bool = fold(xs, false, fun(acc: Bool, x: T, or(acc, p(x))))
    fn list_all<T>(xs: List<T>, p: Fn<T, Bool>) -> Bool = fold(xs, true, fun(acc: Bool, x: T, and(acc, p(x))))
    fn list_count<T>(xs: List<T>, p: Fn<T, Bool>) -> Nat = fold(xs, 0, fun(acc: Nat, x: T, if_then_else(p(x), acc + 1, acc)))
    fn list_find<T>(xs: List<T>, p: Fn<T, Bool>) -> Option<T> =
        fold(list_reverse<T>(xs), none(T), fun(acc: Option<T>, x: T, if_then_else(p(x), some(x), acc)))
    fn list_take<T>(xs: List<T>, n: Nat) -> List<T> decreasing n =
        if_then_else(nat_eq(n, 0), empty_list(T), if_then_else(is_empty(xs), empty_list(T),
            match head(xs) { none => empty_list(T), some(h) => cons(h, list_take<T>(tail(xs), pred(n))) }))
    fn list_drop<T>(xs: List<T>, n: Nat) -> List<T> decreasing n =
        if_then_else(nat_eq(n, 0), xs, if_then_else(is_empty(xs), xs, list_drop<T>(tail(xs), pred(n))))
    fn list_last<T>(xs: List<T>) -> Option<T> = head(list_reverse<T>(xs))
    fn list_contains<T>(xs: List<T>, x: T, same: Fn<T, T, Bool>) -> Bool = fold(xs, false, fun(acc: Bool, y: T, or(acc, same(x, y))))
    fn list_zip<A, B>(xs: List<A>, ys: List<B>) -> List<Duo<A, B>> decreasing xs =
        if_then_else(is_empty(xs), empty_list(Duo<A, B>), match head(xs) { none => empty_list(Duo<A, B>), some(x) => match head(ys) { none => empty_list(Duo<A, B>), some(y) =>
            cons(Duo<A, B>(x, y), list_zip<A, B>(tail(xs), tail(ys))) } })
    fn list_max_by<T>(xs: List<T>, greater: Fn<T, T, Bool>) -> Option<T> =
        fold(xs, none(T), fun(acc: Option<T>, x: T, match acc { none => some(x), some(m) => if_then_else(greater(x, m), some(x), some(m)) }))
    // insertion sort by a strict order `less`
    fn list_insert_by<T>(xs: List<T>, x: T, before: Fn<T, T, Bool>) -> List<T> decreasing xs =
        if_then_else(is_empty(xs), list(x), match head(xs) { none => list(x), some(h) => if_then_else(before(h, x), cons(h, list_insert_by<T>(tail(xs), x, before)), cons(x, xs)) })
    fn list_sort_by<T>(xs: List<T>, before: Fn<T, T, Bool>) -> List<T> decreasing xs =
        if_then_else(is_empty(xs), xs, match head(xs) { none => xs, some(h) => list_insert_by<T>(list_sort_by<T>(tail(xs), before), h, before) })

    fn stack_new<T>(x: T) -> Stack<T> = Stack<T>(empty_list(T))
    fn stack_push<T>(s: Stack<T>, x: T) -> Stack<T> = Stack<T>(cons(x, field(s, items)))
    fn stack_peek<T>(s: Stack<T>) -> Option<T> = head(field(s, items))
    fn stack_pop<T>(s: Stack<T>) -> Stack<T> = Stack<T>(tail(field(s, items)))
    fn stack_size<T>(s: Stack<T>) -> Nat = length(field(s, items))

    fn queue_empty<T>(x: T) -> Queue<T> = Queue<T>(empty_list(T), empty_list(T))
    fn queue_push<T>(q: Queue<T>, x: T) -> Queue<T> = Queue<T>(field(q, front), cons(x, field(q, back)))
    private fn queue_norm<T>(q: Queue<T>) -> Queue<T> =
        if_then_else(is_empty(field(q, front)), Queue<T>(list_reverse<T>(field(q, back)), empty_list(T)), q)
    fn queue_peek<T>(q: Queue<T>) -> Option<T> = head(field(queue_norm<T>(q), front))
    fn queue_pop<T>(q: Queue<T>) -> Queue<T> = let n = queue_norm<T>(q) in Queue<T>(tail(field(n, front)), field(n, back))
    fn queue_size<T>(q: Queue<T>) -> Nat = length(field(q, front)) + length(field(q, back))

    fn map_empty<K, V>(k: K, v: V) -> AssocMap<K, V> = AssocMap<K, V>(empty_list(Entry<K, V>))
    fn map_get<K, V>(m: AssocMap<K, V>, k: K, same: Fn<K, K, Bool>) -> Option<V> =
        fold(list_reverse<Entry<K, V>>(field(m, entries)), none(V), fun(acc: Option<V>, e: Entry<K, V>, if_then_else(same(field(e, key), k), some(field(e, value)), acc)))
    fn map_remove<K, V>(m: AssocMap<K, V>, k: K, same: Fn<K, K, Bool>) -> AssocMap<K, V> =
        AssocMap<K, V>(list_filter<Entry<K, V>>(field(m, entries), fun(e: Entry<K, V>, not(same(field(e, key), k)))))
    fn map_put<K, V>(m: AssocMap<K, V>, k: K, v: V, same: Fn<K, K, Bool>) -> AssocMap<K, V> =
        AssocMap<K, V>(cons(Entry<K, V>(k, v), field(map_remove<K, V>(m, k, same), entries)))
    fn map_size<K, V>(m: AssocMap<K, V>) -> Nat = length(field(m, entries))
    fn map_keys<K, V>(m: AssocMap<K, V>) -> List<K> = map(field(m, entries), fun(e: Entry<K, V>, field(e, key)))

    // v0.111: bounded variants (Eq: structural `equal`; Ord: `less` on Nat, Int, Text) and inferred type arguments
    fn list_contains_eq<T: Eq>(xs: List<T>, x: T) -> Bool = list_any(xs, fun(y: T, equal(x, y)))
    fn list_index_of<T: Eq>(xs: List<T>, x: T) -> Option<Nat> =
        fst(fold(xs, pair(none(Nat), 0), fun(acc: ProductType<Option<Nat>*Nat>, y: T,
            pair(if_then_else(and(is_none(fst(acc)), equal(x, y)), some(snd(acc)), fst(acc)), snd(acc) + 1))))
    fn list_dedup<T: Eq>(xs: List<T>) -> List<T> =
        list_reverse(fold(xs, empty_list(T), fun(acc: List<T>, x: T, if_then_else(list_contains_eq(acc, x), acc, cons(x, acc)))))
    fn list_sort<T: Ord>(xs: List<T>) -> List<T> = list_sort_by(xs, fun(a: T, b: T, less(a, b)))
    fn list_min<T: Ord>(xs: List<T>) -> Option<T> = list_max_by(xs, fun(a: T, b: T, less(a, b)))
    fn list_max<T: Ord>(xs: List<T>) -> Option<T> = list_max_by(xs, fun(a: T, b: T, less(b, a)))
    fn map_get_eq<K: Eq, V>(m: AssocMap<K, V>, k: K) -> Option<V> = map_get(m, k, fun(a: K, b: K, equal(a, b)))
    fn map_put_eq<K: Eq, V>(m: AssocMap<K, V>, k: K, v: V) -> AssocMap<K, V> = map_put(m, k, v, fun(a: K, b: K, equal(a, b)))

    theorem reverse_of_three: nat_eq(unwrap_or(head(list_reverse<Nat>(list(1, 2, 3))), 0), 3)
}
