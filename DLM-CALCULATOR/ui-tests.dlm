// Headless checks: native event format, scaled hit testing and incremental frames.
module calculator.ui_tests
import calculator.window

theory CalculatorUiTests extends CalculatorWindow {
    record CalcUiCheck(name: Text, passed: Bool)
    fn cc_ui_test(failures: Nat, test: CalcUiCheck) -> Nat =
        let line = print_line(concat(if_then_else(field(test, passed), "PASS ", "FAIL "), field(test, name))) in
        failures + if_then_else(field(test, passed), 0, 1)
    fn cc_ui_blank(w: Nat, h: Nat, fonts: NatVec) -> CalcWindow = CalcWindow(cc_initial(), "", "", w, h, fonts, nv_empty(), empty_list(NatVec))
    fn cc_ui_step(s: CalcWindow, kind: Nat, a: Nat, b: Nat, c: Nat) -> CalcWindow =
        match cc_handle(s, nvl(list(kind, a, b, c))) {
            ok(next) => next,
            err(message) => CalcWindow(cc_put(field(s, model), cc_failed(message)), "", "", field(s, width), field(s, height), field(s, fonts), nv_empty(), empty_list(NatVec))
        }
    fn cc_ui_entry(s: CalcWindow) -> Text = field(field(field(s, model), engine), entry)
    fn main() -> Nat =
        let fonts = cc_load_fonts() in
        let blank = cc_ui_blank(220, 360, fonts) in
        match cc_patch(blank, cc_initial(), "", "", 220, 360) {
            err(message) => let line = print_line(message) in 1,
            ok(initial) =>
                let down = cc_ui_step(initial, 2, 33, 243, 1) in
                let up = cc_ui_step(down, 3, 33, 243, 1) in
                let cancelled = cc_ui_step(cc_ui_step(down, 4, 0, 0, 0), 3, 33, 243, 1) in
                let resized = cc_ui_step(up, 6, 440, 720, 0) in
                let full = unwrap_or(vx_render(cat_all(field(up, chunks)), 220, 360, 500, 1000), nv_empty()) in
                let text = "−1234567890123456" in
                let tests = list(
                    CalcUiCheck("initial frame is rendered", nv_len(field(initial, frame)) > 0),
                    CalcUiCheck("disabled memory has no hit", text_eq(cc_hit(cc_initial(), 56, 219), "")),
                    CalcUiCheck("digit hit", text_eq(cc_hit(cc_initial(), 67, 377), "7")),
                    CalcUiCheck("button gap has no hit", text_eq(cc_hit(cc_initial(), 116, 377), "")),
                    CalcUiCheck("empty history has no hit", text_eq(cc_hit(cc_initial(), 35, 620), "")),
                    CalcUiCheck("half scale hit", text_eq(cc_hit_pixel(initial, 33, 243), "1")),
                    CalcUiCheck("wide window centered hit", text_eq(cc_hit_pixel(cc_ui_blank(660, 720, fonts), 177, 485), "1")),
                    CalcUiCheck("wide window margin", text_eq(cc_hit_pixel(cc_ui_blank(660, 720, fonts), 50, 485), "")),
                    CalcUiCheck("tall window centered hit", text_eq(cc_hit_pixel(cc_ui_blank(440, 900, fonts), 67, 575), "1")),
                    CalcUiCheck("press waits for release", and(text_eq(cc_ui_entry(down), "0"), text_eq(field(down, pressed), "1"))),
                    CalcUiCheck("release activates digit", and(text_eq(cc_ui_entry(up), "1"), text_eq(field(up, pressed), ""))),
                    CalcUiCheck("release without press ignored", text_eq(cc_ui_entry(cc_ui_step(initial, 3, 33, 243, 1)), "0")),
                    CalcUiCheck("drag away cancels click", text_eq(cc_ui_entry(cancelled), "0")),
                    CalcUiCheck("right click ignored", equal(cc_ui_step(initial, 2, 33, 243, 3), initial)),
                    CalcUiCheck("unknown event ignored", equal(cc_ui_step(initial, 99, 0, 0, 0), initial)),
                    CalcUiCheck("native Escape clears", text_eq(cc_ui_entry(cc_ui_step(up, 5, 130843, 0, 0)), "0")),
                    CalcUiCheck("resize preserves calculation", and(text_eq(cc_ui_entry(resized), "1"), and(nat_eq(field(resized, width), 440), nat_eq(field(resized, height), 720)))),
                    CalcUiCheck("incremental frame equals full render", and(nv_len(full) > 0, equal(full, field(up, frame)))),
                    CalcUiCheck("long entry fits display", not(vx_font_width(fonts, text, px(cc_entry_size(fonts, text))) > px(368))),
                    CalcUiCheck("model invariant after events", cc_invariant(field(resized, model)))) in
                let failed = fold_effect(tests, 0, cc_ui_test) in
                let line = print_line(concat("UI failures: ", nat_to_text(failed))) in failed
        }
}
