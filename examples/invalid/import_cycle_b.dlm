module examples.import_cycle_b
import examples.import_cycle_a

theory CycleB {
    fn g(n: Nat) -> Nat = n
}
