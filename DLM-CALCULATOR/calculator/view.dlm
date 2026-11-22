// Procedural vector scene. Coordinates are logical pixels; the renderer uses 1/16 px.
module calculator.view
import calculator.core

theory CalculatorView extends CalculatorCore {
    record CalcButton(id: Text, label: Text, x: Nat, y: Nat, w: Nat, h: Nat, kind: Nat)
    fn cc_base_w() -> Nat = 440
    fn cc_base_h() -> Nat = 720
    fn cc_scale_num(w: Nat, h: Nat) -> Nat = ui_max_v(1, ui_min_v(div(mul(w, 1000), cc_base_w()), div(mul(h, 1000), cc_base_h())))
    fn cc_origin_x(w: Nat, h: Nat) -> Nat = div(monus(div(mul(w, 1000), cc_scale_num(w, h)), cc_base_w()), 2)
    fn cc_origin_y(w: Nat, h: Nat) -> Nat = div(monus(div(mul(h, 1000), cc_scale_num(w, h)), cc_base_h()), 2)
    fn cc_bg() -> Nat = 236200703
    fn cc_panel() -> Nat = 354365695
    fn cc_text() -> Nat = 3992189695
    fn cc_muted() -> Nat = 2443361023
    fn cc_blue() -> Nat = 679338239
    fn cc_red() -> Nat = 4287468031
    fn cc_ui_text(s: Text) -> Text = replace_all(replace_all(s, ".", ","), "-", "−")
    fn cc_text_at(fonts: NatVec, x: Nat, y: Nat, size: Nat, width: Nat, color: Nat, text: Text) -> NatVec =
        font_text_cmd(px(x), px(y), px(size), px(width), fonts, solid(color), text)
    fn cc_text_right(fonts: NatVec, x: Nat, y: Nat, size: Nat, width: Nat, color: Nat, text: Text) -> NatVec =
        let used = ui_min_v(vx_font_width(fonts, text, px(size)), px(width)) in
        font_text_cmd(px(x) + monus(px(width), used), px(y), px(size), px(width), fonts, solid(color), text)
    fn cc_entry_size(fonts: NatVec, text: Text) -> Nat =
        let width = vx_font_width(fonts, text, px(42)) in
        if_then_else(width > px(368), ui_max_v(22, div(mul(42, px(368)), width)), 42)
    fn cc_text_center(fonts: NatVec, x: Nat, y: Nat, w: Nat, h: Nat, size: Nat, color: Nat, text: Text) -> NatVec =
        let used = ui_min_v(vx_font_width(fonts, text, px(size)), px(monus(w, 12))) in
        font_text_cmd(px(x) + div(monus(px(w), used), 2), px(y) + div(monus(px(h), px(size)), 2), px(size), px(monus(w, 12)), fonts, solid(color), text)
    fn cc_buttons() -> List<CalcButton> = list(
        CalcButton("MC", "MC", 20, 204, 72, 30, 4),
        CalcButton("MR", "MR", 102, 204, 72, 30, 4),
        CalcButton("MS", "MS", 184, 204, 72, 30, 4),
        CalcButton("M+", "M+", 266, 204, 72, 30, 4),
        CalcButton("M-", "M−", 348, 204, 72, 30, 4),
        CalcButton("%", "%", 20, 246, 94, 46, 0),
        CalcButton("CE", "CE", 122, 246, 94, 46, 0),
        CalcButton("C", "C", 224, 246, 94, 46, 0),
        CalcButton("back", "⌫", 326, 246, 94, 46, 0),
        CalcButton("recip", "1/x", 20, 300, 94, 46, 0),
        CalcButton("square", "x²", 122, 300, 94, 46, 0),
        CalcButton("sqrt", "√x", 224, 300, 94, 46, 0),
        CalcButton("/", "÷", 326, 300, 94, 46, 2),
        CalcButton("7", "7", 20, 354, 94, 46, 1),
        CalcButton("8", "8", 122, 354, 94, 46, 1),
        CalcButton("9", "9", 224, 354, 94, 46, 1),
        CalcButton("*", "×", 326, 354, 94, 46, 2),
        CalcButton("4", "4", 20, 408, 94, 46, 1),
        CalcButton("5", "5", 122, 408, 94, 46, 1),
        CalcButton("6", "6", 224, 408, 94, 46, 1),
        CalcButton("-", "−", 326, 408, 94, 46, 2),
        CalcButton("1", "1", 20, 462, 94, 46, 1),
        CalcButton("2", "2", 122, 462, 94, 46, 1),
        CalcButton("3", "3", 224, 462, 94, 46, 1),
        CalcButton("+", "+", 326, 462, 94, 46, 2),
        CalcButton("sign", "±", 20, 516, 94, 46, 0),
        CalcButton("0", "0", 122, 516, 94, 46, 1),
        CalcButton(".", ",", 224, 516, 94, 46, 0),
        CalcButton("=", "=", 326, 516, 94, 46, 3),
        CalcButton("history-clear", "Очистить", 350, 584, 58, 20, 5))
    fn cc_disabled(m: CalcModel, b: CalcButton) -> Bool =
        let id = field(b, id) in
        if_then_else(or(text_eq(id, "MC"), text_eq(id, "MR")), not(field(m, has_memory)),
        if_then_else(text_eq(id, "history-clear"), is_empty(field(m, history)),
            and(text_length(field(field(m, engine), error)) > 0,
                not(or(nat_eq(field(b, kind), 1), or(or(text_eq(id, "C"), text_eq(id, "CE")), or(or(text_eq(id, "back"), text_eq(id, "sign")), text_eq(id, "."))))))))
    fn cc_button_color(b: CalcButton) -> Nat =
        if_then_else(nat_eq(field(b, kind), 3), cc_blue(),
        if_then_else(nat_eq(field(b, kind), 2), 456479743,
        if_then_else(nat_eq(field(b, kind), 1), 641093375, 455687935)))
    fn cc_button_chunk(m: CalcModel, fonts: NatVec, hover: Text, pressed: Text, b: CalcButton, ox: Nat, oy: Nat) -> NatVec =
        let id = field(b, id) in let disabled = cc_disabled(m, b) in
        let down = and(not(disabled), text_eq(pressed, id)) in let hot = and(not(disabled), text_eq(hover, id)) in
        let selected = and(nat_eq(field(b, kind), 2), text_eq(field(field(m, engine), op), id)) in
        let base = cc_button_color(b) in
        let color = if_then_else(down, darken(base, 40), if_then_else(hot, lighten(base, 18), base)) in
        let x = ox + field(b, x) in let y = oy + field(b, y) in let w = field(b, w) in let h = field(b, h) in
        let size = if_then_else(nat_eq(field(b, kind), 4), 12, if_then_else(nat_eq(field(b, kind), 5), 10,
            if_then_else(or(text_eq(id, "CE"), or(text_eq(id, "C"), or(text_eq(id, "recip"), or(text_eq(id, "square"), text_eq(id, "sqrt"))))), 18, 24))) in
        let text_color = if_then_else(disabled, 1382185215, if_then_else(selected, 2379546623, cc_text())) in
        cat_all(list(
            rrect(px(x), px(y + 1), px(w), px(h), px(10), solid(135141887)),
            rrect(px(x), px(y), px(w), px(h), px(10), solid(if_then_else(selected, cc_blue(), color))),
            if_then_else(selected, rrect(px(x + 1), px(y + 1), px(monus(w, 2)), px(monus(h, 2)), px(9), solid(color)), nv_empty()),
            cc_text_center(fonts, x, y + if_then_else(down, 1, 0), w, h, size, text_color, field(b, label))))
    fn cc_header_chunk(fonts: NatVec, ox: Nat, oy: Nat) -> NatVec = cat_all(list(
        cc_text_at(fonts, ox + 20, oy + 18, 25, 330, cc_text(), "Калькулятор"),
        cc_text_at(fonts, ox + 20, oy + 53, 11, 360, cc_muted(), "Обычный режим"),
        rrect(px(ox + 370), px(oy + 24), px(50), px(24), px(7), solid(456479743)),
        cc_text_center(fonts, ox + 370, oy + 24, 50, 24, 10, 2379546623, "DLM")))
    fn cc_display_chunk(m: CalcModel, fonts: NatVec, ox: Nat, oy: Nat) -> NatVec =
        let e = field(m, engine) in let failed = text_length(field(e, error)) > 0 in
        let status = if_then_else(failed, field(e, error), if_then_else(field(m, has_memory), concat("M: ", cc_ui_text(cc_format(field(m, memory)))), "Мышь и клавиатура")) in
        cat_all(list(
            rrect(px(ox + 20), px(oy + 80), px(400), px(112), px(16), solid(cc_panel())),
            cc_text_right(fonts, ox + 36, oy + 96, 13, 368, cc_muted(), cc_ui_text(field(e, hint))),
            cc_text_right(fonts, ox + 36, oy + 122, cc_entry_size(fonts, cc_ui_text(field(e, entry))), 368, if_then_else(failed, cc_red(), cc_text()), cc_ui_text(field(e, entry))),
            cc_text_at(fonts, ox + 36, oy + 172, 10, 368, if_then_else(failed, cc_red(), cc_muted()), status)))
    fn cc_history_row(m: CalcModel, fonts: NatVec, hover: Text, i: Nat, ox: Nat, oy: Nat) -> NatVec =
        match index(field(m, history), i) {
            none => nv_empty(),
            some(item) => let y = oy + 608 + mul(i, 24) in
                cat_all(list(
                    if_then_else(text_eq(hover, concat("h", nat_to_text(i))), rrect(px(ox + 28), px(y), px(384), px(24), px(6), solid(455687935)), nv_empty()),
                    cc_text_at(fonts, ox + 36, y + 5, 12, 368, cc_muted(), cc_ui_text(field(item, text)))))
        }
    fn cc_history_chunk(m: CalcModel, fonts: NatVec, hover: Text, ox: Nat, oy: Nat) -> NatVec = cat_all(list(
        rrect(px(ox + 20), px(oy + 578), px(400), px(106), px(14), solid(cc_panel())),
        cc_text_at(fonts, ox + 34, oy + 590, 11, 250, cc_muted(), "История"),
        if_then_else(is_empty(field(m, history)), cc_text_at(fonts, ox + 36, oy + 626, 12, 368, cc_muted(), "Здесь появятся результаты"), nv_empty()),
        cat_all(map(list(0, 1, 2), fun(i: Nat, cc_history_row(m, fonts, hover, i, ox, oy))))))
    fn cc_chunks(m: CalcModel, fonts: NatVec, hover: Text, pressed: Text, w: Nat, h: Nat) -> List<NatVec> =
        let ox = cc_origin_x(w, h) in let oy = cc_origin_y(w, h) in let num = cc_scale_num(w, h) in
        let world_w = div(mul(w, 1000) + pred(num), num) in let world_h = div(mul(h, 1000) + pred(num), num) in
        cons(rrect(0, 0, px(world_w), px(world_h), 0, solid(cc_bg())),
            cons(cc_header_chunk(fonts, ox, oy), cons(cc_display_chunk(m, fonts, ox, oy), cons(cc_history_chunk(m, fonts, hover, ox, oy),
                cons(cc_text_at(fonts, ox + 20, oy + 699, 10, 400, cc_muted(), "Enter =   Esc C   Del CE   Backspace ⌫"),
                    map(cc_buttons(), fun(b: CalcButton, cc_button_chunk(m, fonts, hover, pressed, b, ox, oy))))))))
    fn cc_hit(m: CalcModel, x: Nat, y: Nat) -> Text =
        let button = fold(cc_buttons(), "", fun(hit: Text, b: CalcButton,
            if_then_else(and(not(cc_disabled(m, b)), inside(Rect(field(b, x), field(b, y), field(b, w), field(b, h)), x, y)), field(b, id), hit))) in
        if_then_else(text_length(button) > 0, button,
            if_then_else(and(inside(Rect(28, 608, 384, 72), x, y), not(is_empty(field(m, history)))),
                let i = div(monus(y, 608), 24) in if_then_else(length(field(m, history)) > i, concat("h", nat_to_text(i)), ""), ""))
}
