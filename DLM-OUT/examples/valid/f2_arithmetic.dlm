module demo.f2
import std.f2

theory F2Example extends StdF2 {
    fn render(b: Bool) -> Text = if_then_else(b, "true", "false")
    print_line(render(f2_add(true, true)))
    print_line(render(f2_add(true, false)))
    print_line(render(f2_mul(true, false)))
    print_line(render(is_some(f2_inv(true))))
}
