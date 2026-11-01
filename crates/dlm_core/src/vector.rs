//! v0.128: vector scenes for the DLM UI (docs/GUI.md, stage G2). A scene is a
//! `NatVec` of drawing commands in 1/16 px units; it is rasterized with exact
//! horizontal coverage and 16 sub-scanlines per pixel (anti-aliased), entirely
//! in integer arithmetic, so the native runtime (`dvy_*`) produces the same
//! pixels. The same scene exports to SVG.
//!
//! Commands (`P` = paint, `path` = `[n, verbs...]`, verbs `0 x y` move,
//! `1 x y` line, `2 cx cy x y` quadratic, `3 c1x c1y c2x c2y x y` cubic, `4` close):
//! `1 rule P path` fill (rule 0 nonzero, 1 even-odd) · `2 width P path` stroke
//! (round joins and caps) · `3 x y w h r P` rounded rectangle · `4 x y w h iw ih px...`
//! image (nearest) · `5 x y w h` push clip · `6` pop clip · `7 x y size weight P n cp...`
//! text (stroked 5x7 glyphs, ASCII and Cyrillic) · `8 cx cy rx ry P` ellipse.
//! `9 x y size max_w nf font... P n cp...` text in loaded TrueType fonts (first font
//! with the glyph, else the stroked glyph; squeezed horizontally to `max_w` if wider).
//! Paints: `0 color` · `1 x0 y0 c0 x1 y1 c1` linear · `2 cx cy r c0 c1` radial.
use crate::font::{Font, FontTable};
use crate::gfx;

const COS: [i64; 33] = [65536, 64277, 60547, 54491, 46341, 36410, 25080, 12785, 0, -12785, -25080, -36410, -46341, -54491, -60547, -64277, -65536, -64277, -60547, -54491, -46341, -36410, -25080, -12785, 0, 12785, 25080, 36410, 46341, 54491, 60547, 64277, 65536];
const SIN: [i64; 33] = [0, 12785, 25080, 36410, 46341, 54491, 60547, 64277, 65536, 64277, 60547, 54491, 46341, 36410, 25080, 12785, 0, -12785, -25080, -36410, -46341, -54491, -60547, -64277, -65536, -64277, -60547, -54491, -46341, -36410, -25080, -12785, 0];
const LIMIT: i64 = 1 << 40;

#[derive(Clone, Copy)]
enum Paint {
    Solid(u128),
    Linear(i64, i64, u128, i64, i64, u128),
    Radial(i64, i64, i64, u128, u128),
}

type Poly = Vec<(i64, i64)>;

enum Op {
    Shape { polys: Vec<Poly>, evenodd: bool, paint: Paint },
    Image { x0: i64, y0: i64, x1: i64, y1: i64, iw: usize, ih: usize, px: Vec<u128> },
    Clip(i64, i64, i64, i64),
    Unclip,
}

struct Reader<'a> {
    v: &'a [u128],
    i: usize,
}

impl Reader<'_> {
    fn next(&mut self) -> Option<u128> {
        let x = *self.v.get(self.i)?;
        self.i += 1;
        Some(x)
    }
}

fn isqrt(n: u128) -> u128 {
    if n < 2 {
        return n;
    }
    let mut x = 1u128 << (128 - n.leading_zeros()).div_ceil(2);
    loop {
        let y = (x + n / x) >> 1;
        if y >= x {
            return x;
        }
        x = y;
    }
}

fn rot(x: i64, y: i64, k: usize) -> (i64, i64) {
    let (c, s) = (i128::from(COS[k]), i128::from(SIN[k]));
    let (x, y) = (i128::from(x), i128::from(y));
    (((x * c + y * s) / 65536) as i64, ((-x * s + y * c) / 65536) as i64)
}

fn circle(cx: i64, cy: i64, r: i64) -> Poly {
    (0..32).map(|k| {
        let (dx, dy) = rot(0, r, k);
        (cx + dx, cy + dy)
    }).collect()
}

/// Convex stadium around segment `p..q` with half width `h` (consistent orientation).
fn capsule(p: (i64, i64), q: (i64, i64), h: i64) -> Poly {
    let (dx, dy) = (q.0 - p.0, q.1 - p.1);
    let len = isqrt((i128::from(dx) * i128::from(dx) + i128::from(dy) * i128::from(dy)) as u128) as i64;
    if len == 0 {
        return circle(p.0, p.1, h);
    }
    let nx = (-i128::from(dy) * i128::from(h) / i128::from(len)) as i64;
    let ny = (i128::from(dx) * i128::from(h) / i128::from(len)) as i64;
    let mut out = Vec::with_capacity(34);
    for k in 0..=16 {
        let (a, b) = rot(nx, ny, k);
        out.push((q.0 + a, q.1 + b));
    }
    for k in 0..=16 {
        let (a, b) = rot(-nx, -ny, k);
        out.push((p.0 + a, p.1 + b));
    }
    out
}

struct Scale {
    num: i128,
    den: i128,
}

impl Scale {
    fn ci(&self, v: i128) -> i64 {
        let v = v.clamp(-(1 << 60), 1 << 60);
        (v * 16 * self.num / self.den).clamp(-i128::from(LIMIT), i128::from(LIMIT)) as i64
    }
    fn c(&self, v: u128) -> i64 {
        let v = i128::try_from(v.min(1 << 60)).unwrap_or(0);
        (v * 16 * self.num / self.den).clamp(-i128::from(LIMIT), i128::from(LIMIT)) as i64
    }
}

fn color(r: &mut Reader) -> Option<u128> {
    r.next().filter(|c| *c <= 0xFFFF_FFFF)
}

fn paint(r: &mut Reader, s: &Scale) -> Option<Paint> {
    Some(match r.next()? {
        0 => Paint::Solid(color(r)?),
        1 => {
            let (x0, y0) = (s.c(r.next()?), s.c(r.next()?));
            let c0 = color(r)?;
            let (x1, y1) = (s.c(r.next()?), s.c(r.next()?));
            Paint::Linear(x0, y0, c0, x1, y1, color(r)?)
        }
        2 => {
            let (cx, cy, rad) = (s.c(r.next()?), s.c(r.next()?), s.c(r.next()?));
            let c0 = color(r)?;
            Paint::Radial(cx, cy, rad, c0, color(r)?)
        }
        _ => return None,
    })
}

fn steps(legs: &[(i64, i64)]) -> i128 {
    let est: i128 = legs.windows(2).map(|w| i128::from((w[1].0 - w[0].0).abs() + (w[1].1 - w[0].1).abs())).sum();
    (est / 512 + 1).clamp(1, 64)
}

/// Flattened subpaths `(points, closed)` of a path in pixel units ×256.
fn path(r: &mut Reader, s: &Scale) -> Option<Vec<(Poly, bool)>> {
    let n = r.next()?;
    if n > 1 << 20 {
        return None;
    }
    let mut out: Vec<(Poly, bool)> = Vec::new();
    let mut cur: Poly = Vec::new();
    let mut at = (0i64, 0i64);
    let flush = |cur: &mut Poly, out: &mut Vec<(Poly, bool)>, closed: bool| {
        if !cur.is_empty() {
            out.push((std::mem::take(cur), closed));
        }
    };
    for _ in 0..n {
        match r.next()? {
            0 => {
                flush(&mut cur, &mut out, false);
                at = (s.c(r.next()?), s.c(r.next()?));
                cur.push(at);
            }
            1 => {
                if cur.is_empty() {
                    cur.push(at);
                }
                at = (s.c(r.next()?), s.c(r.next()?));
                cur.push(at);
            }
            2 => {
                let c = (s.c(r.next()?), s.c(r.next()?));
                let e = (s.c(r.next()?), s.c(r.next()?));
                if cur.is_empty() {
                    cur.push(at);
                }
                let n = steps(&[at, c, e]);
                for i in 1..=n {
                    let (a, b, cc) = ((n - i) * (n - i), 2 * i * (n - i), i * i);
                    let f = |p: i64, q: i64, t: i64| ((i128::from(p) * a + i128::from(q) * b + i128::from(t) * cc) / (n * n)) as i64;
                    cur.push((f(at.0, c.0, e.0), f(at.1, c.1, e.1)));
                }
                at = e;
            }
            3 => {
                let c1 = (s.c(r.next()?), s.c(r.next()?));
                let c2 = (s.c(r.next()?), s.c(r.next()?));
                let e = (s.c(r.next()?), s.c(r.next()?));
                if cur.is_empty() {
                    cur.push(at);
                }
                let n = steps(&[at, c1, c2, e]);
                for i in 1..=n {
                    let m = n - i;
                    let (a, b, c, d) = (m * m * m, 3 * i * m * m, 3 * i * i * m, i * i * i);
                    let f = |p: i64, q: i64, t: i64, u: i64| ((i128::from(p) * a + i128::from(q) * b + i128::from(t) * c + i128::from(u) * d) / (n * n * n)) as i64;
                    cur.push((f(at.0, c1.0, c2.0, e.0), f(at.1, c1.1, c2.1, e.1)));
                }
                at = e;
            }
            4 => {
                if let Some(&first) = cur.first() {
                    at = first;
                }
                flush(&mut cur, &mut out, true);
            }
            _ => return None,
        }
    }
    flush(&mut cur, &mut out, false);
    Some(out)
}

fn rrect(x0: i64, y0: i64, x1: i64, y1: i64, r: i64) -> Poly {
    let r = r.min((x1 - x0) / 2).min((y1 - y0) / 2).max(0);
    if r == 0 {
        return vec![(x0, y0), (x1, y0), (x1, y1), (x0, y1)];
    }
    let k = r * 1105 / 2000;
    let mut out = Vec::new();
    let cubic = |p: (i64, i64), c1: (i64, i64), c2: (i64, i64), e: (i64, i64), out: &mut Poly| {
        let n = steps(&[p, c1, c2, e]);
        for i in 1..=n {
            let m = n - i;
            let (a, b, c, d) = (m * m * m, 3 * i * m * m, 3 * i * i * m, i * i * i);
            let f = |p: i64, q: i64, t: i64, u: i64| ((i128::from(p) * a + i128::from(q) * b + i128::from(t) * c + i128::from(u) * d) / (n * n * n)) as i64;
            out.push((f(p.0, c1.0, c2.0, e.0), f(p.1, c1.1, c2.1, e.1)));
        }
    };
    out.push((x0 + r, y0));
    out.push((x1 - r, y0));
    cubic((x1 - r, y0), (x1 - r + k, y0), (x1, y0 + r - k), (x1, y0 + r), &mut out);
    out.push((x1, y1 - r));
    cubic((x1, y1 - r), (x1, y1 - r + k), (x1 - r + k, y1), (x1 - r, y1), &mut out);
    out.push((x0 + r, y1));
    cubic((x0 + r, y1), (x0 + r - k, y1), (x0, y1 - r + k), (x0, y1 - r), &mut out);
    out.push((x0, y0 + r));
    cubic((x0, y0 + r), (x0, y0 + r - k), (x0 + r - k, y0), (x0 + r, y0), &mut out);
    out
}

fn ellipse(cx: i64, cy: i64, rx: i64, ry: i64) -> Poly {
    let (kx, ky) = (rx * 1105 / 2000, ry * 1105 / 2000);
    let pts = [(cx + rx, cy), (cx, cy + ry), (cx - rx, cy), (cx, cy - ry), (cx + rx, cy)];
    let ctl = [((cx + rx, cy + ky), (cx + kx, cy + ry)), ((cx - kx, cy + ry), (cx - rx, cy + ky)), ((cx - rx, cy - ky), (cx - kx, cy - ry)), ((cx + kx, cy - ry), (cx + rx, cy - ky))];
    let mut out = vec![pts[0]];
    for q in 0..4 {
        let (p, e) = (pts[q], pts[q + 1]);
        let (c1, c2) = ctl[q];
        let n = steps(&[p, c1, c2, e]);
        for i in 1..=n {
            let m = n - i;
            let (a, b, c, d) = (m * m * m, 3 * i * m * m, 3 * i * i * m, i * i * i);
            let f = |p: i64, q: i64, t: i64, u: i64| ((i128::from(p) * a + i128::from(q) * b + i128::from(t) * c + i128::from(u) * d) / (n * n * n)) as i64;
            out.push((f(p.0, c1.0, c2.0, e.0), f(p.1, c1.1, c2.1, e.1)));
        }
    }
    out
}

/// Stroke outlines: one capsule per segment (and closing segment).
fn stroke(lines: &[(Poly, bool)], half: i64) -> Vec<Poly> {
    let mut out = Vec::new();
    for (pts, closed) in lines {
        if pts.len() == 1 {
            out.push(circle(pts[0].0, pts[0].1, half));
        }
        for w in pts.windows(2) {
            out.push(capsule(w[0], w[1], half));
        }
        if *closed && pts.len() > 2 {
            out.push(capsule(pts[pts.len() - 1], pts[0], half));
        }
    }
    out
}

/// Code points of the glyph segments of `text` (scene units); `None` ends a line.
pub fn text_segments(x: u128, y: u128, size: u128, cps: &[u128]) -> Vec<((u128, u128), (u128, u128))> {
    let mut out = Vec::new();
    let (x, y, size) = (x.min(1 << 60), y.min(1 << 60), size.min(1 << 60));
    let (mut col, mut line) = (0u128, 0u128);
    for &cp in cps {
        if cp == 10 {
            col = 0;
            line += 1;
            continue;
        }
        let g = gfx::glyph(char::from_u32(u32::try_from(cp).unwrap_or(0xFFFD)).unwrap_or('\u{FFFD}'));
        let (cx, cy) = (x + col * size * 6 / 8, y + line * size);
        let on = |c: i64, r: i64| (0..5).contains(&c) && (0..7).contains(&r) && g[c as usize] >> r & 1 == 1;
        let at = |c: i64, r: i64| (cx + (2 * c as u128 + 1) * size / 16, cy + (2 * r as u128 + 1) * size / 16);
        for c in 0..5i64 {
            for r in 0..7i64 {
                if !on(c, r) {
                    continue;
                }
                let mut linked = false;
                if on(c + 1, r) {
                    out.push((at(c, r), at(c + 1, r)));
                    linked = true;
                }
                if on(c, r + 1) {
                    out.push((at(c, r), at(c, r + 1)));
                    linked = true;
                }
                if on(c + 1, r + 1) && !on(c + 1, r) && !on(c, r + 1) {
                    out.push((at(c, r), at(c + 1, r + 1)));
                    linked = true;
                }
                if on(c - 1, r + 1) && !on(c - 1, r) && !on(c, r + 1) {
                    out.push((at(c, r), at(c - 1, r + 1)));
                    linked = true;
                }
                let touched = linked || on(c - 1, r) || on(c, r - 1) || (on(c - 1, r - 1) && !on(c - 1, r) && !on(c, r - 1)) || (on(c + 1, r - 1) && !on(c + 1, r) && !on(c, r - 1));
                if !touched {
                    out.push((at(c, r), at(c, r)));
                }
            }
        }
        col += 1;
    }
    out
}

/// The text command `[7, x, y, size, weight, paint..., n, code points...]`.
pub fn text_command(x: u128, y: u128, size: u128, weight: u128, paint: &[u128], text: &str) -> Vec<u128> {
    let mut out = vec![7, x, y, size, weight];
    out.extend_from_slice(paint);
    out.push(text.chars().count() as u128);
    out.extend(text.chars().map(|c| u128::from(c as u32)));
    out
}

/// One laid-out piece of font text: a TrueType glyph (font, glyph, origin) or a
/// stroked fallback glyph (code point, cell origin); scene units.
enum Piece<'a> {
    Outline(&'a Font, u32, i128, i128),
    Stroke(u128, i128, i128),
}

struct FontText<'a> {
    pieces: Vec<Piece<'a>>,
    size: u128,
    em: i128,
    fit: (i128, i128),
    x: i128,
}

fn pick<'a>(fonts: &'a FontTable, ids: &[u128], cp: u128) -> Option<(&'a Font, u32)> {
    let c = u32::try_from(cp).ok()?;
    ids.iter().filter_map(|id| fonts.get(*id)).find_map(|f| {
        let g = f.glyph_index(c);
        (g != 0).then_some((f, g))
    })
}

/// Natural width (scene units) of the longest line.
pub fn font_text_width(fonts: &FontTable, ids: &[u128], cps: &[u128], size: u128) -> u128 {
    let size = size.min(FONT_SIZE_MAX);
    let em = (size as i128) * 27 / 32;
    let (mut best, mut cur) = (0i128, 0i128);
    for &cp in cps {
        if cp == 10 {
            cur = 0;
            continue;
        }
        cur += match pick(fonts, ids, cp) {
            Some((f, g)) => f.advance(g) as i128 * em / f.upem as i128,
            None => (size as i128) * 6 / 8,
        };
        best = best.max(cur);
    }
    best.max(0) as u128
}

/// Clamps keeping the integer layout arithmetic inside i128.
pub const FONT_SIZE_MAX: u128 = 1 << 24;
const FONT_POS_MAX: u128 = 1 << 60;

fn layout_font_text<'a>(fonts: &'a FontTable, ids: &[u128], x: u128, y: u128, size: u128, max_w: u128, cps: &[u128]) -> FontText<'a> {
    let (x, y, size, max_w) = (x.min(FONT_POS_MAX), y.min(FONT_POS_MAX), size.min(FONT_SIZE_MAX), max_w.min(FONT_POS_MAX));
    let natural = font_text_width(fonts, ids, cps, size) as i128;
    let fit = if max_w > 0 && natural > max_w as i128 { (max_w as i128, natural) } else { (1, 1) };
    let em = (size as i128) * 27 / 32;
    let base = ids.iter().find_map(|id| fonts.get(*id)).map_or(0, |f| em * f.ascender as i128 / f.upem as i128);
    let (mut pen, mut line) = (0i128, 0i128);
    let mut pieces = Vec::new();
    for &cp in cps {
        if cp == 10 {
            pen = 0;
            line += 1;
            continue;
        }
        let y_line = y as i128 + line * size as i128;
        match pick(fonts, ids, cp) {
            Some((f, g)) => {
                pieces.push(Piece::Outline(f, g, pen, y_line + base));
                pen += f.advance(g) as i128 * em / f.upem as i128;
            }
            None => {
                pieces.push(Piece::Stroke(cp, pen, y_line));
                pen += (size as i128) * 6 / 8;
            }
        }
    }
    FontText { pieces, size, em, fit, x: x as i128 }
}

/// Polygon of one TrueType contour (px ×256), quadratic segments flattened.
fn contour_poly(pts: &[(i64, i64, bool)]) -> Poly {
    let n = pts.len();
    let mut out = Vec::new();
    if n == 0 {
        return out;
    }
    let mid = |a: (i64, i64, bool), b: (i64, i64, bool)| ((a.0 + b.0) / 2, (a.1 + b.1) / 2, true);
    let (start, seq): ((i64, i64, bool), Vec<(i64, i64, bool)>) = match pts.iter().position(|p| p.2) {
        Some(k) => (pts[k], (1..=n).map(|i| pts[(k + i) % n]).collect()),
        None => {
            let s = mid(pts[n - 1], pts[0]);
            (s, pts.iter().copied().chain(std::iter::once(s)).collect())
        }
    };
    let mut cur = (start.0, start.1);
    out.push(cur);
    let mut pending: Option<(i64, i64)> = None;
    let quad = |a: (i64, i64), c: (i64, i64), e: (i64, i64), out: &mut Poly| {
        let n = steps(&[a, c, e]);
        for i in 1..=n {
            let (p, q, r) = ((n - i) * (n - i), 2 * i * (n - i), i * i);
            let f = |u: i64, v: i64, w: i64| ((i128::from(u) * p + i128::from(v) * q + i128::from(w) * r) / (n * n)) as i64;
            out.push((f(a.0, c.0, e.0), f(a.1, c.1, e.1)));
        }
    };
    for p in seq {
        let pt = (p.0, p.1);
        if p.2 {
            match pending.take() {
                Some(c) => quad(cur, c, pt, &mut out),
                None => out.push(pt),
            }
            cur = pt;
        } else {
            if let Some(c) = pending {
                let m = ((c.0 + pt.0) / 2, (c.1 + pt.1) / 2);
                quad(cur, c, m, &mut out);
                cur = m;
            }
            pending = Some(pt);
        }
    }
    out
}

// Divide before multiplying: malformed font coordinates can exceed 64 bits.
fn fit_width(v: i128, fit: (i128, i128)) -> i128 {
    (v / fit.1) * fit.0 + (v % fit.1) * fit.0 / fit.1
}

fn font_text_polys(ft: &FontText, s: &Scale) -> Vec<Poly> {
    let size = ft.size;
    let mut polys = Vec::new();
    let half = (s.c(size / 10) / 2).max(1);
    for piece in &ft.pieces {
        match *piece {
            Piece::Outline(f, g, pen, base) => {
                for contour in f.outline(g) {
                    let pts: Vec<(i64, i64, bool)> = contour.iter().map(|p| {
                        let gx = i128::from(p.x) * ft.em / i128::from(f.upem);
                        let gy = i128::from(p.y) * ft.em / i128::from(f.upem);
                        (s.ci(ft.x + fit_width(pen + gx, ft.fit)), s.ci(base - gy), p.on)
                    }).collect();
                    polys.push(contour_poly(&pts));
                }
            }
            Piece::Stroke(cp, pen, y_line) => {
                let ox = (ft.x + fit_width(pen, ft.fit)).max(0) as u128;
                for (a, b) in text_segments(ox, y_line.max(0) as u128, size, &[cp]) {
                    polys.push(capsule((s.c(a.0), s.c(a.1)), (s.c(b.0), s.c(b.1)), half));
                }
            }
        }
    }
    polys
}

/// The font text command `[9, x, y, size, max_w, nf, fonts..., paint..., n, code points...]`.
#[allow(clippy::too_many_arguments)]
pub fn font_text_command(x: u128, y: u128, size: u128, max_w: u128, fonts: &[u128], paint: &[u128], text: &str) -> Vec<u128> {
    let cps = visual(&text.chars().map(|c| u128::from(c as u32)).collect::<Vec<_>>());
    let mut out = vec![9, x, y, size, max_w, fonts.len() as u128];
    out.extend_from_slice(fonts);
    out.extend_from_slice(paint);
    out.push(cps.len() as u128);
    out.extend(cps);
    out
}

// ---- v0.129: display order and Arabic joining for font text ----
// Simplified Unicode bidi per line (paragraph direction from the first strong character,
// neutrals between equal directions take it, numbers stay left-to-right, runs reversed by
// level with combining marks kept after their base, bracket mirroring) and Arabic/Persian
// contextual forms (Presentation Forms A/B, lam-alef ligatures). The native runtime
// (`dvtf_visual`) applies the same rules.

/// Arabic letters: (letter, isolated presentation form, joining 0 none / 1 right / 2 dual).
const ARABIC: [(u32, u32, u8); 43] = [
    (0x0621, 0xFE80, 0), (0x0622, 0xFE81, 1), (0x0623, 0xFE83, 1), (0x0624, 0xFE85, 1), (0x0625, 0xFE87, 1), (0x0626, 0xFE89, 2),
    (0x0627, 0xFE8D, 1), (0x0628, 0xFE8F, 2), (0x0629, 0xFE93, 1), (0x062A, 0xFE95, 2), (0x062B, 0xFE99, 2), (0x062C, 0xFE9D, 2),
    (0x062D, 0xFEA1, 2), (0x062E, 0xFEA5, 2), (0x062F, 0xFEA9, 1), (0x0630, 0xFEAB, 1), (0x0631, 0xFEAD, 1), (0x0632, 0xFEAF, 1),
    (0x0633, 0xFEB1, 2), (0x0634, 0xFEB5, 2), (0x0635, 0xFEB9, 2), (0x0636, 0xFEBD, 2), (0x0637, 0xFEC1, 2), (0x0638, 0xFEC5, 2),
    (0x0639, 0xFEC9, 2), (0x063A, 0xFECD, 2), (0x0641, 0xFED1, 2), (0x0642, 0xFED5, 2), (0x0643, 0xFED9, 2), (0x0644, 0xFEDD, 2),
    (0x0645, 0xFEE1, 2), (0x0646, 0xFEE5, 2), (0x0647, 0xFEE9, 2), (0x0648, 0xFEED, 1), (0x0649, 0xFEEF, 1), (0x064A, 0xFEF1, 2),
    (0x067E, 0xFB56, 2), (0x0686, 0xFB7A, 2), (0x0698, 0xFB8A, 1), (0x06A9, 0xFB8E, 2), (0x06AF, 0xFB92, 2), (0x06CC, 0xFBFC, 2),
    (0x06C0, 0xFBA4, 1),
];

fn arabic(cp: u128) -> Option<(u128, u8)> {
    ARABIC.iter().find(|e| u128::from(e.0) == cp).map(|e| (u128::from(e.1), e.2))
}

fn is_mark(cp: u128) -> bool {
    matches!(cp, 0x0300..=0x036F | 0x0483..=0x0489 | 0x0591..=0x05BD | 0x05BF | 0x05C1..=0x05C2 | 0x05C4..=0x05C5 | 0x05C7 | 0x0610..=0x061A
        | 0x064B..=0x065F | 0x0670 | 0x06D6..=0x06DC | 0x06DF..=0x06E4 | 0x06E7..=0x06E8 | 0x06EA..=0x06ED | 0x0E31 | 0x0E34..=0x0E3A
        | 0x0E47..=0x0E4E | 0x1AB0..=0x1AFF | 0x1DC0..=0x1DFF | 0x200C..=0x200D | 0x20D0..=0x20FF | 0xFE00..=0xFE0F | 0xFE20..=0xFE2F)
}

/// Joins forward to the next letter: dual-joining letters, tatweel, ZWJ.
fn joins_next(cp: u128) -> bool {
    cp == 0x0640 || cp == 0x200D || arabic(cp).is_some_and(|a| a.1 == 2)
}

fn joins_prev(cp: u128) -> bool {
    cp == 0x0640 || cp == 0x200D || arabic(cp).is_some_and(|a| a.1 >= 1)
}

fn shape_arabic(line: &[u128]) -> Vec<u128> {
    let n = line.len();
    let mut prev = 0;
    let mut prev_join = vec![false; n];
    let mut next_join = vec![false; n];
    for i in 0..n {
        prev_join[i] = joins_next(prev);
        if !is_mark(line[i]) || matches!(line[i], 0x200C..=0x200D) { prev = line[i]; }
    }
    prev = 0;
    for i in (0..n).rev() {
        next_join[i] = joins_prev(prev);
        if !is_mark(line[i]) || matches!(line[i], 0x200C..=0x200D) { prev = line[i]; }
    }
    let mut out = Vec::with_capacity(n);
    let mut i = 0;
    while i < n {
        let c = line[i];
        let joined_prev = prev_join[i];
        if c == 0x0644 && i + 1 < n {
            let lig = match line[i + 1] {
                0x0622 => 0xFEF5,
                0x0623 => 0xFEF7,
                0x0625 => 0xFEF9,
                0x0627 => 0xFEFB,
                _ => 0,
            };
            if lig != 0 {
                out.push(lig + u128::from(joined_prev));
                i += 2;
                continue;
            }
        }
        match arabic(c) {
            Some((iso, kind)) if kind > 0 => {
                let p = joined_prev;
                let q = kind == 2 && next_join[i];
                out.push(iso + match (p, q) {
                    (false, false) => 0,
                    (true, false) => 1,
                    (false, true) => 2,
                    (true, true) => 3,
                });
            }
            _ => out.push(c),
        }
        i += 1;
    }
    out
}

/// 0 left-to-right, 1 right-to-left, 2 number, 3 neutral.
fn bidi_class(cp: u128) -> u8 {
    match cp {
        0x30..=0x39 | 0x0660..=0x0669 | 0x06F0..=0x06F9 => 2,
        0x0590..=0x08FF | 0xFB1D..=0xFDFF | 0xFE70..=0xFEFF | 0x10800..=0x10FFF | 0x1E800..=0x1EFFF => 1,
        0..=0x2F | 0x3A..=0x40 | 0x5B..=0x60 | 0x7B..=0xBF | 0xD7 | 0xF7 | 0x2000..=0x206F | 0x2190..=0x23FF | 0x2500..=0x27BF | 0x3000..=0x3003 | 0x3008..=0x3011 => 3,
        _ => 0,
    }
}

fn mirror(cp: u128) -> u128 {
    match cp {
        0x28 => 0x29, 0x29 => 0x28, 0x3C => 0x3E, 0x3E => 0x3C, 0x5B => 0x5D, 0x5D => 0x5B, 0x7B => 0x7D, 0x7D => 0x7B,
        0xAB => 0xBB, 0xBB => 0xAB, 0x2039 => 0x203A, 0x203A => 0x2039,
        _ => cp,
    }
}

fn visual_line(line: &[u128]) -> Vec<u128> {
    let shaped = shape_arabic(line);
    // clusters: a base with its following combining marks
    let mut clusters: Vec<(usize, usize)> = Vec::new();
    for (i, &c) in shaped.iter().enumerate() {
        match clusters.last_mut() {
            Some(last) if is_mark(c) => last.1 = i + 1,
            _ => clusters.push((i, i + 1)),
        }
    }
    let class: Vec<u8> = clusters.iter().map(|&(a, _)| if is_mark(shaped[a]) { 3 } else { bidi_class(shaped[a]) }).collect();
    let rtl = class.iter().find(|&&c| c <= 1).is_some_and(|&c| c == 1);
    if !rtl && !class.contains(&1) {
        return shaped;
    }
    let para: u8 = u8::from(rtl);
    // strong direction for neutrals: numbers count as right-to-left
    let strong = |c: u8| match c {
        0 => Some(0u8),
        1 | 2 => Some(1u8),
        _ => None,
    };
    let n = clusters.len();
    let mut dir = vec![0u8; n];
    let mut i = 0;
    while i < n {
        if class[i] != 3 {
            dir[i] = class[i];
            i += 1;
            continue;
        }
        let start = i;
        while i < n && class[i] == 3 {
            i += 1;
        }
        let before = (0..start).rev().find_map(|k| strong(class[k])).unwrap_or(para);
        let after = (i..n).find_map(|k| strong(class[k])).unwrap_or(para);
        let d = if before == after { before } else { para };
        for x in &mut dir[start..i] {
            *x = d;
        }
    }
    let mut level = vec![0u8; n];
    let mut last_strong = para;
    for k in 0..n {
        level[k] = match (para, dir[k]) {
            (0, 0) => 0,
            (0, 1) => 1,
            (0, _) => if last_strong == 1 { 2 } else { 0 },
            (_, 1) => 1,
            _ => 2,
        };
        if class[k] <= 1 {
            last_strong = class[k];
        }
    }
    let mut order: Vec<usize> = (0..n).collect();
    let top = level.iter().copied().max().unwrap_or(0);
    for k in (1..=top).rev() {
        let mut a = 0;
        while a < n {
            if level[order[a]] >= k {
                let mut b = a;
                while b < n && level[order[b]] >= k {
                    b += 1;
                }
                order[a..b].reverse();
                a = b;
            } else {
                a += 1;
            }
        }
    }
    let mut out = Vec::with_capacity(shaped.len());
    for idx in order {
        let (a, b) = clusters[idx];
        let odd = level[idx] % 2 == 1;
        out.push(if odd { mirror(shaped[a]) } else { shaped[a] });
        out.extend_from_slice(&shaped[a + 1..b]);
    }
    out
}

/// Code points in display order with Arabic contextual forms, line by line.
pub fn visual(cps: &[u128]) -> Vec<u128> {
    let mut out = Vec::with_capacity(cps.len());
    for (k, line) in cps.split(|&c| c == 10).enumerate() {
        if k > 0 {
            out.push(10);
        }
        out.extend(visual_line(line));
    }
    out
}

/// Width of `text` at cell height `size` (scene units): longest line × size·6/8,
/// saturated at the exact Nat limit.
pub fn text_width(text: &str, size: u128) -> u128 {
    let (mut best, mut cur) = (0u128, 0u128);
    for c in text.chars() {
        cur = if c == '\n' { 0 } else { cur + 1 };
        best = best.max(cur);
    }
    let factor = best * 3;
    (size / 4).checked_mul(factor).and_then(|q| q.checked_add((size % 4) * factor / 4)).unwrap_or(u128::MAX)
}

fn font_cmd(r: &mut Reader) -> Option<(u128, u128, u128, u128, Vec<u128>)> {
    let (x, y, size, max_w, nf) = (r.next()?, r.next()?, r.next()?, r.next()?, r.next()?);
    if nf > 64 {
        return None;
    }
    let mut ids = Vec::with_capacity(nf as usize);
    for _ in 0..nf {
        ids.push(r.next()?);
    }
    Some((x, y, size, max_w, ids))
}

fn code_points(r: &mut Reader) -> Option<Vec<u128>> {
    let n = r.next()?;
    if n > 1 << 20 {
        return None;
    }
    let mut cps = Vec::with_capacity(n as usize);
    for _ in 0..n {
        cps.push(r.next()?);
    }
    Some(cps)
}

fn decode(scene: &[u128], s: &Scale, fonts: &FontTable) -> Option<Vec<Op>> {
    let mut r = Reader { v: scene, i: 0 };
    let mut ops = Vec::new();
    let mut depth = 0usize;
    while let Some(op) = r.next() {
        ops.push(match op {
            1 => {
                let rule = r.next()?;
                let p = paint(&mut r, s)?;
                let polys = path(&mut r, s)?.into_iter().map(|(pts, _)| pts).collect();
                Op::Shape { polys, evenodd: rule == 1, paint: p }
            }
            2 => {
                let half = (s.c(r.next()?) / 2).max(1);
                let p = paint(&mut r, s)?;
                Op::Shape { polys: stroke(&path(&mut r, s)?, half), evenodd: false, paint: p }
            }
            3 => {
                let (x, y, w, h, rad) = (r.next()?, r.next()?, r.next()?, r.next()?, r.next()?);
                let p = paint(&mut r, s)?;
                Op::Shape { polys: vec![rrect(s.c(x), s.c(y), s.c(x.saturating_add(w)), s.c(y.saturating_add(h)), s.c(rad))], evenodd: false, paint: p }
            }
            4 => {
                let (x, y, w, h) = (r.next()?, r.next()?, r.next()?, r.next()?);
                let (iw, ih) = (r.next()?, r.next()?);
                let n = iw.checked_mul(ih).filter(|n| *n <= (crate::graph::VEC_MAX as u128))? as usize;
                let mut px = Vec::with_capacity(n);
                for _ in 0..n {
                    px.push(r.next()? & 0xFFFF_FFFF);
                }
                Op::Image { x0: s.c(x), y0: s.c(y), x1: s.c(x.saturating_add(w)), y1: s.c(y.saturating_add(h)), iw: iw as usize, ih: ih as usize, px }
            }
            5 => {
                let (x, y, w, h) = (r.next()?, r.next()?, r.next()?, r.next()?);
                depth += 1;
                if depth >= 256 {
                    return None;
                }
                Op::Clip(s.c(x), s.c(y), s.c(x.saturating_add(w)), s.c(y.saturating_add(h)))
            }
            6 => {
                depth = depth.saturating_sub(1);
                Op::Unclip
            }
            7 => {
                let (x, y, size, weight) = (r.next()?, r.next()?, r.next()?, r.next()?);
                let p = paint(&mut r, s)?;
                let n = r.next()?;
                if n > 1 << 20 {
                    return None;
                }
                let mut cps = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    cps.push(r.next()?);
                }
                let half = (s.c(weight) / 2).max(1);
                let polys = text_segments(x, y, size, &cps).into_iter().map(|(a, b)| capsule((s.c(a.0), s.c(a.1)), (s.c(b.0), s.c(b.1)), half)).collect();
                Op::Shape { polys, evenodd: false, paint: p }
            }
            8 => {
                let (cx, cy, rx, ry) = (r.next()?, r.next()?, r.next()?, r.next()?);
                let p = paint(&mut r, s)?;
                Op::Shape { polys: vec![ellipse(s.c(cx), s.c(cy), s.c(rx), s.c(ry))], evenodd: false, paint: p }
            }
            9 => {
                let (x, y, size, max_w, ids) = font_cmd(&mut r)?;
                let p = paint(&mut r, s)?;
                let cps = code_points(&mut r)?;
                let ft = layout_font_text(fonts, &ids, x, y, size, max_w, &cps);
                Op::Shape { polys: font_text_polys(&ft, s), evenodd: false, paint: p }
            }
            _ => return None,
        });
    }
    Some(ops)
}

fn lerp(c0: u128, c1: u128, t: i64) -> u128 {
    let t = t.clamp(0, 256) as u128;
    let mut out = 0;
    for shift in [24u32, 16, 8, 0] {
        let (a, b) = ((c0 >> shift) & 0xFF, (c1 >> shift) & 0xFF);
        out |= ((a * (256 - t) + b * t + 128) / 256) << shift;
    }
    out
}

fn color_at(p: &Paint, x: i64, y: i64) -> u128 {
    match *p {
        Paint::Solid(c) => c,
        Paint::Linear(x0, y0, c0, x1, y1, c1) => {
            let (dx, dy) = (i128::from(x1 - x0), i128::from(y1 - y0));
            let dd = dx * dx + dy * dy;
            if dd == 0 {
                return c0;
            }
            let t = ((i128::from(x - x0) * dx + i128::from(y - y0) * dy) * 256 / dd).clamp(0, 256) as i64;
            lerp(c0, c1, t)
        }
        Paint::Radial(cx, cy, r, c0, c1) => {
            if r <= 0 {
                return c1;
            }
            let (dx, dy) = (i128::from(x - cx), i128::from(y - cy));
            let d = isqrt((dx * dx + dy * dy) as u128) as i128;
            lerp(c0, c1, (d * 256 / i128::from(r)).clamp(0, 256) as i64)
        }
    }
}

#[derive(Clone, Copy)]
struct ClipBox(i64, i64, i64, i64);

fn shape(img: &mut [u128], w: usize, polys: &[Poly], evenodd: bool, p: &Paint, clip: ClipBox) {
    let mut edges: Vec<(i64, i64, i64, i64, i32)> = Vec::new();
    for poly in polys {
        for i in 0..poly.len() {
            let (a, b) = (poly[i], poly[(i + 1) % poly.len()]);
            if a.1 == b.1 {
                continue;
            }
            edges.push(if a.1 < b.1 { (a.0, a.1, b.0, b.1, 1) } else { (b.0, b.1, a.0, a.1, -1) });
        }
    }
    if edges.is_empty() {
        return;
    }
    edges.sort_by(|a, b| a.1.cmp(&b.1).then(a.0.cmp(&b.0)).then(a.3.cmp(&b.3)).then(a.2.cmp(&b.2)).then(a.4.cmp(&b.4)));
    let ymin = edges[0].1;
    let ymax = edges.iter().map(|e| e.3).max().unwrap_or(0);
    let r0 = (ymin.div_euclid(256)).max(clip.1);
    let r1 = ((ymax + 255).div_euclid(256)).min(clip.3);
    let (cx0, cx1) = (clip.0, clip.2);
    if cx0 >= cx1 || r0 >= r1 {
        return;
    }
    let mut acc = vec![0i64; (cx1 - cx0) as usize];
    let mut next = 0usize;
    let mut active: Vec<usize> = Vec::new();
    let mut xs: Vec<(i64, i32)> = Vec::new();
    let mut row = r0;
    while row < r1 {
        acc.iter_mut().for_each(|a| *a = 0);
        let mut any = false;
        for k in 0..16 {
            let ys = row * 256 + k * 16 + 8;
            while next < edges.len() && edges[next].1 <= ys {
                active.push(next);
                next += 1;
            }
            active.retain(|&e| edges[e].3 > ys);
            xs.clear();
            for &e in &active {
                let (xa, ya, xb, yb, d) = edges[e];
                if ya <= ys {
                    xs.push(((i128::from(xa) + i128::from(xb - xa) * i128::from(ys - ya) / i128::from(yb - ya)) as i64, d));
                }
            }
            xs.sort_unstable();
            let mut wind = 0i32;
            let mut start = 0i64;
            for &(x, d) in &xs {
                let inside = |w: i32| if evenodd { w & 1 != 0 } else { w != 0 };
                let was = inside(wind);
                wind += d;
                let now = inside(wind);
                if !was && now {
                    start = x;
                } else if was && !now {
                    let (a, b) = (start.max(cx0 * 256), x.min(cx1 * 256));
                    if a < b {
                        any = true;
                        let (pa, pb) = (a.div_euclid(256), (b - 1).div_euclid(256));
                        if pa == pb {
                            acc[(pa - cx0) as usize] += b - a;
                        } else {
                            acc[(pa - cx0) as usize] += (pa + 1) * 256 - a;
                            for px in pa + 1..pb {
                                acc[(px - cx0) as usize] += 256;
                            }
                            acc[(pb - cx0) as usize] += b - pb * 256;
                        }
                    }
                }
            }
        }
        if any {
            for (i, &a) in acc.iter().enumerate() {
                if a <= 0 {
                    continue;
                }
                let cov = ((a * 255 + 2048) / 4096).min(255) as u128;
                let x = cx0 + i as i64;
                let c = color_at(p, x * 256 + 128, row * 256 + 128);
                let alpha = ((c & 0xFF) * cov + 127) / 255;
                let at = 2 + row as usize * w + x as usize;
                img[at] = gfx::blend(img[at], (c & !0xFF) | alpha);
            }
        }
        row += 1;
    }
}

fn image(img: &mut [u128], w: usize, o: &Op, clip: ClipBox) {
    let Op::Image { x0, y0, x1, y1, iw, ih, px } = o else { return };
    if *iw == 0 || *ih == 0 || x1 <= x0 || y1 <= y0 {
        return;
    }
    let (ax, bx) = (x0.div_euclid(256).max(clip.0), ((x1 + 255).div_euclid(256)).min(clip.2));
    let (ay, by) = (y0.div_euclid(256).max(clip.1), ((y1 + 255).div_euclid(256)).min(clip.3));
    for y in ay..by {
        let cy = y * 256 + 128;
        if cy < *y0 || cy >= *y1 {
            continue;
        }
        let sy = (i128::from(cy - y0) * *ih as i128 / i128::from(y1 - y0)) as usize;
        for x in ax..bx {
            let cx = x * 256 + 128;
            if cx < *x0 || cx >= *x1 {
                continue;
            }
            let sx = (i128::from(cx - x0) * *iw as i128 / i128::from(x1 - x0)) as usize;
            let at = 2 + y as usize * w + x as usize;
            img[at] = gfx::blend(img[at], px[sy * iw + sx]);
        }
    }
}

fn draw(img: &mut [u128], ops: &[Op], region: ClipBox) {
    let w = img[0] as usize;
    let mut stack = vec![region];
    for o in ops {
        let clip = *stack.last().expect("clip stack");
        match o {
            Op::Shape { polys, evenodd, paint } => shape(img, w, polys, *evenodd, paint, clip),
            Op::Image { .. } => image(img, w, o, clip),
            Op::Clip(x0, y0, x1, y1) => {
                let c = ClipBox(clip.0.max(x0.div_euclid(256)), clip.1.max(y0.div_euclid(256)), clip.2.min((x1 + 255).div_euclid(256)), clip.3.min((y1 + 255).div_euclid(256)));
                stack.push(ClipBox(c.0, c.1, c.2.max(c.0), c.3.max(c.1)));
            }
            Op::Unclip => {
                if stack.len() > 1 {
                    stack.pop();
                }
            }
        }
    }
}

fn scale(num: u128, den: u128) -> Option<Scale> {
    (num > 0 && den > 0 && num <= 1 << 16 && den <= 1 << 16).then(|| Scale { num: num as i128, den: den as i128 })
}

/// A new transparent `w × h` image with the scene drawn at scale `num/den`.
pub fn render(scene: &[u128], w: u128, h: u128, num: u128, den: u128) -> Option<Vec<u128>> {
    render_fonts(scene, w, h, num, den, &FontTable::default())
}

/// `render` with the loaded TrueType fonts for text commands (op 9).
pub fn render_fonts(scene: &[u128], w: u128, h: u128, num: u128, den: u128, fonts: &FontTable) -> Option<Vec<u128>> {
    let s = scale(num, den)?;
    let ops = decode(scene, &s, fonts)?;
    let mut img = gfx::new(w, h, 0)?;
    if w != 0 && h != 0 { draw(&mut img, &ops, ClipBox(0, 0, w as i64, h as i64)); }
    Some(img)
}

// ---- v0.131: the scene as a GPU draw list (crate::window, `win_present_scene`) ----

/// Paint in pixel coordinates; colors 0xRRGGBBAA.
#[derive(Clone, Debug)]
pub enum GpuPaint {
    Solid(u32),
    Linear { x0: f64, y0: f64, x1: f64, y1: f64, c0: u32, c1: u32 },
    Radial { cx: f64, cy: f64, r: f64, c0: u32, c1: u32 },
}

/// One draw: filled polygons (pixels) or a nearest-sampled image, with its clip rectangle.
#[derive(Clone, Debug)]
pub enum GpuItem {
    Shape { polys: Vec<Vec<(f64, f64)>>, convex: bool, evenodd: bool, paint: GpuPaint, clip: (i64, i64, i64, i64) },
    Image { x0: f64, y0: f64, x1: f64, y1: f64, iw: usize, ih: usize, px: Vec<u32>, clip: (i64, i64, i64, i64) },
}

fn convex(p: &Poly) -> bool {
    let n = p.len();
    if n < 3 {
        return false;
    }
    let mut sign = 0;
    for i in 0..n {
        let (a, b, c) = (p[i], p[(i + 1) % n], p[(i + 2) % n]);
        let cr = i128::from(b.0 - a.0) * i128::from(c.1 - b.1) - i128::from(b.1 - a.1) * i128::from(c.0 - b.0);
        let s = cr.signum();
        if s != 0 && sign != 0 && s != sign {
            return false;
        }
        if s != 0 {
            sign = s;
        }
    }
    // a single turn: the edge directions change x and y sign at most twice each (not a star)
    let flips = |f: &dyn Fn(usize) -> i64| {
        let signs: Vec<i64> = (0..n).map(|i| f(i).signum()).filter(|&s| s != 0).collect();
        (0..signs.len()).filter(|&i| signs[i] != signs[(i + 1) % signs.len()]).count()
    };
    sign != 0 && flips(&|i| p[(i + 1) % n].0 - p[i].0) <= 2 && flips(&|i| p[(i + 1) % n].1 - p[i].1) <= 2
}

/// The draw list of a scene at scale `num/den` in a `w × h` viewport (the clips of `render`).
pub fn gpu_items(scene: &[u128], w: u128, h: u128, num: u128, den: u128, fonts: &FontTable) -> Option<Vec<GpuItem>> {
    let s = scale(num, den)?;
    let ops = decode(scene, &s, fonts)?;
    let px = |v: i64| v as f64 / 256.0;
    let mut stack = vec![ClipBox(0, 0, w.min(1 << 20) as i64, h.min(1 << 20) as i64)];
    let mut out = Vec::new();
    for o in ops {
        let clip = *stack.last().expect("clip stack");
        let cl = (clip.0, clip.1, clip.2, clip.3);
        match o {
            Op::Shape { polys, evenodd, paint } => {
                let paint = match paint {
                    Paint::Solid(c) => GpuPaint::Solid(c as u32),
                    Paint::Linear(x0, y0, c0, x1, y1, c1) => GpuPaint::Linear { x0: px(x0), y0: px(y0), x1: px(x1), y1: px(y1), c0: c0 as u32, c1: c1 as u32 },
                    Paint::Radial(cx, cy, r, c0, c1) => GpuPaint::Radial { cx: px(cx), cy: px(cy), r: px(r), c0: c0 as u32, c1: c1 as u32 },
                };
                let convex = polys.len() == 1 && convex(&polys[0]);
                let polys = polys.iter().map(|p| p.iter().map(|&(x, y)| (px(x), px(y))).collect()).collect();
                out.push(GpuItem::Shape { polys, convex, evenodd, paint, clip: cl });
            }
            Op::Image { x0, y0, x1, y1, iw, ih, px: pixels } => {
                out.push(GpuItem::Image { x0: px(x0), y0: px(y0), x1: px(x1), y1: px(y1), iw, ih, px: pixels.iter().map(|&c| (c & 0xFFFF_FFFF) as u32).collect(), clip: cl });
            }
            Op::Clip(x0, y0, x1, y1) => {
                let c = ClipBox(clip.0.max(x0.div_euclid(256)), clip.1.max(y0.div_euclid(256)), clip.2.min((x1 + 255).div_euclid(256)), clip.3.min((y1 + 255).div_euclid(256)));
                stack.push(ClipBox(c.0, c.1, c.2.max(c.0), c.3.max(c.1)));
            }
            Op::Unclip => {
                if stack.len() > 1 {
                    stack.pop();
                }
            }
        }
    }
    Some(out)
}

/// `img` with the rectangle `x, y, rw, rh` cleared and drawn again: the same
/// pixels a full `render` gives there (incremental redraw of a dirty region).
#[allow(clippy::too_many_arguments)]
pub fn render_region(img: Vec<u128>, scene: &[u128], num: u128, den: u128, x: u128, y: u128, rw: u128, rh: u128) -> Option<Vec<u128>> {
    render_region_fonts(img, scene, num, den, x, y, rw, rh, &FontTable::default())
}

#[allow(clippy::too_many_arguments)]
pub fn render_region_fonts(mut img: Vec<u128>, scene: &[u128], num: u128, den: u128, x: u128, y: u128, rw: u128, rh: u128, fonts: &FontTable) -> Option<Vec<u128>> {
    let s = scale(num, den)?;
    let ops = decode(scene, &s, fonts)?;
    let (w, h) = (*img.first()?, *img.get(1)?);
    if w.checked_mul(h)?.checked_add(2)? != img.len() as u128 {
        return None;
    }
    if w == 0 || h == 0 { return Some(img); }
    let (x0, y0) = (x.min(w), y.min(h));
    let (x1, y1) = (x.saturating_add(rw).min(w), y.saturating_add(rh).min(h));
    for yy in y0..y1 {
        for xx in x0..x1 {
            img[2 + (yy * w + xx) as usize] = 0;
        }
    }
    draw(&mut img, &ops, ClipBox(x0 as i64, y0 as i64, x1 as i64, y1 as i64));
    Some(img)
}

/// Pixel bounds `[x0, y0, x1, y1]` (exclusive) of everything the scene draws; `[0, 0, 0, 0]` when empty.
pub fn bounds(scene: &[u128], num: u128, den: u128) -> Option<Vec<u128>> {
    bounds_fonts(scene, num, den, &FontTable::default())
}

pub fn bounds_fonts(scene: &[u128], num: u128, den: u128, fonts: &FontTable) -> Option<Vec<u128>> {
    let s = scale(num, den)?;
    let ops = decode(scene, &s, fonts)?;
    let mut b: Option<(i64, i64, i64, i64)> = None;
    let mut add = |x0: i64, y0: i64, x1: i64, y1: i64| {
        b = Some(match b {
            None => (x0, y0, x1, y1),
            Some(o) => (o.0.min(x0), o.1.min(y0), o.2.max(x1), o.3.max(y1)),
        });
    };
    for o in &ops {
        match o {
            Op::Shape { polys, .. } => {
                for p in polys {
                    for &(x, y) in p {
                        add(x, y, x, y);
                    }
                }
            }
            Op::Image { x0, y0, x1, y1, .. } => add(*x0, *y0, *x1, *y1),
            _ => {}
        }
    }
    Some(match b {
        None => vec![0, 0, 0, 0],
        Some((x0, y0, x1, y1)) => vec![x0.div_euclid(256).max(0) as u128, y0.div_euclid(256).max(0) as u128, ((x1 + 255).div_euclid(256) + 1).max(0) as u128, ((y1 + 255).div_euclid(256) + 1).max(0) as u128],
    })
}

// ---- SVG ----

fn num(v: u128) -> String {
    let (i, f) = (v / 16, v % 16);
    if f == 0 {
        i.to_string()
    } else {
        format!("{i}.{}", format!("{:04}", f * 625).trim_end_matches('0'))
    }
}

fn hex(c: u128) -> String {
    format!("#{:06x}", c >> 8)
}

fn opacity(c: u128) -> String {
    let a = c & 0xFF;
    if a == 255 {
        "1".into()
    } else {
        let t = a * 1000 / 255;
        format!("0.{t:03}")
    }
}

struct Svg {
    out: String,
    ids: u128,
}

impl Svg {
    /// `fill="..."`-style attribute value for a paint, defining gradients first.
    fn paint(&mut self, r: &mut Reader) -> Option<(String, String)> {
        Some(match r.next()? {
            0 => {
                let c = color(r)?;
                (hex(c), opacity(c))
            }
            1 => {
                let (x0, y0, c0, x1, y1, c1) = (r.next()?, r.next()?, color(r)?, r.next()?, r.next()?, color(r)?);
                self.ids += 1;
                self.out.push_str(&format!(
                    "<defs><linearGradient id=\"g{}\" gradientUnits=\"userSpaceOnUse\" x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\"><stop offset=\"0\" stop-color=\"{}\" stop-opacity=\"{}\"/><stop offset=\"1\" stop-color=\"{}\" stop-opacity=\"{}\"/></linearGradient></defs>",
                    self.ids, num(x0), num(y0), num(x1), num(y1), hex(c0), opacity(c0), hex(c1), opacity(c1)
                ));
                (format!("url(#g{})", self.ids), "1".into())
            }
            2 => {
                let (cx, cy, rad, c0, c1) = (r.next()?, r.next()?, r.next()?, color(r)?, color(r)?);
                self.ids += 1;
                self.out.push_str(&format!(
                    "<defs><radialGradient id=\"g{}\" gradientUnits=\"userSpaceOnUse\" cx=\"{}\" cy=\"{}\" r=\"{}\"><stop offset=\"0\" stop-color=\"{}\" stop-opacity=\"{}\"/><stop offset=\"1\" stop-color=\"{}\" stop-opacity=\"{}\"/></radialGradient></defs>",
                    self.ids, num(cx), num(cy), num(rad), hex(c0), opacity(c0), hex(c1), opacity(c1)
                ));
                (format!("url(#g{})", self.ids), "1".into())
            }
            _ => return None,
        })
    }

    fn path(r: &mut Reader) -> Option<String> {
        let n = r.next()?;
        if n > 1 << 20 {
            return None;
        }
        let mut d = String::new();
        for _ in 0..n {
            let (verb, k) = match r.next()? {
                0 => ("M", 2),
                1 => ("L", 2),
                2 => ("Q", 4),
                3 => ("C", 6),
                4 => ("Z", 0),
                _ => return None,
            };
            if !d.is_empty() {
                d.push(' ');
            }
            d.push_str(verb);
            for _ in 0..k {
                d.push(' ');
                d.push_str(&num(r.next()?));
            }
        }
        Some(d)
    }
}

fn base64(data: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for c in data.chunks(3) {
        let b = [c[0], *c.get(1).unwrap_or(&0), *c.get(2).unwrap_or(&0)];
        let n = (u32::from(b[0]) << 16) | (u32::from(b[1]) << 8) | u32::from(b[2]);
        for i in 0..4 {
            if i <= c.len() {
                out.push(T[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

/// SVG document of the scene (`w × h` px viewport).
fn snum(v: i128) -> String {
    num(v.max(0) as u128)
}

pub fn svg(scene: &[u128], w: u128, h: u128) -> Option<String> {
    svg_fonts(scene, w, h, &FontTable::default())
}

pub fn svg_fonts(scene: &[u128], w: u128, h: u128, fonts: &FontTable) -> Option<String> {
    let mut s = Svg { out: format!("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{w}\" height=\"{h}\" viewBox=\"0 0 {w} {h}\">"), ids: 0 };
    let mut r = Reader { v: scene, i: 0 };
    let mut open = 0usize;
    while let Some(op) = r.next() {
        match op {
            1 => {
                let rule = r.next()?;
                let (f, o) = s.paint(&mut r)?;
                let d = Svg::path(&mut r)?;
                s.out.push_str(&format!("<path d=\"{d}\" fill=\"{f}\" fill-opacity=\"{o}\" fill-rule=\"{}\"/>", if rule == 1 { "evenodd" } else { "nonzero" }));
            }
            2 => {
                let width = r.next()?;
                let (f, o) = s.paint(&mut r)?;
                let d = Svg::path(&mut r)?;
                s.out.push_str(&format!("<path d=\"{d}\" fill=\"none\" stroke=\"{f}\" stroke-opacity=\"{o}\" stroke-width=\"{}\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>", num(width)));
            }
            3 => {
                let (x, y, w, h, rad) = (r.next()?, r.next()?, r.next()?, r.next()?, r.next()?);
                let (f, o) = s.paint(&mut r)?;
                let rad = rad.min(w / 2).min(h / 2);
                s.out.push_str(&format!("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" rx=\"{}\" fill=\"{f}\" fill-opacity=\"{o}\"/>", num(x), num(y), num(w), num(h), num(rad)));
            }
            4 => {
                let (x, y, w, h, iw, ih) = (r.next()?, r.next()?, r.next()?, r.next()?, r.next()?, r.next()?);
                let n = iw.checked_mul(ih).filter(|n| *n <= (crate::graph::VEC_MAX as u128))? as usize;
                let mut img = vec![iw, ih];
                for _ in 0..n {
                    img.push(r.next()? & 0xFFFF_FFFF);
                }
                if let Some(png) = gfx::png(&img) {
                    s.out.push_str(&format!("<image x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" preserveAspectRatio=\"none\" style=\"image-rendering:pixelated\" href=\"data:image/png;base64,{}\"/>", num(x), num(y), num(w), num(h), base64(&png)));
                }
            }
            5 => {
                let (x, y, w, h) = (r.next()?, r.next()?, r.next()?, r.next()?);
                s.ids += 1;
                s.out.push_str(&format!("<clipPath id=\"c{}\"><rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\"/></clipPath><g clip-path=\"url(#c{})\">", s.ids, num(x), num(y), num(w), num(h), s.ids));
                open += 1;
            }
            6 => {
                if open > 0 {
                    s.out.push_str("</g>");
                    open -= 1;
                }
            }
            7 => {
                let (x, y, size, weight) = (r.next()?, r.next()?, r.next()?, r.next()?);
                let (f, o) = s.paint(&mut r)?;
                let n = r.next()?;
                if n > 1 << 20 {
                    return None;
                }
                let mut cps = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    cps.push(r.next()?);
                }
                let d: Vec<String> = text_segments(x, y, size, &cps).iter().map(|(a, b)| format!("M {} {} L {} {}", num(a.0), num(a.1), num(b.0), num(b.1))).collect();
                if !d.is_empty() {
                    s.out.push_str(&format!("<path d=\"{}\" fill=\"none\" stroke=\"{f}\" stroke-opacity=\"{o}\" stroke-width=\"{}\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>", d.join(" "), num(weight)));
                }
            }
            9 => {
                let (x, y, size0, max_w, ids) = font_cmd(&mut r)?;
                let (f, o) = s.paint(&mut r)?;
                let cps = code_points(&mut r)?;
                let ft = layout_font_text(fonts, &ids, x, y, size0, max_w, &cps);
                let size = ft.size;
                let mut fill = String::new();
                let mut strokes = String::new();
                for piece in &ft.pieces {
                    match *piece {
                        Piece::Outline(font, g, pen, base) => {
                            for contour in font.outline(g) {
                                let pts: Vec<(i128, i128, bool)> = contour.iter().map(|p| {
                                    let gx = i128::from(p.x) * ft.em / i128::from(font.upem);
                                    let gy = i128::from(p.y) * ft.em / i128::from(font.upem);
                                    (ft.x + fit_width(pen + gx, ft.fit), base - gy, p.on)
                                }).collect();
                                let Some(k) = pts.iter().position(|p| p.2).or(Some(0)) else { continue };
                                let n = pts.len();
                                if n == 0 {
                                    continue;
                                }
                                let at = |i: usize| pts[(k + i) % n];
                                let first = at(0);
                                let start = if first.2 { (first.0, first.1) } else { ((pts[n - 1].0 + first.0) / 2, (pts[n - 1].1 + first.1) / 2) };
                                fill.push_str(&format!("M {} {} ", snum(start.0), snum(start.1)));
                                let mut pending: Option<(i128, i128)> = None;
                                let begin = usize::from(first.2);
                                for i in begin..=n {
                                    let p = if i == n { (start.0, start.1, true) } else { at(i) };
                                    if p.2 {
                                        match pending.take() {
                                            Some(c) => fill.push_str(&format!("Q {} {} {} {} ", snum(c.0), snum(c.1), snum(p.0), snum(p.1))),
                                            None => fill.push_str(&format!("L {} {} ", snum(p.0), snum(p.1))),
                                        }
                                    } else {
                                        if let Some(c) = pending {
                                            fill.push_str(&format!("Q {} {} {} {} ", snum(c.0), snum(c.1), snum((c.0 + p.0) / 2), snum((c.1 + p.1) / 2)));
                                        }
                                        pending = Some((p.0, p.1));
                                    }
                                }
                                fill.push_str("Z ");
                            }
                        }
                        Piece::Stroke(cp, pen, y_line) => {
                            let ox = (ft.x + fit_width(pen, ft.fit)).max(0) as u128;
                            for (a, b) in text_segments(ox, y_line.max(0) as u128, size, &[cp]) {
                                strokes.push_str(&format!("M {} {} L {} {} ", num(a.0), num(a.1), num(b.0), num(b.1)));
                            }
                        }
                    }
                }
                if !fill.is_empty() {
                    s.out.push_str(&format!("<path d=\"{}\" fill=\"{f}\" fill-opacity=\"{o}\"/>", fill.trim_end()));
                }
                if !strokes.is_empty() {
                    s.out.push_str(&format!("<path d=\"{}\" fill=\"none\" stroke=\"{f}\" stroke-opacity=\"{o}\" stroke-width=\"{}\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>", strokes.trim_end(), num(size / 10)));
                }
            }
            8 => {
                let (cx, cy, rx, ry) = (r.next()?, r.next()?, r.next()?, r.next()?);
                let (f, o) = s.paint(&mut r)?;
                s.out.push_str(&format!("<ellipse cx=\"{}\" cy=\"{}\" rx=\"{}\" ry=\"{}\" fill=\"{f}\" fill-opacity=\"{o}\"/>", num(cx), num(cy), num(rx), num(ry)));
            }
            _ => return None,
        }
    }
    for _ in 0..open {
        s.out.push_str("</g>");
    }
    s.out.push_str("</svg>");
    Some(s.out)
}
