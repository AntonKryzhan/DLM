module examples.import_cycle_a
import examples.import_cycle_b

theory CycleA {
    fn f(n: Nat) -> Nat = n
}
