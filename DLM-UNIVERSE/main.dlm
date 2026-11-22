module universe.main
import universe.window

theory UniverseApp extends UniverseWindow {
    // Options: --count N --seed N --scale PERCENT --cpu, in any order.
    fn u_option(xs: List<Text>, key: Text, default: Nat) -> Nat decreasing xs =
        match xs { [] => default, cons(h, rest) => if_then_else(text_eq(h, key), unwrap_or(text_to_nat(unwrap_or(head(rest), "")), default), u_option(rest, key, default)) }
    fn u_flag(xs: List<Text>, key: Text) -> Bool = fold(xs, false, fun(found: Bool, x: Text, or(found, text_eq(x, key))))
    fn main() -> Nat =
        let options = args() in
        match u_run_on(u_option(options, "--count", 240), u_option(options, "--seed", 7291), u_option(options, "--scale", 100), not(u_flag(options, "--cpu"))) {
            ok(app) => 0,
            err(message) => let saved = file_write_atomic("universe-error.txt", message) in let line = print_line(message) in 1
        }
}
