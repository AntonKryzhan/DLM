module examples.private_lemma_not_visible
import std.nat

theory App extends StdNat {
    theorem uses_private(a: Nat): nat_eq(add_fast(a, 0), a) by normalize using add_acc_spec
}
