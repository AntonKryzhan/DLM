// Native window loop; effects are kept outside the calculator model and vector scene.
module calculator.window
import calculator.view

theory CalculatorWindow extends CalculatorView {
    record CalcWindow(model: CalcModel, hover: Text, pressed: Text, width: Nat, height: Nat, fonts: NatVec, frame: NatVec, chunks: List<NatVec>)
    fn cc_font_id(name: Text) -> Nat = match file_font_find(name) { err(e) => 0, ok(path) => match file_font_load(path) { err(e) => 0, ok(id) => id } }
    fn cc_load_fonts() -> NatVec =
        let segoe = cc_font_id("segoeui.ttf") in
        if_then_else(segoe > 0, nvl(list(segoe)),
            let arial = cc_font_id("arial.ttf") in if_then_else(arial > 0, nvl(list(arial)),
                let dejavu = cc_font_id("DejaVuSans.ttf") in if_then_else(dejavu > 0, nvl(list(dejavu)), nv_empty())))
    fn cc_changed(s: CalcWindow, m: CalcModel, hover: Text, pressed: Text, w: Nat, h: Nat) -> Bool =
        not(and(and(equal(field(s, model), m), and(text_eq(field(s, hover), hover), text_eq(field(s, pressed), pressed))),
            and(nat_eq(field(s, width), w), nat_eq(field(s, height), h))))
    fn cc_patch(s: CalcWindow, m: CalcModel, hover: Text, pressed: Text, w: Nat, h: Nat) -> Result<CalcWindow, Text> =
        if_then_else(and(not(cc_changed(s, m, hover, pressed, w, h)), nv_len(field(s, frame)) > 0), ok(s, Text),
            let chunks = cc_chunks(m, field(s, fonts), hover, pressed, w, h) in let num = cc_scale_num(w, h) in
            if_then_else(and(and(nat_eq(field(s, width), w), nat_eq(field(s, height), h)), not(nat_eq(nv_len(field(s, frame)), 0))),
                ok(CalcWindow(m, hover, pressed, w, h, field(s, fonts), redraw(field(s, frame), field(s, chunks), chunks, num, 1000), chunks), Text),
                match vx_render(cat_all(chunks), w, h, num, 1000) {
                    none => err("Не удалось создать изображение окна", CalcWindow),
                    some(frame) => ok(CalcWindow(m, hover, pressed, w, h, field(s, fonts), frame, chunks), Text)
                }))
    fn cc_hit_pixel(s: CalcWindow, x: Nat, y: Nat) -> Text =
        let w = field(s, width) in let h = field(s, height) in let num = cc_scale_num(w, h) in
        let wx = div(mul(x, 1000), num) in let wy = div(mul(y, 1000), num) in
        let ox = cc_origin_x(w, h) in let oy = cc_origin_y(w, h) in
        if_then_else(or(ox > wx, oy > wy), "", cc_hit(field(s, model), monus(wx, ox), monus(wy, oy)))
    fn cc_handle(s: CalcWindow, event: NatVec) -> Result<CalcWindow, Text> =
        let kind = unwrap_or(nv_get(event, 0), 0) in
        let a = unwrap_or(nv_get(event, 1), 0) in let b = unwrap_or(nv_get(event, 2), 0) in let button = unwrap_or(nv_get(event, 3), 0) in
        let m = field(s, model) in let w = field(s, width) in let h = field(s, height) in
        if_then_else(and(nat_eq(kind, 2), nat_eq(button, 1)),
            let hit = cc_hit_pixel(s, a, b) in cc_patch(s, m, hit, hit, w, h),
        if_then_else(and(nat_eq(kind, 3), nat_eq(button, 1)),
            let hit = cc_hit_pixel(s, a, b) in let pressed = field(s, pressed) in
            let model = if_then_else(and(text_length(pressed) > 0, text_eq(hit, pressed)), cc_press(m, pressed), m) in
            cc_patch(s, model, hit, "", w, h),
        if_then_else(nat_eq(kind, 4),
            let hit = cc_hit_pixel(s, a, b) in
            cc_patch(s, m, hit, if_then_else(text_eq(hit, field(s, pressed)), field(s, pressed), ""), w, h),
        if_then_else(nat_eq(kind, 5), cc_patch(s, cc_key(m, a), field(s, hover), "", w, h),
        if_then_else(and(nat_eq(kind, 6), and(a > 0, b > 0)), cc_patch(s, m, "", "", a, b), ok(s, Text))))))
    fn cc_loop(id: Nat, state: CalcWindow) -> Result<CalcModel, Text> loop =
        match win_poll(id, 30) {
            err(message) => err(message, CalcModel),
            ok(event) => let kind = unwrap_or(nv_get(event, 0), 0) in
                if_then_else(nat_eq(kind, 1), ok(field(state, model), Text),
                    // WM_PAINT already presents the cached image. Presenting again
                    // for event 7 would create a feedback loop on Win32.
                    if_then_else(or(nat_eq(kind, 0), nat_eq(kind, 7)), cc_loop(id, state),
                        match cc_handle(state, event) {
                            err(message) => err(message, CalcModel),
                            ok(next) => if_then_else(cc_changed(state, field(next, model), field(next, hover), field(next, pressed), field(next, width), field(next, height)),
                                match win_present(id, field(next, frame)) {
                                    err(message) => err(message, CalcModel), ok(n) => cc_loop(id, next)
                                }, cc_loop(id, next))
                        }))
        }
    fn cc_run(scale: Nat) -> Result<CalcModel, Text> =
        let percentage = ui_max_v(50, ui_min_v(scale, 200)) in
        let width = div(mul(cc_base_w(), percentage), 100) in let height = div(mul(cc_base_h(), percentage), 100) in
        let fonts = cc_load_fonts() in
        let empty = CalcWindow(cc_initial(), "", "", width, height, fonts, nv_empty(), empty_list(NatVec)) in
        match cc_patch(empty, cc_initial(), "", "", width, height) {
            err(message) => err(message, CalcModel),
            ok(first) => match win_open("DLM — Калькулятор", width, height) {
                err(message) => err(message, CalcModel),
                ok(id) => let shown = win_present(id, field(first, frame)) in
                    let result = match shown { err(message) => err(message, CalcModel), ok(n) => cc_loop(id, first) } in
                    let closed = win_close(id) in
                    match result {
                        err(message) => err(message, CalcModel),
                        ok(model) => match closed { err(message) => err(message, CalcModel), ok(n) => ok(model, Text) }
                    }
            }
        }
}
