// Vector UI, stage G2 (v0.128, docs/GUI.md): procedural vector scenes (paths, Bézier curves,
// rounded rectangles, ellipses, gradients, stroked Unicode text with Cyrillic, clipping, raster
// images inside), HiDPI rendering at any scale, SVG export, incremental redraw of changed
// widgets, a vector look for `std.ui` widgets with hover/pressed/disabled states, and bounded
// exhaustive checking of UI state invariants. Scene units are 1/16 px.
// v0.129: TrueType text of any script (`*_f` functions take font ids from `file_font_load`,
// the first font with a glyph wins), scroll boxes, list views and tables, wheel and
// keyboard navigation, text input of every Unicode character in native windows.
module std.vector
import std.ui

theory StdVector extends StdUi {
    record PathB(n: Nat, data: NatVec)
    record VState(hover: Text, pressed: Text, disabled: List<Text>)
    record Trace<M>(model: M, path: List<UiEvent>)

    fn nvl(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn cat(a: NatVec, b: NatVec) -> NatVec = unwrap_or(nv_concat(a, b), a)
    fn cat_all(xs: List<NatVec>) -> NatVec = fold(xs, nv_empty(), fun(acc: NatVec, x: NatVec, cat(acc, x)))
    fn px(n: Nat) -> Nat = mul(n, 16)
    // paints
    fn solid(c: Nat) -> NatVec = nvl(list(0, c))
    fn linear(x0: Nat, y0: Nat, c0: Nat, x1: Nat, y1: Nat, c1: Nat) -> NatVec = nvl(list(1, x0, y0, c0, x1, y1, c1))
    fn radial(cx: Nat, cy: Nat, r: Nat, c0: Nat, c1: Nat) -> NatVec = nvl(list(2, cx, cy, r, c0, c1))
    // paths
    fn path_start() -> PathB = PathB(0, nv_empty())
    fn move_to(p: PathB, x: Nat, y: Nat) -> PathB = PathB(field(p, n) + 1, cat(field(p, data), nvl(list(0, x, y))))
    fn line_to(p: PathB, x: Nat, y: Nat) -> PathB = PathB(field(p, n) + 1, cat(field(p, data), nvl(list(1, x, y))))
    fn quad_to(p: PathB, cx: Nat, cy: Nat, x: Nat, y: Nat) -> PathB = PathB(field(p, n) + 1, cat(field(p, data), nvl(list(2, cx, cy, x, y))))
    fn cubic_to(p: PathB, c1x: Nat, c1y: Nat, c2x: Nat, c2y: Nat, x: Nat, y: Nat) -> PathB = PathB(field(p, n) + 1, cat(field(p, data), nvl(list(3, c1x, c1y, c2x, c2y, x, y))))
    fn close_path(p: PathB) -> PathB = PathB(field(p, n) + 1, cat(field(p, data), nvl(list(4))))
    fn path_data(p: PathB) -> NatVec = cat(nvl(list(field(p, n))), field(p, data))
    // commands (rule 0 nonzero, 1 even-odd)
    fn fill_path(rule: Nat, paint: NatVec, p: PathB) -> NatVec = cat_all(list(nvl(list(1, rule)), paint, path_data(p)))
    fn stroke_path(width: Nat, paint: NatVec, p: PathB) -> NatVec = cat_all(list(nvl(list(2, width)), paint, path_data(p)))
    fn rrect(x: Nat, y: Nat, w: Nat, h: Nat, r: Nat, paint: NatVec) -> NatVec = cat(nvl(list(3, x, y, w, h, r)), paint)
    fn ellipse_cmd(cx: Nat, cy: Nat, rx: Nat, ry: Nat, paint: NatVec) -> NatVec = cat(nvl(list(8, cx, cy, rx, ry)), paint)
    // a raster image (`gx_*` NatVec) stretched over the rectangle
    fn image_cmd(x: Nat, y: Nat, w: Nat, h: Nat, img: NatVec) -> NatVec = cat(nvl(list(4, x, y, w, h)), img)
    fn clip_cmd(x: Nat, y: Nat, w: Nat, h: Nat) -> NatVec = nvl(list(5, x, y, w, h))
    fn unclip_cmd() -> NatVec = nvl(list(6))
    // text cell height `size`, stroke `weight` (scene units)
    fn text_cmd(x: Nat, y: Nat, size: Nat, weight: Nat, paint: NatVec, t: Text) -> NatVec = vx_text(x, y, size, weight, paint, t)

    // ---- vector look of std.ui widgets ----
    fn ui_min_v(a: Nat, b: Nat) -> Nat = if_then_else(a > b, b, a)
    fn ui_max_v(a: Nat, b: Nat) -> Nat = if_then_else(a > b, a, b)
    private fn vc_mix(a: Nat, b: Nat, t: Nat) -> Nat =
        let ch = fun(s: Nat, mod(div(mul(mod(div(a, s), 256), monus(256, t)) + mul(mod(div(b, s), 256), t) + 128, 256), 256)) in
        mul(ch(16777216), 16777216) + mul(ch(65536), 65536) + mul(ch(256), 256) + ch(1)
    fn lighten(c: Nat, t: Nat) -> Nat = vc_mix(c, 4294967295, t)
    fn darken(c: Nat, t: Nat) -> Nat = vc_mix(c, 255, t)
    private fn vc_has(xs: List<Text>, x: Text) -> Bool = fold(xs, false, fun(acc: Bool, y: Text, or(acc, text_eq(x, y))))
    fn no_state() -> VState = VState("", "", empty_list(Text))
    private fn vc_text(t: Theme, x: Nat, y: Nat, s: Text, color: Nat) -> NatVec =
        text_cmd(px(x), px(y), px(line_height(t)), mul(field(t, scale), 13), solid(color), s)
    // TrueType text in a cell of height `size` (scene units), squeezed to `max_w` if wider (0: no limit)
    fn font_text_cmd(x: Nat, y: Nat, size: Nat, max_w: Nat, fonts: NatVec, paint: NatVec, t: Text) -> NatVec = vx_text_fonts(x, y, size, max_w, fonts, paint, t)
    fn vc_text_f(t: Theme, fonts: NatVec, x: Nat, y: Nat, s: Text, color: Nat) -> NatVec =
        if_then_else(nat_eq(nv_len(fonts), 0), vc_text(t, x, y, s, color), vx_text_fonts(px(x), px(y), px(line_height(t)), 0, fonts, solid(color), s))
    // scene of one placed element
    fn vector_item_f(t: Theme, fonts: NatVec, st: VState, p: Placed) -> NatVec =
        let r = field(p, rect) in let k = field(p, kind) in let pd = field(t, pad) in let lh = line_height(t) in let sc = field(t, scale) in
        let x = px(field(r, x)) in let y = px(field(r, y)) in let w = px(field(r, w)) in let h = px(field(r, h)) in
        let off = vc_has(field(st, disabled), field(p, id)) in
        let hot = and(not(off), text_eq(field(st, hover), field(p, id))) in let down = and(not(off), text_eq(field(st, pressed), field(p, id))) in
        let base = if_then_else(off, vc_mix(field(t, accent), field(t, panel), 160), if_then_else(down, darken(field(t, accent), 60), if_then_else(hot, lighten(field(t, accent), 40), field(t, accent)))) in
        if_then_else(nat_eq(k, 0), vc_text_f(t, fonts, field(r, x), field(r, y), field(p, text), field(t, fg)),
        if_then_else(nat_eq(k, 1), cat_all(list(
                rrect(x, y + px(1), w, h, px(mul(3, sc)), solid(darken(base, 90))),
                rrect(x, y, w, h, px(mul(3, sc)), linear(x, y, lighten(base, 50), x, y + h, base)),
                vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + pd + if_then_else(down, 1, 0), field(p, text), if_then_else(off, field(t, panel), field(t, on_accent))))),
        if_then_else(nat_eq(k, 2),
            let box = px(lh) in cat_all(list(
                rrect(x, y, box, box, px(sc + sc), solid(field(t, border))),
                rrect(x + px(1), y + px(1), monus(box, px(2)), monus(box, px(2)), px(sc + sc), solid(if_then_else(field(p, on), base, field(t, field)))),
                if_then_else(field(p, on), stroke_path(px(sc), solid(field(t, on_accent)),
                    line_to(line_to(move_to(path_start(), x + div(mul(box, 5), 20), y + div(box, 2)), x + div(mul(box, 9), 20), y + div(mul(box, 14), 20)), x + div(mul(box, 15), 20), y + div(mul(box, 6), 20))), nv_empty()),
                vc_text_f(t, fonts, field(r, x) + lh + pd, field(r, y), field(p, text), if_then_else(off, field(t, border), field(t, fg))))),
        if_then_else(nat_eq(k, 3), cat_all(list(
                rrect(x, y, w, h, px(sc + sc), solid(if_then_else(hot, field(t, accent), field(t, border)))),
                rrect(x + px(1), y + px(1), monus(w, px(2)), monus(h, px(2)), px(sc + sc), solid(field(t, field))),
                clip_cmd(x + px(1), y, monus(w, px(2)), h),
                vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + pd, field(p, text), field(t, fg)),
                unclip_cmd())),
        if_then_else(nat_eq(k, 4),
            let filled = if_then_else(nat_eq(field(p, max), 0), 0, div(mul(w, ui_min_v(field(p, value), field(p, max))), field(p, max))) in
            cat_all(list(rrect(x, y, w, h, div(h, 2), solid(field(t, field))),
                if_then_else(nat_eq(filled, 0), nv_empty(), rrect(x, y, filled, h, div(h, 2), linear(x, y, lighten(field(t, accent), 60), x + w, y, field(t, accent)))),
                stroke_path(px(1), solid(field(t, border)), close_path(line_to(line_to(line_to(move_to(path_start(), x, y), x + w, y), x + w, y + h), x, y + h))))),
        if_then_else(nat_eq(k, 6),
            let bar = lh + mul(2, pd) in cat_all(list(
                rrect(x + px(sc), y + px(sc + sc), w, h, px(mul(4, sc)), solid(darken(field(t, panel), 200))),
                rrect(x, y, w, h, px(mul(4, sc)), solid(field(t, panel))),
                if_then_else(nat_eq(text_length(field(p, text)), 0), nv_empty(), cat_all(list(
                    clip_cmd(x, y, w, px(bar)),
                    rrect(x, y, w, px(bar + mul(8, sc)), px(mul(4, sc)), linear(x, y, lighten(field(t, accent), 30), x, y + px(bar), field(t, accent))),
                    unclip_cmd(),
                    vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + pd, field(p, text), field(t, on_accent))))))),
        if_then_else(nat_eq(k, 7), let th = thumb_rect(t, r, field(p, value), field(p, max)) in cat_all(list(
                rrect(x, y, w, h, px(sc + sc), solid(field(t, border))),
                rrect(x + px(1), y + px(1), monus(w, px(2)), monus(h, px(2)), px(sc + sc), solid(field(t, field))),
                rrect(px(field(th, x)), px(field(th, y)), px(field(th, w)), px(field(th, h)), px(sc), solid(if_then_else(hot, field(t, accent), lighten(field(t, border), 60)))),
                clip_cmd(x + px(1), y + px(1), monus(w, px(2 + scrollbar_w(t))), monus(h, px(2))))),
        if_then_else(nat_eq(k, 8), unclip_cmd(),
        if_then_else(nat_eq(k, 9), cat_all(list(
                if_then_else(field(p, on), rrect(x, y, w, h, px(sc), linear(x, y, lighten(field(t, accent), 30), x, y + h, field(t, accent))),
                    if_then_else(nat_eq(mod(field(p, value), 2), 1), rrect(x, y, w, h, 0, solid(darken(field(t, field), 12))), nv_empty())),
                vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), if_then_else(field(p, on), field(t, on_accent), field(t, fg))))),
        if_then_else(nat_eq(k, 10), cat_all(list(clip_cmd(x, y, w, h), vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, fg)), unclip_cmd())),
        if_then_else(nat_eq(k, 11), cat_all(list(
                rrect(x, y, w, h, 0, linear(x, y, lighten(field(t, panel), 60), x, y + h, field(t, panel))),
                stroke_path(px(1), solid(field(t, border)), line_to(move_to(path_start(), x, monus(y + h, px(1))), x + w, monus(y + h, px(1)))),
                clip_cmd(x, y, w, h), vc_text_f(t, fonts, field(r, x) + pd, field(r, y) + div(pd, 2), field(p, text), field(t, fg)), unclip_cmd())),
            nv_empty())))))))))))
    fn vector_item(t: Theme, st: VState, p: Placed) -> NatVec = vector_item_f(t, nv_empty(), st, p)
    fn background(t: Theme, w: Nat, h: Nat) -> NatVec = rrect(0, 0, px(w), px(h), 0, solid(field(t, bg)))
    // scene chunks: background, then one per placed element (for incremental redraw)
    fn vector_chunks_f(t: Theme, fonts: NatVec, st: VState, w: Widget) -> List<NatVec> =
        let s = measure_f(t, fonts, w) in let m = field(t, pad) in
        cons(background(t, field(s, w) + mul(2, m), field(s, h) + mul(2, m)), map(layout_f(t, fonts, w, m, m), fun(p: Placed, vector_item_f(t, fonts, st, p))))
    fn vector_chunks(t: Theme, st: VState, w: Widget) -> List<NatVec> = vector_chunks_f(t, nv_empty(), st, w)
    fn vector_size_f(t: Theme, fonts: NatVec, w: Widget) -> Size = let s = measure_f(t, fonts, w) in Size(field(s, w) + mul(2, field(t, pad)), field(s, h) + mul(2, field(t, pad)))
    fn vector_size(t: Theme, w: Widget) -> Size = vector_size_f(t, nv_empty(), w)
    fn vector_scene_f(t: Theme, fonts: NatVec, st: VState, w: Widget) -> NatVec = cat_all(vector_chunks_f(t, fonts, st, w))
    fn vector_scene(t: Theme, st: VState, w: Widget) -> NatVec = vector_scene_f(t, nv_empty(), st, w)
    // raster at scale num/den (HiDPI: 5/4, 3/2, 2/1, ...)
    fn vector_render_f(t: Theme, fonts: NatVec, st: VState, w: Widget, num: Nat, den: Nat) -> NatVec =
        let s = vector_size_f(t, fonts, w) in unwrap_or(vx_render(vector_scene_f(t, fonts, st, w), div(monus(mul(field(s, w), num) + den, 1), den), div(monus(mul(field(s, h), num) + den, 1), den), num, den), nv_empty())
    fn vector_render(t: Theme, st: VState, w: Widget, num: Nat, den: Nat) -> NatVec = vector_render_f(t, nv_empty(), st, w, num, den)
    fn vector_svg_f(t: Theme, fonts: NatVec, st: VState, w: Widget) -> Text = let s = vector_size_f(t, fonts, w) in unwrap_or(vx_svg(vector_scene_f(t, fonts, st, w), field(s, w), field(s, h)), "")
    fn vector_svg(t: Theme, st: VState, w: Widget) -> Text = vector_svg_f(t, nv_empty(), st, w)
    fn vector_png_f(path: Text, t: Theme, fonts: NatVec, st: VState, w: Widget, num: Nat, den: Nat) -> Result<Nat, Text> = file_write_png(path, vector_render_f(t, fonts, st, w, num, den))
    fn vector_png(path: Text, t: Theme, st: VState, w: Widget, num: Nat, den: Nat) -> Result<Nat, Text> = vector_png_f(path, t, nv_empty(), st, w, num, den)

    // ---- incremental redraw ----
    private fn vc_union(a: NatVec, b: NatVec) -> NatVec =
        if_then_else(nat_eq(unwrap_or(nv_get(a, 2), 0), 0), b, if_then_else(nat_eq(unwrap_or(nv_get(b, 2), 0), 0), a,
            nvl(list(ui_min_v(unwrap_or(nv_get(a, 0), 0), unwrap_or(nv_get(b, 0), 0)), ui_min_v(unwrap_or(nv_get(a, 1), 0), unwrap_or(nv_get(b, 1), 0)),
                ui_max_v(unwrap_or(nv_get(a, 2), 0), unwrap_or(nv_get(b, 2), 0)), ui_max_v(unwrap_or(nv_get(a, 3), 0), unwrap_or(nv_get(b, 3), 0))))))
    private fn vc_bounds(c: NatVec, num: Nat, den: Nat) -> NatVec = unwrap_or(vx_bounds(c, num, den), nvl(list(0, 0, 0, 0)))
    // pixel box `[x0, y0, x1, y1]` covering every chunk that differs (old and new position)
    fn dirty(old: List<NatVec>, new: List<NatVec>, num: Nat, den: Nat) -> NatVec decreasing old =
        match old { [] => fold(new, nvl(list(0, 0, 0, 0)), fun(acc: NatVec, c: NatVec, vc_union(acc, vc_bounds(c, num, den)))), cons(o, rest) =>
            match new { [] => fold(old, nvl(list(0, 0, 0, 0)), fun(acc: NatVec, c: NatVec, vc_union(acc, vc_bounds(c, num, den)))), cons(n, more) =>
                let tail_box = dirty(rest, more, num, den) in
                if_then_else(equal(o, n), tail_box, vc_union(tail_box, vc_union(vc_bounds(o, num, den), vc_bounds(n, num, den)))) } }
    // the next frame: only the dirty box of `prev` is rasterized again
    fn redraw(prev: NatVec, old: List<NatVec>, new: List<NatVec>, num: Nat, den: Nat) -> NatVec =
        let b = dirty(old, new, num, den) in
        let x0 = unwrap_or(nv_get(b, 0), 0) in let y0 = unwrap_or(nv_get(b, 1), 0) in
        if_then_else(nat_eq(unwrap_or(nv_get(b, 2), 0), 0), prev,
            unwrap_or(vx_render_region(prev, cat_all(new), num, den, x0, y0, monus(unwrap_or(nv_get(b, 2), 0), x0), monus(unwrap_or(nv_get(b, 3), 0), y0)), prev))

    // ---- checked UI states: every event sequence up to `depth` keeps `ok` ----
    fn vc_expand<M>(front: List<Trace<M>>, update: Fn<M, UiEvent, M>, events: List<UiEvent>) -> List<Trace<M>> =
        fold(front, empty_list(Trace<M>), fun(acc: List<Trace<M>>, tr: Trace<M>,
            fold(events, acc, fun(a2: List<Trace<M>>, e: UiEvent, cons(Trace<M>(update(field(tr, model), e), cons(e, field(tr, path))), a2)))))
    fn vc_bad<M>(front: List<Trace<M>>, holds: Fn<M, Bool>) -> Option<List<UiEvent>> =
        fold(front, none(List(UiEvent)), fun(acc: Option<List<UiEvent>>, tr: Trace<M>, if_then_else(is_some(acc), acc, if_then_else(holds(field(tr, model)), acc, some(field(tr, path))))))
    fn vc_search<M>(front: List<Trace<M>>, update: Fn<M, UiEvent, M>, events: List<UiEvent>, holds: Fn<M, Bool>, depth: Nat) -> Option<List<UiEvent>> decreasing depth =
        match vc_bad(front, holds) { some(p) => some(p), none =>
            if_then_else(nat_eq(depth, 0), none(List(UiEvent)), vc_search(vc_expand(front, update, events), update, events, holds, pred(depth))) }
    fn vc_rev_events(xs: List<UiEvent>) -> List<UiEvent> = fold(xs, empty_list(UiEvent), fun(acc: List<UiEvent>, e: UiEvent, cons(e, acc)))
    // none: the invariant holds after every sequence of at most `depth` events; else a counterexample
    fn check_ui<M>(init: M, update: Fn<M, UiEvent, M>, events: List<UiEvent>, holds: Fn<M, Bool>, depth: Nat) -> Option<List<UiEvent>> =
        match vc_search(list(Trace<M>(init, empty_list(UiEvent))), update, events, holds, depth) { none => none(List(UiEvent)), some(p) => some(vc_rev_events(p)) }

    // ---- browser backend with the vector look: inline SVG + transparent hit areas ----
    private fn vc_box(r: Rect) -> Text =
        concat(concat(concat(concat(concat(concat(concat("position:absolute;left:", nat_to_text(field(r, x))), "px;top:"), nat_to_text(field(r, y))), "px;width:"), nat_to_text(field(r, w))), "px;height:"), concat(nat_to_text(field(r, h)), "px;"))
    fn vc_hit_html(t: Theme, p: Placed) -> Text =
        let k = field(p, kind) in let id = url_encode(field(p, id)) in let r = field(p, rect) in
        if_then_else(nat_eq(k, 1), concat(concat(concat(concat("<a href=\"/e/press/", id), "\" style=\""), vc_box(r)), "\"></a>"),
        if_then_else(nat_eq(k, 2), concat(concat(concat(concat("<a href=\"/e/toggle/", id), "\" style=\""), vc_box(r)), "\"></a>"),
        if_then_else(nat_eq(k, 3), concat(concat(concat(concat(concat(concat("<form method=\"get\" action=\"/e/input/", id), "\" style=\"margin:0\"><input name=\"v\" value=\""), html_escape(field(p, text))),
            "\" style=\""), concat(vc_box(r), concat(concat("border:0;background:transparent;color:transparent;caret-color:black;font:", nat_to_text(line_height(t))), "px monospace"))), "\"></form>"),
        if_then_else(nat_eq(k, 9), concat(concat(concat(concat(concat("<a href=\"/e/select/", nat_to_text(field(p, value))), concat("/", id)), "\" style=\""), vc_box(r)), "\"></a>"),
        if_then_else(nat_eq(k, 7), ui_wheel_links(t, p), "")))))
    fn vector_page_f(t: Theme, fonts: NatVec, w: Widget, title: Text) -> Text =
        let s = vector_size_f(t, fonts, w) in let m = field(t, pad) in
        concat(concat(concat(concat(concat(concat("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>", html_escape(title)), "</title></head><body style=\"margin:0\"><div style=\"position:relative;width:"),
            concat(nat_to_text(field(s, w)), "px;height:")), concat(nat_to_text(field(s, h)), "px\">")), vector_svg_f(t, fonts, no_state(), w)),
            concat(fold(layout_f(t, fonts, w, m, m), "", fun(acc: Text, p: Placed, concat(acc, vc_hit_html(t, p)))), "</div></body></html>"))
    fn vector_page(t: Theme, w: Widget, title: Text) -> Text = vector_page_f(t, nv_empty(), w, title)
    fn serve_vector_once<M>(l: Nat, t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, title: Text, timeout_ms: Nat) -> Result<M, Text> =
        match net_accept(l, timeout_ms) { err(e) => err(e, M), ok(c) =>
            match ui_request(c) { err(e) => let x = net_close(c) in err(e, M), ok(path) =>
                match route_event(path) {
                    some(e) => ui_reply(c, "HTTP/1.0 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n", update(m, e)),
                    none => ui_reply(c, concat("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n\r\n", vector_page(t, view(m), title)), m) } } }
    fn serve_vector_app<M>(l: Nat, t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, title: Text, count: Nat, timeout_ms: Nat) -> M loop =
        match serve_vector_once(l, t, m, view, update, title, timeout_ms) { err(e) => m, ok(next) =>
            if_then_else(nat_eq(count, 1), next, serve_vector_app(l, t, next, view, update, title, pred(count), timeout_ms)) }

    // ---- native window loop (X11 / Win32): mouse, hover, pressed state, keyboard into the focused field ----
    record WinState<M>(model: M, st: VState, focus: Text, frame: NatVec, chunks: List<NatVec>, size: Size, fonts: NatVec)
    fn utf8_of(cp: Nat) -> List<Nat> =
        if_then_else(128 > cp, list(cp), if_then_else(2048 > cp, list(192 + div(cp, 64), 128 + mod(cp, 64)),
            if_then_else(65536 > cp, list(224 + div(cp, 4096), 128 + mod(div(cp, 64), 64), 128 + mod(cp, 64)),
                list(240 + div(cp, 262144), 128 + mod(div(cp, 4096), 64), 128 + mod(div(cp, 64), 64), 128 + mod(cp, 64)))))
    fn char_text(cp: Nat) -> Text = let b = buf_from_list(utf8_of(cp)) in let r = buf_to_text(b) in let f = buf_free(b) in unwrap_or(r, "")
    fn vc_drop_cont(bs: List<Nat>) -> List<Nat> decreasing bs =
        match bs { [] => bs, cons(b, rest) => if_then_else(and(b > 127, 192 > b), vc_drop_cont(rest), rest) }
    fn vc_rev_nat(xs: List<Nat>) -> List<Nat> = fold(xs, empty_list(Nat), fun(acc: List<Nat>, x: Nat, cons(x, acc)))
    // the text without its last character
    fn drop_last_char(t: Text) -> Text =
        let b = buf_from_text(t) in let bytes = buf_to_list(b) in let o = buf_from_list(vc_rev_nat(vc_drop_cont(vc_rev_nat(bytes)))) in let r = buf_to_text(o) in let f = buf_free(o) in unwrap_or(r, t)
    // interactive kinds: button, check, field, row
    fn vc_interactive(k: Nat) -> Bool = or(or(nat_eq(k, 1), nat_eq(k, 2)), or(nat_eq(k, 3), nat_eq(k, 9)))
    fn placed_at(items: List<Placed>, x: Nat, y: Nat) -> Option<Placed> =
        fold(ui_at(items, x, y), none(Placed), fun(acc: Option<Placed>, p: Placed, if_then_else(and(inside(field(p, rect), x, y), vc_interactive(field(p, kind))), some(p), acc)))
    fn placed_kind(items: List<Placed>, id: Text, kind: Nat) -> Option<Placed> =
        fold(items, none(Placed), fun(acc: Option<Placed>, p: Placed, if_then_else(and(is_none(acc), and(nat_eq(field(p, kind), kind), text_eq(field(p, id), id))), some(p), acc)))
    fn vc_event(p: Placed) -> UiEvent =
        if_then_else(nat_eq(field(p, kind), 2), Toggle(field(p, id)), if_then_else(nat_eq(field(p, kind), 9), Select(field(p, id), field(p, value)), Press(field(p, id))))
    fn vc_selected(items: List<Placed>, id: Text) -> Option<Nat> =
        fold(items, none(Nat), fun(acc: Option<Nat>, p: Placed, if_then_else(and(and(nat_eq(field(p, kind), 9), field(p, on)), text_eq(field(p, id), id)), some(field(p, value)), acc)))
    fn vc_rows(items: List<Placed>, id: Text) -> Nat =
        fold(items, 0, fun(acc: Nat, p: Placed, if_then_else(and(nat_eq(field(p, kind), 9), text_eq(field(p, id), id)), acc + 1, acc)))
    // a key typed as text: code points without the function-key range (0x1FE00 + keysym)
    fn vc_is_text(cp: Nat) -> Bool = and(and(cp > 31, 1114112 > cp), not(and(cp > 130559, 131072 > cp)))
    // the row a navigation key (up, down, page up/down, home, end) selects in list `id`
    fn vc_nav(items: List<Placed>, id: Text, cp: Nat) -> Option<Nat> =
        let box = unwrap_or(placed_kind(items, id, 7), Placed(7, id, "", false, Rect(0, 0, 0, 0), 0, 0)) in let count = field(box, max) in
        let sel = unwrap_or(vc_selected(items, id), field(box, value)) in let vis = ui_max_v(vc_rows(items, id), 1) in let last = monus(count, 1) in
        if_then_else(nat_eq(count, 0), none(Nat),
        if_then_else(nat_eq(cp, 130898), some(monus(sel, 1)), if_then_else(nat_eq(cp, 130900), some(ui_min_v(sel + 1, last)),
        if_then_else(nat_eq(cp, 130901), some(monus(sel, vis)), if_then_else(nat_eq(cp, 130902), some(ui_min_v(sel + vis, last)),
        if_then_else(nat_eq(cp, 130896), some(0), if_then_else(nat_eq(cp, 130903), some(last), none(Nat))))))))
    fn placed_id(items: List<Placed>, id: Text) -> Option<Placed> =
        fold(items, none(Placed), fun(acc: Option<Placed>, p: Placed, if_then_else(text_eq(field(p, id), id), some(p), acc)))
    fn scaled_size(s: Size, num: Nat, den: Nat) -> Size = Size(div(monus(mul(field(s, w), num) + den, 1), den), div(monus(mul(field(s, h), num) + den, 1), den))
    // draws the new state: only changed widgets are rasterized again (full frame when the size changes)
    fn win_frame<M>(t: Theme, s: WinState<M>, view: Fn<M, Widget>, num: Nat, den: Nat) -> WinState<M> =
        let fs = field(s, fonts) in let w = view(field(s, model)) in let sz = vector_size_f(t, fs, w) in let ch = vector_chunks_f(t, fs, field(s, st), w) in
        if_then_else(equal(sz, field(s, size)),
            WinState<M>(field(s, model), field(s, st), field(s, focus), redraw(field(s, frame), field(s, chunks), ch, num, den), ch, sz, fs),
            let ps = scaled_size(sz, num, den) in WinState<M>(field(s, model), field(s, st), field(s, focus), unwrap_or(vx_render(cat_all(ch), field(ps, w), field(ps, h), num, den), nv_empty()), ch, sz, fs))
    fn win_with<M>(s: WinState<M>, m: M, st: VState, focus: Text) -> WinState<M> = WinState<M>(m, st, focus, field(s, frame), field(s, chunks), field(s, size), field(s, fonts))
    fn win_handle<M>(t: Theme, s: WinState<M>, e: NatVec, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, num: Nat, den: Nat) -> WinState<M> =
        let k = unwrap_or(nv_get(e, 0), 0) in let x = div(mul(unwrap_or(nv_get(e, 1), 0), den), num) in let y = div(mul(unwrap_or(nv_get(e, 2), 0), den), num) in
        let m = field(s, model) in let st = field(s, st) in let foc = field(s, focus) in let items = layout_f(t, field(s, fonts), view(m), field(t, pad), field(t, pad)) in
        if_then_else(nat_eq(k, 2), match placed_at(items, x, y) {
                none => win_with(s, m, VState(field(st, hover), "", field(st, disabled)), ""),
                some(p) => win_with(s, update(m, vc_event(p)), VState(field(st, hover), field(p, id), field(st, disabled)),
                    if_then_else(or(nat_eq(field(p, kind), 3), nat_eq(field(p, kind), 9)), field(p, id), foc)) },
        if_then_else(nat_eq(k, 3), win_with(s, m, VState(field(st, hover), "", field(st, disabled)), foc),
        if_then_else(nat_eq(k, 4), win_with(s, m, VState(match placed_at(items, x, y) { none => "", some(p) => field(p, id) }, field(st, pressed), field(st, disabled)), foc),
        if_then_else(nat_eq(k, 8), let dir = unwrap_or(nv_get(e, 3), 0) in
            if_then_else(or(nat_eq(dir, 1), nat_eq(dir, 2)), match wheel_at(items, x, y, nat_eq(dir, 2)) { none => s, some(ev) => win_with(s, update(m, ev), st, foc) }, s),
        if_then_else(and(nat_eq(k, 5), text_length(foc) > 0),
            let cp = unwrap_or(nv_get(e, 1), 0) in
            if_then_else(is_some(placed_kind(items, foc, 7)),
                match vc_nav(items, foc, cp) { none => s, some(i) => win_with(s, update(m, Select(foc, i)), st, foc) },
                let cur = match placed_id(items, foc) { none => "", some(p) => field(p, text) } in
                if_then_else(nat_eq(cp, 130824), win_with(s, update(m, Input(foc, drop_last_char(cur))), st, foc),
                if_then_else(nat_eq(cp, 130829), win_with(s, update(m, Press(foc)), st, foc),
                if_then_else(vc_is_text(cp), win_with(s, update(m, Input(foc, concat(cur, char_text(cp)))), st, foc), s)))),
            s)))))
    fn win_loop<M>(id: Nat, t: Theme, s: WinState<M>, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, num: Nat, den: Nat, left: Nat) -> M loop =
        match win_poll(id, 30) { err(e) => field(s, model), ok(e) =>
            let k = unwrap_or(nv_get(e, 0), 0) in
            if_then_else(nat_eq(k, 1), field(s, model),
                let next = if_then_else(or(nat_eq(k, 0), or(nat_eq(k, 6), nat_eq(k, 7))), s, win_frame(t, win_handle(t, s, e, view, update, num, den), view, num, den)) in
                let shown = if_then_else(or(nat_eq(k, 0), nat_eq(k, 1)), 0, match win_present(id, field(next, frame)) { ok(n) => n, err(x) => 0 }) in
                if_then_else(nat_eq(left, 1), field(next, model), win_loop(id, t, next, view, update, num, den, pred(left)))) }
    // opens a window showing `view(model)` at scale num/den and runs until it is closed
    // (or after `max_events` events, 0 = no limit); the final model
    // the same with TrueType fonts (ids of `file_font_load`, first font with a glyph wins)
    fn run_window_f<M>(title: Text, t: Theme, fonts: NatVec, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, num: Nat, den: Nat, max_events: Nat) -> Result<M, Text> =
        let w = view(m) in let sz = vector_size_f(t, fonts, w) in let ps = scaled_size(sz, num, den) in let ch = vector_chunks_f(t, fonts, no_state(), w) in
        let frame = unwrap_or(vx_render(cat_all(ch), field(ps, w), field(ps, h), num, den), nv_empty()) in
        match win_open(title, field(ps, w), field(ps, h)) { err(e) => err(e, M), ok(id) =>
            let p = win_present(id, frame) in
            let out = win_loop(id, t, WinState<M>(m, no_state(), "", frame, ch, sz, fonts), view, update, num, den, max_events) in
            let c = win_close(id) in ok(out, Text) }

    fn run_window<M>(title: Text, t: Theme, m: M, view: Fn<M, Widget>, update: Fn<M, UiEvent, M>, num: Nat, den: Nat, max_events: Nat) -> Result<M, Text> =
        run_window_f(title, t, nv_empty(), m, view, update, num, den, max_events)

    theorem lighten_white_is_white: nat_eq(lighten(4294967295, 100), 4294967295)
}
