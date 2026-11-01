// v0.127 GUI toolkit (docs/GUI.md): model, view, update; events replayed headlessly,
// the screen rendered into a raster image and as HTML. To serve it in a browser:
//   let l = net_listen("127.0.0.1", 8080) ... serve_app(l, default_theme(), model, view, update, "Counter", 0, 0)
module examples.gui_counter
import std.ui

theory GuiCounter extends StdUi {
    record Model(count: Nat, dark: Bool)
    fn view(m: Model) -> Widget = Panel("Counter", Column(6, list(
        Label(concat("count: ", nat_to_text(field(m, count)))),
        Row(6, list(Button("inc", "+1"), Button("reset", "reset"))),
        Check("dark", "dark mode", field(m, dark)),
        Bar(field(m, count), 10, 120))))
    fn update(m: Model, e: UiEvent) -> Model = match e {
        Press(b) => if_then_else(text_eq(b, "inc"), Model(field(m, count) + 1, field(m, dark)), Model(0, field(m, dark))),
        Toggle(b) => Model(field(m, count), not(field(m, dark))),
        _ => m }
    let after = apply_events(Model(0, false), update, list(Press("inc"), Press("inc"), Toggle("dark")))
    let clicked = click(default_theme(), after, view, update, 20, 70)
    print_line(concat("count after events and a click: ", nat_to_text(field(clicked, count))))
    print_line(concat("screen pixels: ", nat_to_text(monus(nv_len(screenshot(default_theme(), view(clicked))), 2))))
    print_line(concat("html bytes: ", nat_to_text(text_length(to_html(default_theme(), view(clicked), "Counter")))))
}
