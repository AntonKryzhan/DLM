module examples.effects
import std.effects

theory EffectDemo extends StdEffects {
    fn logger(prefix: Text) -> FnEffect<Stdout, Nat, Text> =
        effect_fn(FnEffect(Stdout, Nat, Text), fun(n: Nat,
            let line = concat(prefix, nat_to_text(n)) in let log = print_line(line) in line))
    let action = logger("item=")
    print_line(join(effect_map(list(1, 2, 3), action), ", "))
    let is_two = effect_fn(FnEffect(Stdout, Nat, Bool), fun(n: Nat,
        let log = print_decimal(n) in nat_eq(n, 2)))
    print_decimal(unwrap_or(effect_find(list(1, 2, 3), is_two), 0))
}
