module examples.private_declaration_not_visible
import std.nat

theory App extends StdNat {
    fn sneaky(a: Nat) -> Nat = add_acc(a, 1)
}
