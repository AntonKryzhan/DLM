module examples.ordinary_records_sums

// v0.74: nominal records and binary sums.
theory Geometry {
    record Point(x: Nat, y: Nat)
    record Segment(from: Point, to: Point)

    fn distance(a: Nat, b: Nat) -> Nat = monus(a, b) + monus(b, a)
    fn manhattan(s: Segment) -> Nat = distance(field(field(s, from), x), field(field(s, to), x)) + distance(field(field(s, from), y), field(field(s, to), y))
    fn shift(p: Point, d: Nat) -> Point = Point(field(p, x) + d, field(p, y) + d)
    fn from_nat(n: Nat) -> Nat = n
    fn from_flag(b: Bool) -> Nat = if_then_else(b, 1, 0)
    fn weight(v: Sum<Nat, Bool>) -> Nat = case(v, from_nat, from_flag)

    let origin = Point(0, 0)
    let seg = Segment(origin, Point(3, 4))
    let values = list(inl(7, Bool), inr(true, Nat), inr(false, Nat))
    let weights = map(values, weight)

    print_symbolic(seg)
    print_decimal(manhattan(seg))
    print_symbolic(shift(origin, 5))
    print_symbolic(values)
    print_symbolic(weights)
    print_symbolic(is_left(inl(1, Bool)))
}
