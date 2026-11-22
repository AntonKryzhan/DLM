// Effects at the boundary: fonts, native window, polling, presentation and close.
// Frames are drawn by the GPU (OpenGL, `win_gpu` + `win_present_scene`); the software
// rasterizer remains the fallback when no OpenGL driver is available or `--cpu` is given.
module universe.window
import universe.view

theory UniverseWindow extends UniverseView {
    record UWindow(app: UApp, width: Nat, height: Nat, fonts: NatVec, frame: NatVec, dirty: Bool)
    fn u_font_id(name: Text) -> Nat = match file_font_find(name) { err(e) => 0, ok(path) => match file_font_load(path) { err(e) => 0, ok(id) => id } }
    fn u_fonts() -> NatVec =
        let segoe = u_font_id("segoeui.ttf") in if_then_else(segoe > 0, nvl(list(segoe)),
            let arial = u_font_id("arial.ttf") in if_then_else(arial > 0, nvl(list(arial)),
                let dejavu = u_font_id("DejaVuSans.ttf") in if_then_else(dejavu > 0, nvl(list(dejavu)), nv_empty())))
    fn u_on_canvas(x: Nat, y: Nat) -> Bool = inside(Rect(24, 100, 864, 605), x, y)
    fn u_world_pointer(a: UApp, x: Nat, y: Nat) -> UVec =
        let c = field(a, camera) in UVec(
            fl_add(field(c, x), fl_div(fl_sub(fl_div(fl_sub(fl_of_nat(x), 24.0), 0.864), 500.0), field(c, zoom))),
            fl_add(field(c, y), fl_div(fl_sub(fl_div(fl_sub(fl_of_nat(y), 100.0), 0.864), 350.0), field(c, zoom))))
    fn u_point_at(a: UApp, x: Nat, y: Nat) -> UApp =
        let p = field(a, pointer) in
        if_then_else(u_on_canvas(x, y), let v = u_world_pointer(a, x, y) in u_with_pointer(a, UPointer(field(v, x), field(v, y), field(p, button), true)),
            u_with_pointer(a, UPointer(field(p, x), field(p, y), field(p, button), false)))
    fn u_zoom(a: UApp, x: Nat, y: Nat, direction: Nat) -> UApp =
        if_then_else(not(and(u_on_canvas(x, y), or(nat_eq(direction, 1), nat_eq(direction, 2)))), a,
            let c = field(a, camera) in let p = u_world_pointer(a, x, y) in
            let z = u_clamp(fl_mul(field(c, zoom), if_then_else(nat_eq(direction, 1), 1.16, 0.8620689655172413)), 0.65, u_zoom_limit(a)) in
            let ratio = fl_div(field(c, zoom), z) in
            u_with_camera(a, UCamera(fl_sub(field(p, x), fl_mul(fl_sub(field(p, x), field(c, x)), ratio)), fl_sub(field(p, y), fl_mul(fl_sub(field(p, y), field(c, y)), ratio)), z, false)))
    fn u_pan(a: UApp, code: Nat) -> UApp =
        let c = field(a, camera) in let amount = fl_div(32.0, field(c, zoom)) in
        let dx = if_then_else(nat_eq(code, 130897), fl_neg(amount), if_then_else(nat_eq(code, 130899), amount, 0.0)) in
        let dy = if_then_else(nat_eq(code, 130898), fl_neg(amount), if_then_else(nat_eq(code, 130900), amount, 0.0)) in
        if_then_else(and(fl_eq(dx, 0.0), fl_eq(dy, 0.0)), a,
            u_with_camera(a, UCamera(u_clamp(fl_add(field(c, x), dx), -500.0, 1500.0), u_clamp(fl_add(field(c, y), dy), -350.0, 1050.0), field(c, zoom), false)))
    fn u_event_app(a: UApp, kind: Nat, x: Nat, y: Nat, button: Nat) -> UApp =
        let hit = u_hit(a, x, y) in
        if_then_else(nat_eq(kind, 5), u_pan(u_key(a, x), x),
        if_then_else(nat_eq(kind, 8), u_zoom(a, x, y, button),
        if_then_else(nat_eq(kind, 4), u_with_hover(u_point_at(a, x, y), hit, if_then_else(text_eq(hit, field(a, pressed)), field(a, pressed), "")),
        if_then_else(nat_eq(kind, 2),
            if_then_else(u_on_canvas(x, y), let b = u_point_at(a, x, y) in let p = field(b, pointer) in
                u_with_hover(u_with_pointer(b, UPointer(field(p, x), field(p, y), if_then_else(nat_eq(button, 2), 0, button), true)), "", ""),
                u_with_hover(u_point_at(a, x, y), hit, if_then_else(nat_eq(button, 1), hit, ""))),
        if_then_else(nat_eq(kind, 3),
            let b = if_then_else(and(nat_eq(button, 1), and(text_length(hit) > 0, text_eq(hit, field(a, pressed)))), u_action(a, hit), a) in
            u_with_hover(u_point_at(b, x, y), hit, ""), a)))))
    fn u_shell_changed(a: UApp, b: UApp) -> Bool =
        or(not(and(and(equal(field(a, settings), field(b, settings)), text_eq(field(a, hover), field(b, hover))),
            and(text_eq(field(a, pressed), field(b, pressed)), equal(field(field(a, camera), automatic), field(field(b, camera), automatic))))),
            or(not(nat_eq(field(field(a, world), mode), field(field(b, world), mode))), field(field(a, world), tick) > field(field(b, world), tick)))
    fn u_event(s: UWindow, event: NatVec) -> UWindow =
        let kind = unwrap_or(nv_get(event, 0), 0) in let a = unwrap_or(nv_get(event, 1), 0) in let b = unwrap_or(nv_get(event, 2), 0) in let c = unwrap_or(nv_get(event, 3), 0) in
        if_then_else(and(nat_eq(kind, 6), and(a > 0, b > 0)),
            UWindow(u_with_hover(field(s, app), "", ""), a, b, field(s, fonts), field(s, frame), true),
            let scale = u_scale(field(s, width), field(s, height)) in let wx = div(mul(a, 1000), scale) in let wy = div(mul(b, 1000), scale) in
            let ox = u_ox(field(s, width), field(s, height)) in let oy = u_oy(field(s, width), field(s, height)) in
            let key = nat_eq(kind, 5) in
            let next = u_event_app(field(s, app), kind, if_then_else(key, a, if_then_else(ox > wx, 0, monus(wx, ox))), if_then_else(oy > wy, 0, monus(wy, oy)), c) in
            UWindow(next, field(s, width), field(s, height), field(s, fonts), field(s, frame), or(field(s, dirty), u_shell_changed(field(s, app), next))))
    fn u_dynamic_image(s: UWindow, a: UApp) -> Option<NatVec> =
        let w = field(s, width) in let h = field(s, height) in let scale = u_scale(w, h) in let ox = u_ox(w, h) in let oy = u_oy(w, h) in
        let x = monus(div(mul(ox + 24, scale), 1000), 1) in let y = monus(div(mul(oy + 100, scale), 1000), 1) in
        let right = div(mul(ox + 888, scale) + 999, 1000) + 1 in let bottom = div(mul(oy + 705, scale) + 999, 1000) + 1 in
        let world_w = div(mul(w, 1000) + pred(scale), scale) in let world_h = div(mul(h, 1000) + pred(scale), scale) in
        let scene = u_join(list(rrect(0, 0, px(world_w), px(world_h), 0, solid(u_bg())),
            rrect(px(ox + 23), px(oy + 99), px(866), px(607), px(14), solid(607472383)), u_canvas(a, field(s, fonts), ox, oy))) in
        vx_render_region(field(s, frame), scene, scale, 1000, x, y, monus(right, x), monus(bottom, y))
    fn u_image(s: UWindow, a: UApp) -> Option<NatVec> =
        let fresh = or(field(s, dirty), or(nat_eq(mod(field(field(a, world), tick), 30), 0), not(nat_eq(field(field(a, world), mode), field(field(field(s, app), world), mode))))) in
        if_then_else(or(fresh, nat_eq(nv_len(field(s, frame)), 0)), vx_render(u_scene(a, field(s, fonts), field(s, width), field(s, height)), field(s, width), field(s, height), u_scale(field(s, width), field(s, height)), 1000), u_dynamic_image(s, a))
    // GPU: the whole vector scene goes to OpenGL every frame (no CPU raster, no dirty regions)
    fn u_gpu_frame(id: Nat, s: UWindow, next: UApp) -> Result<UWindow, Text> =
        let w = field(s, width) in let h = field(s, height) in
        match win_present_scene(id, u_scene(next, field(s, fonts), w, h), w, h, u_scale(w, h), 1000) {
            err(message) => err(message, UWindow),
            ok(n) => ok(UWindow(next, w, h, field(s, fonts), field(s, frame), false), Text)
        }
    fn u_frame(id: Nat, s: UWindow, gpu: Bool) -> Result<UWindow, Text> =
        let next = u_tick(field(s, app)) in
        if_then_else(and(not(field(s, dirty)), field(field(next, settings), paused)), ok(s, Text),
            if_then_else(gpu, u_gpu_frame(id, s, next), match u_image(s, next) {
                none => err("Не удалось отрисовать кадр", UWindow),
                some(frame) => match win_present(id, frame) { err(message) => err(message, UWindow), ok(n) => ok(UWindow(next, field(s, width), field(s, height), field(s, fonts), frame, false), Text) }
            }))
    fn u_loop(id: Nat, s: UWindow, burst: Nat, gpu: Bool) -> Result<UApp, Text> loop =
        // A bounded event burst prevents motion events from starving animation.
        if_then_else(not(32 > burst), match u_frame(id, s, gpu) { err(e) => err(e, UApp), ok(next) => u_loop(id, next, 0, gpu) },
            match win_poll(id, if_then_else(field(field(field(s, app), settings), paused), 50, 16)) {
                err(e) => err(e, UApp),
                ok(event) => let kind = unwrap_or(nv_get(event, 0), 0) in let key = unwrap_or(nv_get(event, 1), 0) in
                    if_then_else(or(nat_eq(kind, 1), and(nat_eq(kind, 5), or(nat_eq(key, 27), nat_eq(key, 130843)))), ok(field(s, app), Text),
                    if_then_else(nat_eq(kind, 0), match u_frame(id, s, gpu) { err(e) => err(e, UApp), ok(next) => u_loop(id, next, 0, gpu) },
                        // Win32 WM_PAINT restores its cached image and queues event 7.
                        // Re-presenting in response would generate another WM_PAINT.
                        u_loop(id, if_then_else(nat_eq(kind, 7), s, u_event(s, event)), burst + 1, gpu)))
            })
    // `gpu`: try OpenGL first; the software renderer is used when it is unavailable
    fn u_run_on(count: Nat, seed: Nat, scale: Nat, gpu: Bool) -> Result<UApp, Text> =
        let size = ui_max_v(50, ui_min_v(scale, 150)) in let width = div(mul(1200, size), 100) in let height = div(mul(800, size), 100) in
        let fonts = u_fonts() in let initial = u_initial(count, seed) in
        match win_open("DLM Universe", width, height) {
            err(e) => err(e, UApp),
            ok(id) => let accelerated = and(gpu, match win_gpu(id) { ok(info) => true, err(e) => false }) in
                let starting = if_then_else(accelerated, u_enable_gpu(initial, true), u_initial(ui_min_v(count, 400), seed)) in
                let result = u_loop(id, UWindow(starting, width, height, fonts, nv_empty(), true), 32, accelerated) in
                let closed = win_close(id) in
                match result { err(e) => err(e, UApp), ok(a) => match closed { err(e) => err(e, UApp), ok(n) => ok(a, Text) } }
        }
    fn u_run(count: Nat, seed: Nat, scale: Nat) -> Result<UApp, Text> = u_run_on(count, seed, scale, true)
}
