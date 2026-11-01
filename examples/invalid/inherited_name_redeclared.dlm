module examples.inherited_name_redeclared
import std.nat

theory App extends StdNat {
    fn double(n: Nat) -> Nat = n
}
