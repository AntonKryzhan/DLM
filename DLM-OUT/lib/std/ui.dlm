// Own GUI toolkit, stage G1 (v0.127, docs/GUI.md): a widget tree (recursive enum), layout,
// software rendering into raster images (gx_*), hit testing, Elm-style model/view/update,
// PNG snapshots and a browser backend over HTTP. Rendering and layout are pure.
// v0.129: scrolling containers, list views and tables (Scroll/ListView/Table, events Select
// and Wheel), text measured in TrueType fonts (`*_f` functions taking font ids).
module std.ui
import std.net

theory StdUi extends StdNet {
    record Rect(x: Nat, y: Nat, w: Nat, h: Nat)
    record Size(w: Nat, h: Nat)
    // colors are 0xRRGGBBAA; `scale` multiplies the 6x8 font cell; `pad` is inner spacing
    record Theme(bg: Nat, fg: Nat, accent: Nat, on_accent: Nat, panel: Nat, border: Nat, field: Nat, scale: Nat, pad: Nat)
    enum Widget {
        Label(text: Text),
        Button(id: Text, text: Text),
        Check(id: Text, text: Text, on: Bool),
        Field(id: Text, value: Text, chars: Nat),
        Bar(value: Nat, max: Nat, width: Nat),
        Spacer(w: Nat, h: Nat),
        Column(gap: Nat, kids: List<Widget>),
        Row(gap: Nat, kids: List<Widget>),
        Panel(title: Text, child: Widget),
        // scrolled by whole rows: `offset` is the first visible child / row / item
        Scroll(id: Text, height: Nat, offset: Nat, kids: List<Widget>),
        ListView(id: Text, items: List<Text>, selected: Nat, rows: Nat, offset: Nat, chars: Nat),
        Table(id: Text, headers: List<Text>, rows: List<List<Text>>, widths: List<Nat>, selected: Nat, visible: Nat, offset: Nat)
    }
    // kind: 0 label, 1 button, 2 check, 3 field, 4 bar, 6 panel, 7 scroll box (value offset, max count),
    // 8 end of a scroll box, 9 row (value index, on selected), 10 cell (value column), 11 header cell
    record Placed(kind: Nat, id: Text, text: Text, on: Bool, rect: Rect, value: Nat, max: Nat)
    enum UiEvent { Press(id: Text), Toggle(id: Text), Input(id: Text, value: Text), Pointer(x: Nat, y: Nat), Select(id: Text, index: Nat), Wheel(id: Text, down: Bool) }
    record Flow(at: Nat, items: List<Placed>)
    record RowAcc(i: Nat, at: Nat, items: List<Placed>)

    fn default_theme() -> Theme = Theme(4059165183, 572662527, 1450696191, 4294967295, 3958107135, 2560137471, 4294967295, 2, 6)
    private fn ui_max(a: Nat, b: Nat) -> Nat = if_then_else(a > b, a, b)
    private fn ui_min(a: Nat, b: Nat) -> Nat = if_then_else(a > b, b, a)
    private fn ui_rev(xs: List<Placed>) -> List<Placed> = fold(xs, empty_list(Placed), fun(acc: List<Placed>, p: Placed, cons(p, acc)))
    private fn ui_append(a: List<Placed>, b: List<Placed>) -> List<Placed> = fold(ui_rev(a), b, fun(acc: List<Placed>, p: Placed, cons(p, acc)))
    fn line_height(t: Theme) -> Nat = mul(8, field(t, scale))
    fn text_size(t: Theme, s: Text) -> Size = Size(gx_text_width(s, field(t, scale)), line_height(t))
    private fn ui_title_h(t: Theme, title: Text) -> Nat = if_then_else(nat_eq(text_length(title), 0), 0, line_height(t) + mul(2, field(t, pad)))
    // text width in px: the stroked 6x8-cell font, or the TrueType fonts `fonts` (ids of `file_font_load`)
    fn text_w(t: Theme, fonts: NatVec, s: Text) -> Nat =
        if_then_else(nat_eq(nv_len(fonts), 0), gx_text_width(s, field(t, scale)), div(vx_font_width(fonts, s, mul(16, line_height(t))) + 15, 16))
    fn row_h(t: Theme) -> Nat = line_height(t) + field(t, pad)
    fn scrollbar_w(t: Theme) -> Nat = mul(5, field(t, scale))
    fn chars_w(t: Theme, n: Nat) -> Nat = mul(mul(n, 6), field(t, scale))
    fn cells_w(t: Theme, ws: List<Nat>) -> Nat = fold(ws, 0, fun(acc: Nat, c: Nat, acc + chars_w(t, c) + mul(2, field(t, pad))))

    // natural size of a widget
    fn measure_f(t: Theme, fonts: NatVec, w: Widget) -> Size decreasing w =
        let p = field(t, pad) in let lh = line_height(t) in
        match w {
            Label(s) => Size(text_w(t, fonts, s), lh),
            Button(id, s) => Size(text_w(t, fonts, s) + mul(2, p), lh + mul(2, p)),
            Check(id, s, on) => Size(lh + p + text_w(t, fonts, s), lh + p),
            Field(id, v, n) => Size(mul(mul(n, 6), field(t, scale)) + mul(2, p), lh + mul(2, p)),
            Bar(v, m, width) => Size(width, lh),
            Spacer(sw, sh) => Size(sw, sh),
            Column(gap, kids) => let s = fold(kids, Size(0, 0), fun(acc: Size, k: Widget, let m = measure_f(t, fonts, k) in Size(ui_max(field(acc, w), field(m, w)), field(acc, h) + field(m, h) + gap))) in
                Size(field(s, w), monus(field(s, h), if_then_else(is_empty(kids), 0, gap))),
            Row(gap, kids) => let s = fold(kids, Size(0, 0), fun(acc: Size, k: Widget, let m = measure_f(t, fonts, k) in Size(field(acc, w) + field(m, w) + gap, ui_max(field(acc, h), field(m, h))))) in
                Size(monus(field(s, w), if_then_else(is_empty(kids), 0, gap)), field(s, h)),
            Panel(title, child) => let c = measure_f(t, fonts, child) in
                Size(ui_max(field(c, w), text_w(t, fonts, title)) + mul(2, p), ui_title_h(t, title) + field(c, h) + mul(2, p)),
            Scroll(id, hh, off, kids) => Size(fold(kids, 0, fun(acc: Nat, k: Widget, ui_max(acc, field(measure_f(t, fonts, k), w)))) + scrollbar_w(t) + 2, hh),
            ListView(id, items, sel, rows, off, n) => Size(chars_w(t, n) + mul(2, p) + scrollbar_w(t) + 2, mul(rows, row_h(t)) + 2),
            Table(id, hs, rows, ws, sel, vis, off) => Size(cells_w(t, ws) + scrollbar_w(t) + 2, mul(vis + 1, row_h(t)) + 2)
        }

    fn measure(t: Theme, w: Widget) -> Size = measure_f(t, nv_empty(), w)
    private fn ui_leaf(kind: Nat, id: Text, s: Text, on: Bool, r: Rect, v: Nat, m: Nat) -> List<Placed> = list(Placed(kind, id, s, on, r, v, m))
    // cells of one table row (kind 10) or of the header (kind 11), left to right
    fn ui_cells(t: Theme, kind: Nat, id: Text, texts: List<Text>, ws: List<Nat>, x0: Nat, y0: Nat) -> List<Placed> =
        ui_rev(field(fold(ws, RowAcc(0, x0, empty_list(Placed)), fun(acc: RowAcc, c: Nat,
            let cw = chars_w(t, c) + mul(2, field(t, pad)) in
            RowAcc(field(acc, i) + 1, field(acc, at) + cw, cons(Placed(kind, id, unwrap_or(index(texts, field(acc, i)), ""), false, Rect(field(acc, at), y0, cw, row_h(t)), field(acc, i), 0), field(acc, items))))), items))
    fn ui_visible(i: Nat, off: Nat, n: Nat) -> Bool = and(not(off > i), off + n > i)
    // placed elements, parents before children (drawing order)
    fn layout_f(t: Theme, fonts: NatVec, w: Widget, x: Nat, y: Nat) -> List<Placed> decreasing w =
        let sz = measure_f(t, fonts, w) in let r = Rect(x, y, field(sz, w), field(sz, h)) in let p = field(t, pad) in
        match w {
            Label(s) => ui_leaf(0, "", s, false, r, 0, 0),
            Button(id, s) => ui_leaf(1, id, s, false, r, 0, 0),
            Check(id, s, on) => ui_leaf(2, id, s, on, r, 0, 0),
            Field(id, v, n) => ui_leaf(3, id, v, false, r, n, 0),
            Bar(v, m, width) => ui_leaf(4, "", "", false, r, v, m),
            Spacer(sw, sh) => empty_list(Placed),
            Column(gap, kids) => field(fold(kids, Flow(y, empty_list(Placed)), fun(acc: Flow, k: Widget,
                Flow(field(acc, at) + field(measure_f(t, fonts, k), h) + gap, ui_append(field(acc, items), layout_f(t, fonts, k, x, field(acc, at)))))), items),
            Row(gap, kids) => field(fold(kids, Flow(x, empty_list(Placed)), fun(acc: Flow, k: Widget,
                Flow(field(acc, at) + field(measure_f(t, fonts, k), w) + gap, ui_append(field(acc, items), layout_f(t, fonts, k, field(acc, at), y))))), items),
            Panel(title, child) => cons(Placed(6, "", title, false, r, 0, 0), layout_f(t, fonts, child, x + p, y + p + ui_title_h(t, title))),
            Scroll(id, hh, off, kids) =>
                let body = field(fold(kids, RowAcc(0, y + 1, empty_list(Placed)), fun(acc: RowAcc, k: Widget,
                    let i = field(acc, i) in let kh = field(measure_f(t, fonts, k), h) in
                    if_then_else(and(not(off > i), or(nat_eq(i, off), not(field(acc, at) + kh > y + hh))),
                        RowAcc(i + 1, field(acc, at) + kh, ui_append(field(acc, items), layout_f(t, fonts, k, x + 1, field(acc, at)))),
                        RowAcc(i + 1, if_then_else(off > i, field(acc, at), y + hh + 1), field(acc, items))))), items) in
                cons(Placed(7, id, "", false, r, off, length(kids)), ui_append(body, list(Placed(8, id, "", false, r, 0, 0)))),
            ListView(id, items, sel, rows, off, n) =>
                let rw = monus(field(sz, w), scrollbar_w(t) + 2) in
                let body = field(fold(items, RowAcc(0, 0, empty_list(Placed)), fun(acc: RowAcc, s: Text,
                    let i = field(acc, i) in
                    RowAcc(i + 1, 0, if_then_else(ui_visible(i, off, rows),
                        cons(Placed(9, id, s, nat_eq(i, sel), Rect(x + 1, y + 1 + mul(monus(i, off), row_h(t)), rw, row_h(t)), i, 0), field(acc, items)), field(acc, items))))), items) in
                cons(Placed(7, id, "", false, r, off, length(items)), ui_append(ui_rev(body), list(Placed(8, id, "", false, r, 0, 0)))),
            Table(id, hs, rows, ws, sel, vis, off) =>
                let rw = monus(field(sz, w), scrollbar_w(t) + 2) in
                let body = field(fold(rows, RowAcc(0, 0, empty_list(Placed)), fun(acc: RowAcc, cells: List<Text>,
                    let i = field(acc, i) in let ry = y + 1 + mul(monus(i, off) + 1, row_h(t)) in
                    RowAcc(i + 1, 0, if_then_else(ui_visible(i, off, vis),
                        ui_append(ui_rev(ui_cells(t, 10, id, cells, ws, x + 1, ry)), cons(Placed(9, id, "", nat_eq(i, sel), Rect(x + 1, ry, rw, row_h(t)), i, 0), field(acc, items))),
                        field(acc, items))))), items) in
                cons(Placed(7, id, "", false, r, off, length(rows)), ui_append(ui_cells(t, 11, id, hs, ws, x + 1, y + 1), ui_append(ui_rev(body), list(Placed(8, id, "", false, r, 0, 0)))))
        }

    fn layout(t: Theme, w: Widget, x: Nat, y: Nat) -> List<Placed> = layout_f(t, nv_empty(), w, x, y)

    fn inside(r: Rect, x: Nat, y: Nat) -> Bool =
        and(and(not(field(r, x) > x), not(field(r, y) > y)), and(field(r, x) + field(r, w) > x, field(r, y) + field(r, h) > y))
    record PointAcc(items: List<Placed>, clips: List<Rect>)
    // Apply every enclosing scroll clip before hit testing a descendant.
    fn ui_at(items: List<Placed>, x: Nat, y: Nat) -> List<Placed> =
        ui_rev(field(fold(items, PointAcc(empty_list(Placed), empty_list(Rect)), fun(acc: PointAcc, p: Placed,
            let clips = field(acc, clips) in let k = field(p, kind) in
            let visible = and(inside(field(p, rect), x, y), fold(clips, true, fun(ok: Bool, r: Rect, and(ok, inside(r, x, y))))) in
            PointAcc(if_then_else(visible, cons(p, field(acc, items)), field(acc, items)),
                if_then_else(nat_eq(k, 7), cons(field(p, rect), clips), if_then_else(nat_eq(k, 8), tail(clips), clips))))), items))
    // the topmost interactive element under the point
    fn hit(items: List<Placed>, x: Nat, y: Nat) -> Option<UiEvent> =
        fold(ui_at(items, x, y), none(UiEvent), fun(acc: Option<UiEvent>, p: Placed,
            if_then_else(inside(field(p, rect), x, y),
                if_then_else(nat_eq(field(p, kind), 1), some(Press(field(p, id))),
                if_then_else(nat_eq(field(p, kind), 2), some(Toggle(field(p, id))),
                if_then_else(nat_eq(field(p, kind), 3), some(Press(field(p, id))),
                if_then_else(nat_eq(field(p, kind), 9), some(Select(field(p, id), field(p, value))), acc)))), acc)))
    // the wheel event of the innermost scroll box under the point
    fn wheel_at(items: List<Placed>, x: Nat, y: Nat, down: Bool) -> Option<UiEvent> =
        fold(ui_at(items, x, y), none(UiEvent), fun(acc: Option<UiEvent>, p: Placed,
            if_then_else(and(nat_eq(field(p, kind), 7), inside(field(p, rect), x, y)), some(Wheel(field(p, id), down)), acc)))
    // the first visible row after one wheel step (`count` rows, `visible` of them shown)
    fn scrolled(offset: Nat, down: Bool, count: Nat, visible: Nat) -> Nat =
        if_then_else(down, ui_min(offset + 1, monus(count, visible)), monus(offset, 1))
    // the first visible row that keeps row `index` in view
    fn follow(offset: Nat, index: Nat, visible: Nat) -> Nat =
        if_then_else(offset > index, index, if_then_else(index + 1 > offset + visible, monus(index + 1, visible), offset))
    // scroll bar thumb inside the box `r` for `count` rows starting at `offset`
    fn thumb_rect(t: Theme, r: Rect, offset: Nat, count: Nat) -> Rect =
        let track = monus(field(r, h), 2) in let th = ui_min(track, ui_max(div(track, ui_max(count, 1)), mul(4, field(t, scale)))) in
        Rect(monus(field(r, x) + field(r, w), scrollbar_w(t) + 1), field(r, y) + 1 + div(mul(monus(track, th), ui_min(offset, monus(count, 1))), ui_max(monus(count, 1), 1)), scrollbar_w(t), th)

    fn frame(img: NatVec, r: Rect, color: Nat) -> NatVec =
        let x1 = monus(field(r, x) + field(r, w), 1) in let y1 = monus(field(r, y) + field(r, h), 1) in
        let a = gx_line(img, field(r, x), field(r, y), x1, field(r, y), color) in let b = gx_line(a, x1, field(r, y), x1, y1, color) in
        let c = gx_line(b, x1, y1, field(r, x), y1, color) in gx_line(c, field(r, x), y1, field(r, x), field(r, y), color)
    private fn ui_fill(img: NatVec, r: Rect, color: Nat) -> NatVec = gx_fill_rect(img, field(r, x), field(r, y), field(r, w), field(r, h), color)
    fn draw(t: Theme, img: NatVec, p: Placed) -> NatVec =
        let r = field(p, rect) in let s = field(t, scale) in let pd = field(t, pad) in let k = field(p, kind) in let lh = line_height(t) in
        if_then_else(nat_eq(k, 0), gx_text(img, field(r, x), field(r, y), field(p, text), field(t, fg), s),
        if_then_else(nat_eq(k, 1), gx_text(frame(ui_fill(img, r, field(t, accent)), r, field(t, border)), field(r, x) + pd, field(r, y) + pd, field(p, text), field(t, on_accent), s),
        if_then_else(nat_eq(k, 2),
            let box = Rect(field(r, x), field(r, y), lh, lh) in
            let b = frame(ui_fill(img, box, field(t, field)), box, field(t, border)) in
            let c = if_then_else(field(p, on), ui_fill(b, Rect(field(r, x) + s + s, field(r, y) + s + s, monus(lh, mul(4, s)), monus(lh, mul(4, s))), field(t, accent)), b) in
            gx_text(c, field(r, x) + lh + pd, field(r, y), field(p, text), field(t, fg), s),
        if_then_else(nat_eq(k, 3), gx_text(frame(ui_fill(img, r, field(t, field)), r, field(t, border)), field(r, x) + pd, field(r, y) + pd, field(p, text), field(t, fg), s),
        if_then_else(nat_eq(k, 4),
            let filled = if_then_else(nat_eq(field(p, max), 0), 0, div(mul(field(r, w), ui_min(field(p, value), field(p, max))), field(p, max))) in
            frame(gx_fill_rect(ui_fill(img, r, field(t, field)), field(r, x), field(r, y), filled, field(r, h), field(t, accent)), r, field(t, border)),
        if_then_else(nat_eq(k, 6),
            let body = frame(ui_fill(img, r, field(t, panel)), r, field(t, border)) in
            if_then_else(nat_eq(text_length(field(p, text)), 0), body,
                gx_text(ui_fill(body, Rect(field(r, x), field(r, y), field(r, w), lh + mul(2, pd)), field(t, accent)), field(r, x) + pd, field(r, y) + pd, field(p, text), field(t, on_accent), s)),
        if_then_else(nat_eq(k, 7), ui_fill(frame(ui_fill(img, r, field(t, field)), r, field(t, border)), thumb_rect(t, r, field(p, value), field(p, max)), field(t, border)),
        if_then_else(nat_eq(k, 9), if_then_else(field(p, on),
                gx_text(ui_fill(img, r, field(t, accent)), field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, on_accent), s),
                gx_text(img, field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, fg), s)),
        if_then_else(nat_eq(k, 10), gx_text(img, field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, fg), s),
        if_then_else(nat_eq(k, 11), gx_text(frame(ui_fill(img, r, field(t, panel)), r, field(t, border)), field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, fg), s),
            img))))))))))
    fn render(t: Theme, items: List<Placed>, w: Nat, h: Nat) -> NatVec =
        fold(items, unwrap_or(gx_new(w, h, field(t, bg)), nv_empty()), fun(img: NatVec, p: Placed, draw(t, img, p)))
    // the widget drawn with a margin of `pad` around it
    fn screenshot(t: Theme, w: Widget) -> NatVec =
        let s = measure(t, w) in let m = field(t, pad) in render(t, layout(t, w, m, m), field(s, w) + mul(2, m), field(s, h) + mul(2, m))
    fn save_png(path: Text, t: Theme, w: Widget) -> Result<Nat, Text> = file_write_png(path, screenshot(t, w))

    // model/view/update without a display: replay events, click at a point
    fn apply_events<M>(m: M, update: Fn<M, UiEvent, M>, events: List<UiEvent>) -> M = fold(events, m, fun(acc: M, e: UiEvent, update(acc, e)))
    fn click<M>(t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, x: Nat, y: Nat) -> M =
        let p = field(t, pad) in match hit(layout(t, view(m), p, p), x, y) { none => m, some(e) => update(m, e) }

    // ---- browser backend: HTML with absolutely positioned elements, events as links/forms ----
    private fn ui_replace(s: Text, pat: Text, rep: Text, done: Text, left: Nat) -> Text decreasing left =
        if_then_else(nat_eq(left, 0), concat(done, s), match index_of(s, pat) { none => concat(done, s), some(k) =>
            ui_replace(substring(s, k + text_length(pat), monus(text_length(s), k + text_length(pat))), pat, rep, concat(concat(done, substring(s, 0, k)), rep), pred(left)) })
    fn replace_all(s: Text, pat: Text, rep: Text) -> Text = ui_replace(s, pat, rep, "", text_length(s) + 1)
    fn html_escape(s: Text) -> Text = replace_all(replace_all(replace_all(replace_all(s, "&", "&amp;"), "<", "&lt;"), ">", "&gt;"), "\"", "&quot;")
    private fn ui_css_color(c: Nat) -> Text =
        concat(concat(concat(concat(concat(concat("rgba(", nat_to_text(div(c, 16777216))), ","), concat(nat_to_text(mod(div(c, 65536), 256)), ",")), concat(nat_to_text(mod(div(c, 256), 256)), ",")), nat_to_text(div(mul(mod(c, 256), 100), 255))), "%)")
    private fn ui_box(r: Rect, extra: Text) -> Text =
        concat(concat(concat(concat(concat(concat(concat(concat("position:absolute;left:", nat_to_text(field(r, x))), "px;top:"), nat_to_text(field(r, y))), "px;width:"), nat_to_text(field(r, w))), "px;height:"), concat(nat_to_text(field(r, h)), "px;box-sizing:border-box;")), extra)
    private fn ui_font(t: Theme) -> Text = concat(concat("font:", nat_to_text(line_height(t))), "px monospace;")
    private fn ui_url_bytes(cp: Nat) -> List<Nat> =
        if_then_else(128 > cp, list(cp), if_then_else(2048 > cp, list(192 + div(cp, 64), 128 + mod(cp, 64)),
            if_then_else(65536 > cp, list(224 + div(cp, 4096), 128 + mod(div(cp, 64), 64), 128 + mod(cp, 64)),
                list(240 + div(cp, 262144), 128 + mod(div(cp, 4096), 64), 128 + mod(div(cp, 64), 64), 128 + mod(cp, 64)))))
    private fn ui_url_byte(c: Nat) -> Text =
        if_then_else(or(and(c > 47, 58 > c), or(and(c > 64, 91 > c), or(and(c > 96, 123 > c), or(nat_eq(c, 45), or(nat_eq(c, 46), or(nat_eq(c, 95), nat_eq(c, 126))))))),
            vx_from_codepoints(unwrap_or(nv_from_list(list(c)), nv_empty())),
            concat("%", concat(substring("0123456789ABCDEF", div(c, 16), 1), substring("0123456789ABCDEF", mod(c, 16), 1))))
    // Percent-encode UTF-8 bytes before embedding an id in a URL path.
    fn url_encode(s: Text) -> Text =
        fold(nv_to_list(vx_codepoints(s)), "", fun(out: Text, cp: Nat,
            fold(ui_url_bytes(cp), out, fun(acc: Text, c: Nat, concat(acc, ui_url_byte(c))))))
    // `/e/wheel/up/<id>` and `/e/wheel/down/<id>` links at the right edge of a scroll box
    fn ui_wheel_links(t: Theme, p: Placed) -> Text =
        let r = field(p, rect) in let id = url_encode(field(p, id)) in let sw = scrollbar_w(t) + 1 in let half = div(field(r, h), 2) in
        let up = Rect(monus(field(r, x) + field(r, w), sw), field(r, y), sw, half) in let dn = Rect(monus(field(r, x) + field(r, w), sw), field(r, y) + half, sw, monus(field(r, h), half)) in
        concat(concat(concat(concat(concat(concat("<a href=\"/e/wheel/up/", id), "\" style=\""), ui_box(up, "text-align:center;text-decoration:none\">&#9650;</a>")),
            concat("<a href=\"/e/wheel/down/", id)), "\" style=\""), ui_box(dn, "text-align:center;text-decoration:none\">&#9660;</a>"))
    fn html_item(t: Theme, p: Placed) -> Text =
        let r = field(p, rect) in let k = field(p, kind) in let txt = html_escape(field(p, text)) in let id = url_encode(field(p, id)) in
        if_then_else(nat_eq(k, 0), concat(concat(concat(concat("<span style=\"", ui_box(r, concat(ui_font(t), concat("color:", ui_css_color(field(t, fg)))))), "\">"), txt), "</span>"),
        if_then_else(nat_eq(k, 1), concat(concat(concat(concat(concat(concat("<a href=\"/e/press/", id), "\"><button style=\""), ui_box(r, concat(ui_font(t), concat(concat("background:", ui_css_color(field(t, accent))), concat(";color:", ui_css_color(field(t, on_accent))))))), "\">"), txt), "</button></a>"),
        if_then_else(nat_eq(k, 2), concat(concat(concat(concat(concat(concat("<a href=\"/e/toggle/", id), "\" style=\""), ui_box(r, concat(ui_font(t), "text-decoration:none;color:inherit"))), "\">"), concat(if_then_else(field(p, on), "&#9745; ", "&#9744; "), txt)), "</a>"),
        if_then_else(nat_eq(k, 3), concat(concat(concat(concat(concat(concat("<form method=\"get\" action=\"/e/input/", id), "\" style=\"margin:0\"><input name=\"v\" value=\""), txt), "\" style=\""), ui_box(r, ui_font(t))), "\"></form>"),
        if_then_else(nat_eq(k, 4), concat(concat(concat(concat("<div style=\"", ui_box(r, concat("border:1px solid ", ui_css_color(field(t, border))))), "\"><div style=\"height:100%;background:"), ui_css_color(field(t, accent))),
            concat(concat(";width:", nat_to_text(if_then_else(nat_eq(field(p, max), 0), 0, div(mul(100, ui_min(field(p, value), field(p, max))), field(p, max))))), "%\"></div></div>")),
        if_then_else(nat_eq(k, 7), concat(concat(concat("<div style=\"", ui_box(r, concat(concat("background:", ui_css_color(field(t, field))), concat(";border:1px solid ", ui_css_color(field(t, border)))))), "\"></div>"), ui_wheel_links(t, p)),
        if_then_else(nat_eq(k, 9), concat(concat(concat(concat(concat(concat("<a href=\"/e/select/", nat_to_text(field(p, value))), concat("/", id)), "\" style=\""),
            ui_box(r, concat(concat(ui_font(t), "display:block;text-decoration:none;overflow:hidden;white-space:nowrap;"), if_then_else(field(p, on), concat(concat("background:", ui_css_color(field(t, accent))), concat(";color:", ui_css_color(field(t, on_accent)))), concat("color:", ui_css_color(field(t, fg))))))), "\">"), concat(txt, "</a>")),
        if_then_else(nat_eq(k, 10), concat(concat(concat(concat("<span style=\"", ui_box(r, concat(ui_font(t), "overflow:hidden;white-space:nowrap;pointer-events:none;"))), "\">"), txt), "</span>"),
        if_then_else(nat_eq(k, 11), concat(concat(concat(concat("<span style=\"", ui_box(r, concat(ui_font(t), concat(concat("overflow:hidden;white-space:nowrap;font-weight:bold;background:", ui_css_color(field(t, panel))), concat(";border:1px solid ", ui_css_color(field(t, border))))))), "\">"), txt), "</span>"),
        if_then_else(nat_eq(k, 6), concat(concat(concat(concat("<div style=\"", ui_box(r, concat(concat("background:", ui_css_color(field(t, panel))), concat(";border:1px solid ", ui_css_color(field(t, border)))))), "\"><div style=\""), concat(ui_font(t), concat("background:", ui_css_color(field(t, accent))))), concat(concat(concat(";color:", ui_css_color(field(t, on_accent))), "\">"), concat(txt, "</div></div>"))),
            ""))))))))))
    fn to_html(t: Theme, w: Widget, title: Text) -> Text =
        let s = measure(t, w) in let m = field(t, pad) in
        concat(concat(concat(concat(concat(concat("<!doctype html><html><head><meta charset=\"utf-8\"><title>", html_escape(title)), "</title></head><body style=\"margin:0;background:"), ui_css_color(field(t, bg))),
            concat(concat(concat("\"><div style=\"position:relative;width:", nat_to_text(field(s, w) + mul(2, m))), "px;height:"), concat(nat_to_text(field(s, h) + mul(2, m)), "px\">"))),
            fold(layout(t, w, m, m), "", fun(acc: Text, p: Placed, concat(acc, html_item(t, p))))), "</div></body></html>")

    private fn ui_hex(c: Nat) -> Option<Nat> =
        if_then_else(and(c > 47, 58 > c), some(monus(c, 48)), if_then_else(and(c > 96, 103 > c), some(monus(c, 87)), if_then_else(and(c > 64, 71 > c), some(monus(c, 55)), none(Nat))))
    private fn ui_unescape(bs: List<Nat>, out: List<Nat>) -> List<Nat> decreasing bs =
        match bs { [] => out, cons(b, rest) =>
            if_then_else(nat_eq(b, 43), ui_unescape(rest, cons(32, out)),
            if_then_else(and(nat_eq(b, 37), length(rest) > 1),
                match ui_hex(unwrap_or(index(rest, 0), 0)) { none => ui_unescape(rest, cons(b, out)), some(hi) =>
                    match ui_hex(unwrap_or(index(rest, 1), 0)) { none => ui_unescape(rest, cons(b, out)), some(lo) => let r2 = tail(rest) in ui_unescape(tail(r2), cons(mul(hi, 16) + lo, out)) } },
                ui_unescape(rest, cons(b, out)))) }
    private fn ui_rev_nat(xs: List<Nat>) -> List<Nat> = fold(xs, empty_list(Nat), fun(acc: List<Nat>, x: Nat, cons(x, acc)))
    // `%XX` and `+` decoding of a URL component; malformed UTF-8 gives none
    fn url_decode(s: Text) -> Option<Text> = let b = buf_from_text(s) in let bytes = buf_to_list(b) in let o = buf_from_list(ui_rev_nat(ui_unescape(bytes, empty_list(Nat)))) in let r = buf_to_text(o) in let f = buf_free(o) in r
    private fn ui_after(s: Text, prefix: Text) -> Option<Text> =
        if_then_else(and(not(text_length(prefix) > text_length(s)), text_eq(substring(s, 0, text_length(prefix)), prefix)), some(substring(s, text_length(prefix), monus(text_length(s), text_length(prefix)))), none(Text))
    private fn ui_route_select(rest: Text) -> Option<UiEvent> =
        match index_of(rest, "/") { none => none(UiEvent), some(k) => match text_to_nat(substring(rest, 0, k)) { none => none(UiEvent), some(n) =>
            match url_decode(substring(rest, k + 1, monus(text_length(rest), k + 1))) { none => none(UiEvent), some(id) => some(Select(id, n)) } } }
    private fn ui_route_wheel(rest: Text) -> Option<UiEvent> =
        match ui_after(rest, "down/") { some(id) => match url_decode(id) { none => none(UiEvent), some(d) => some(Wheel(d, true)) }, none =>
        match ui_after(rest, "up/") { some(id) => match url_decode(id) { none => none(UiEvent), some(d) => some(Wheel(d, false)) }, none => none(UiEvent) } }
    // `/e/press/<id>`, `/e/toggle/<id>`, `/e/input/<id>?v=<value>`, `/e/select/<n>/<id>`, `/e/wheel/{up,down}/<id>`
    fn route_event(path: Text) -> Option<UiEvent> =
        match ui_after(path, "/e/select/") { some(rest) => ui_route_select(rest), none =>
        match ui_after(path, "/e/wheel/") { some(rest) => ui_route_wheel(rest), none =>
        match ui_after(path, "/e/press/") { some(id) => match url_decode(id) { none => none(UiEvent), some(d) => some(Press(d)) }, none =>
        match ui_after(path, "/e/toggle/") { some(id) => match url_decode(id) { none => none(UiEvent), some(d) => some(Toggle(d)) }, none =>
        match ui_after(path, "/e/input/") { none => none(UiEvent), some(rest) => match index_of(rest, "?v=") {
            none => none(UiEvent),
            some(k) => match url_decode(substring(rest, 0, k)) { none => none(UiEvent), some(id) =>
                match url_decode(substring(rest, k + 3, monus(text_length(rest), k + 3))) { none => none(UiEvent), some(v) => some(Input(id, v)) } } } } } } } }
    fn ui_skip_headers(c: Nat) -> Result<Nat, Text> loop =
        match net_recv_line(c) { err(e) => err(e, Nat), ok(o) => match o { none => err("unexpected end of HTTP headers", Nat), some(l) => if_then_else(nat_eq(text_length(l), 0), ok(0, Text), ui_skip_headers(c)) } }
    fn ui_request(c: Nat) -> Result<Text, Text> =
        match net_recv_line(c) { err(e) => err(e, Text), ok(first) => match first {
            none => err("empty HTTP request", Text), some(line) =>
                let parts = words(line) in
                if_then_else(not(and(nat_eq(length(parts), 3), and(text_eq(unwrap_or(index(parts, 0), ""), "GET"),
                    or(text_eq(unwrap_or(index(parts, 2), ""), "HTTP/1.0"), text_eq(unwrap_or(index(parts, 2), ""), "HTTP/1.1"))))),
                    err("malformed HTTP request", Text),
                    match ui_skip_headers(c) { err(e) => err(e, Text), ok(h) => ok(unwrap_or(index(parts, 1), "/"), Text) }) } }
    fn ui_reply<M>(c: Nat, response: Text, model: M) -> Result<M, Text> =
        match net_send(c, response) { err(e) => let x = net_close(c) in err(e, M), ok(n) => let x = net_close(c) in ok(model, Text) }
    // one HTTP request: an event redirects to `/`, anything else gets the page
    fn serve_once<M>(l: Nat, t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, title: Text, timeout_ms: Nat) -> Result<M, Text> =
        match net_accept(l, timeout_ms) { err(e) => err(e, M), ok(c) =>
            match ui_request(c) { err(e) => let x = net_close(c) in err(e, M), ok(path) =>
                match route_event(path) {
                    some(e) => ui_reply(c, "HTTP/1.0 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n", update(m, e)),
                    none => ui_reply(c, concat("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n\r\n", to_html(t, view(m), title)), m) } } }
    // serves the application on listener `l`: `count` requests (0 = until an accept fails)
    fn serve_app<M>(l: Nat, t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, title: Text, count: Nat, timeout_ms: Nat) -> M loop =
        match serve_once(l, t, m, view, update, title, timeout_ms) { err(e) => m, ok(next) =>
            if_then_else(nat_eq(count, 1), next, serve_app(l, t, next, view, update, title, pred(count), timeout_ms)) }

    theorem label_measures_its_text: equal(measure(default_theme(), Label("ab")), Size(24, 16))
}
