module record_schemas

theory Records {
    record Point(x: Nat, label: Text)
    record Batch(points: List<Point>, fallback: Option<Point>)

    fn first(b: Batch) -> Point =
        unwrap_or(head(field(b, points)),
                  unwrap_or(field(b, fallback), Point(0, "empty")))

    fn show(p: Point) -> Text =
        concat(field(p, label), concat(": ", nat_to_text(field(p, x))))

    print_line(show(first(Batch(list(Point(3, "three")), none(Point)))))
    print_line(show(first(Batch(empty_list(Point), some(Point(7, "fallback"))))))
}
