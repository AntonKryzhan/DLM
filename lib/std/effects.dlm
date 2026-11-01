// Ordered callbacks with explicit effects. Ordinary Fn stays pure and total.
module std.effects
import std.collections

theory StdEffects extends StdCollections {
    fn effect_call<F: UnaryCallable>(value: ArgOf<F>, action: F) -> ResultOf<F> = action(value)
    fn effect_run<F: NullaryCallable>(action: F) -> ResultOf<F> = action()
    fn effect_each<T, F: UnaryCallable>(xs: List<T>, action: F) -> Nat = each_effect(xs, action)
    fn effect_map<F: UnaryCallable>(xs: List<ArgOf<F>>, action: F) -> List<ResultOf<F>> = map_effect(xs, action)
    fn effect_fold<T, S, F: Callable>(xs: List<T>, state: S, step: F) -> S = fold_effect(xs, state, step)
    fn effect_filter<T, F: UnaryCallable>(xs: List<T>, keep: F) -> List<T> = filter_effect(xs, keep)
    fn effect_find<T, F: UnaryCallable>(xs: List<T>, keep: F) -> Option<T> = find_effect(xs, keep)
    fn effect_any<T, F: UnaryCallable>(xs: List<T>, keep: F) -> Bool = any_effect(xs, keep)
    fn effect_all<T, F: UnaryCallable>(xs: List<T>, keep: F) -> Bool = all_effect(xs, keep)
    fn effect_on_some<F: UnaryCallable>(value: Option<ArgOf<F>>, action: F) -> Option<ResultOf<F>> =
        match value { none => none(ResultOf(F)), some(x) => some(action(x)) }
    fn effect_on_ok<F: UnaryCallable, E>(value: Result<ArgOf<F>, E>, action: F) -> Result<ResultOf<F>, E> =
        match value { err(e) => err(e, ResultOf(F)), ok(x) => ok(action(x), E) }
    fn effect_try_map<F: UnaryCallable>(xs: List<ArgOf<F>>, action: F) -> Result<List<OkOf<ResultOf<F>>>, ErrorOf<ResultOf<F>>> = try_map_effect(xs, action)
    fn effect_try_each<F: UnaryCallable>(xs: List<ArgOf<F>>, action: F) -> Result<Nat, ErrorOf<ResultOf<F>>> = try_each_effect(xs, action)
    fn effect_collect_results<F: UnaryCallable>(xs: List<ArgOf<F>>, action: F) -> List<ResultOf<F>> = map_effect(xs, action)

    theorem effect_empty_map_has_zero_length:
        nat_eq(length(effect_map(empty_list(Nat), fun(n: Nat, n + 1))), 0)
    theorem effect_pure_application_is_identity:
        nat_eq(effect_call(7, fun(n: Nat, n)), 7)
}
