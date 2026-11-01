module examples.record_field_type_mismatch

theory T {
    record Point(x: Nat, y: Nat)
    let p = Point(1, true)
}
