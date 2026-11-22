module universe.ui_tests
import universe.window

theory UniverseUiTests extends UniverseWindow {
    record UUiCheck(name: Text, passed: Bool)
    fn u_ui_check(failures: Nat, test: UUiCheck) -> Nat =
        let line = print_line(concat(if_then_else(field(test, passed), "PASS ", "FAIL "), field(test, name))) in failures + if_then_else(field(test, passed), 0, 1)
    fn u_ui_near(x: Float, y: Float) -> Bool = fl_le(fl_abs(fl_sub(x, y)), 0.0000001)
    fn u_ui_event(s: UWindow, kind: Nat, a: Nat, b: Nat, c: Nat) -> UWindow = u_event(s, nvl(list(kind, a, b, c)))
    fn main() -> Nat =
        let a = u_initial(40, 7291) in let initial = UWindow(a, 1200, 800, nv_empty(), nv_empty(), false) in
        let half = UWindow(a, 600, 400, nv_empty(), nv_empty(), false) in
        let down = u_ui_event(initial, 2, 970, 490, 1) in let up = u_ui_event(down, 3, 970, 490, 1) in
        let cancelled = u_ui_event(u_ui_event(down, 4, 0, 0, 0), 3, 970, 490, 1) in
        let source = u_ui_event(initial, 2, 456, 402, 1) in
        let zoomed = u_zoom(a, 250, 250, 1) in let before = u_world_pointer(a, 250, 250) in let after = u_world_pointer(zoomed, 250, 250) in
        let commands = map(u_indices(100), fun(i: Nat, nvl(list(i, i + 1, i + 2)))) in
        let fonts = u_fonts() in
        let scene = u_scene(a, fonts, 300, 200) in let frame = vx_render(scene, 300, 200, 250, 1000) in
        let cached = UWindow(a, 300, 200, fonts, unwrap_or(frame, nv_empty()), false) in let next = u_tick(a) in
        let dynamic = u_dynamic_image(cached, next) in
        let reference = vx_render_region(unwrap_or(frame, nv_empty()), u_scene(next, fonts, 300, 200), 250, 1000, 5, 24, 218, 154) in
        let checks = list(
            UUiCheck("balanced scene concatenation preserves bytes", equal(u_join(commands), cat_all(commands))),
            UUiCheck("empty scene concatenation", nat_eq(nv_len(u_join(empty_list(NatVec))), 0)),
            UUiCheck("eight scene buttons hit correct modes", fold(u_indices(8), true, fun(ok: Bool, i: Nat, and(ok, text_eq(u_hit(a, 950, 165 + mul(i, 31)), concat("m", nat_to_text(i))))))),
            UUiCheck("field quality label is resolution", text_eq(u_quality_label(u_select(a, 6)), "Поле: 256×192")),
            UUiCheck("GPU flags preserve compact 15 operand protocol", nat_eq(nv_len(u_gpu_particles(u_enable_gpu(a, true), 0, 0)), 16)),
            UUiCheck("cosmic local zoom clamps at eight", let local = u_select(a, 5) in let b = fold(u_indices(80), local, fun(c: UApp, i: Nat, u_zoom(c, 400, 400, 1))) in and(u_invariant(b), u_ui_near(field(field(b, camera), zoom), 8.0))),
            UUiCheck("horizon view supports thirty-two fold zoom", let global = u_action(u_select(a, 5), "fields") in let b = fold(u_indices(80), global, fun(c: UApp, i: Nat, u_zoom(c, 400, 400, 1))) in and(u_invariant(b), u_ui_near(field(field(b, camera), zoom), 32.0))),
            UUiCheck("scale change preserves catalogue and refits camera", let local = u_select(a, 5) in let global = u_action(local, "fields") in and(equal(field(local, world), field(global, world)), u_ui_near(field(field(global, camera), zoom), 16.0))),
            UUiCheck("button hit", text_eq(u_hit(a, 970, 490), "pause")),
            UUiCheck("button gap has no hit", text_eq(u_hit(a, 1043, 490), "")),
            UUiCheck("canvas excludes sidebar", not(u_on_canvas(940, 490))),
            UUiCheck("press waits for release", not(field(field(field(down, app), settings), paused))),
            UUiCheck("release triggers action", field(field(field(up, app), settings), paused)),
            UUiCheck("release without press ignored", not(field(field(field(u_ui_event(initial, 3, 970, 490, 1), app), settings), paused))),
            UUiCheck("drag out cancels action", not(field(field(field(cancelled, app), settings), paused))),
            UUiCheck("right click cannot activate UI", not(field(field(field(u_ui_event(u_ui_event(initial, 2, 970, 490, 3), 3, 970, 490, 3), app), settings), paused))),
            UUiCheck("half scale hit maps to same button", field(field(field(u_ui_event(u_ui_event(half, 2, 485, 245, 1), 3, 485, 245, 1), app), settings), paused)),
            UUiCheck("horizontal letterbox offsets hit", let s = UWindow(a, 1400, 800, nv_empty(), nv_empty(), false) in field(field(field(u_ui_event(u_ui_event(s, 2, 1070, 490, 1), 3, 1070, 490, 1), app), settings), paused)),
            UUiCheck("vertical letterbox offsets hit", let s = UWindow(a, 1200, 1000, nv_empty(), nv_empty(), false) in field(field(field(u_ui_event(u_ui_event(s, 2, 970, 590, 1), 3, 970, 590, 1), app), settings), paused)),
            UUiCheck("attraction source set", nat_eq(field(field(field(source, app), pointer), button), 1)),
            UUiCheck("repulsion source set", nat_eq(field(field(field(u_ui_event(initial, 2, 456, 402, 3), app), pointer), button), 3)),
            UUiCheck("middle button clears source", nat_eq(field(field(field(u_ui_event(source, 2, 456, 402, 2), app), pointer), button), 0)),
            UUiCheck("X clears source on Win32", nat_eq(field(field(u_key(field(source, app), 88), pointer), button), 0)),
            UUiCheck("source leaves canvas safely", not(field(field(field(u_ui_event(source, 4, 950, 490, 0), app), pointer), inside))),
            UUiCheck("source release stays placed", nat_eq(field(field(field(u_ui_event(source, 3, 456, 402, 1), app), pointer), button), 1)),
            UUiCheck("wheel preserves world point at cursor", and(u_ui_near(field(before, x), field(after, x)), u_ui_near(field(before, y), field(after, y)))),
            UUiCheck("wheel switches camera to manual", not(field(field(zoomed, camera), automatic))),
            UUiCheck("wheel on sidebar ignored", equal(u_zoom(a, 950, 300, 1), a)),
            UUiCheck("maximum zoom clamps", u_ui_near(field(field(fold(u_indices(80), a, fun(b: UApp, i: Nat, u_zoom(b, 400, 400, 1))), camera), zoom), 3.0)),
            UUiCheck("minimum zoom clamps", u_ui_near(field(field(fold(u_indices(80), a, fun(b: UApp, i: Nat, u_zoom(b, 400, 400, 2))), camera), zoom), 0.65)),
            UUiCheck("camera reset restores auto", field(field(u_action(zoomed, "camera"), camera), automatic)),
            UUiCheck("arrow key pans left", fl_lt(field(field(u_pan(a, 130897), camera), x), 500.0)),
            UUiCheck("pan clamps bounds", fl_le(-500.0, field(field(fold(u_indices(80), a, fun(b: UApp, i: Nat, u_pan(b, 130897))), camera), x))),
            UUiCheck("native key preserves key code under scale", field(field(field(u_ui_event(half, 5, 32, 0, 0), app), settings), paused)),
            UUiCheck("resize preserves simulation", let s = u_ui_event(initial, 6, 800, 600, 0) in and(equal(field(s, app), a), and(nat_eq(field(s, width), 800), nat_eq(field(s, height), 600)))),
            UUiCheck("zero size event ignored", equal(field(u_ui_event(initial, 6, 0, 0, 0), app), a)),
            UUiCheck("valid vector frame", is_some(frame)),
            UUiCheck("raster width matches target", nat_eq(gx_width(unwrap_or(frame, nv_empty())), 300)),
            UUiCheck("raster height matches target", nat_eq(gx_height(unwrap_or(frame, nv_empty())), 200)),
            UUiCheck("frame has opaque background", nat_eq(mod(unwrap_or(gx_get(unwrap_or(frame, nv_empty()), 0, 0), 0), 256), 255)),
            UUiCheck("dynamic frame matches full scene in dirty region", and(is_some(dynamic), equal(dynamic, reference))),
            UUiCheck("canvas pointer does not invalidate shell", not(field(u_ui_event(initial, 4, 456, 402, 0), dirty))),
            UUiCheck("UI action invalidates shell", field(up, dirty))) in
        let failed = fold_effect(checks, 0, u_ui_check) in
        let line = print_line(concat("UI failures: ", nat_to_text(failed))) in failed
}
