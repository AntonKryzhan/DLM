module examples.import_missing_module
import std.does_not_exist

theory App {
    fn f(n: Nat) -> Nat = n
}
