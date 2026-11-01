//! v0.129: TrueType fonts for vector text (docs/GUI.md). Own parser of the
//! `head`, `hhea`, `maxp`, `hmtx`, `cmap` (formats 4 and 12), `loca` and `glyf`
//! tables (simple and composite glyphs; TTC: first face). Outlines are integer
//! font units; the native runtime (`dvf_*`) parses the same bytes the same way.
//! No shaping (ligatures, contextual forms, bidi): one glyph per code point.

pub struct Font {
    data: Vec<u8>,
    pub upem: i64,
    pub ascender: i64,
    pub descender: i64,
    pub num_glyphs: u32,
    long_loca: bool,
    loca: usize,
    glyf: usize,
    glyf_end: usize,
    hmtx: usize,
    n_hmetrics: u32,
    cmap: usize,
    cmap_end: usize,
    cmap_format: u16,
}

/// A contour point in font units; `on` = on-curve.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Pt {
    pub x: i64,
    pub y: i64,
    pub on: bool,
}

fn u16_at(d: &[u8], o: usize) -> Option<u16> {
    Some(u16::from_be_bytes([*d.get(o)?, *d.get(o + 1)?]))
}
fn i16_at(d: &[u8], o: usize) -> Option<i16> {
    u16_at(d, o).map(|v| v as i16)
}
fn u32_at(d: &[u8], o: usize) -> Option<u32> {
    Some(u32::from_be_bytes([*d.get(o)?, *d.get(o + 1)?, *d.get(o + 2)?, *d.get(o + 3)?]))
}

fn table(d: &[u8], base: usize, tag: &[u8; 4]) -> Option<(usize, usize)> {
    let n = u16_at(d, base.checked_add(4)?)? as usize;
    let records = base.checked_add(12)?;
    d.get(records..records.checked_add(16 * n)?)?;
    for i in 0..n {
        let rec = records + 16 * i;
        if d.get(rec..rec + 4)? == tag {
            let off = u32_at(d, rec + 8)? as usize;
            let len = u32_at(d, rec + 12)? as usize;
            d.get(off..off.checked_add(len)?)?;
            return Some((off, len));
        }
    }
    None
}

impl Font {
    pub fn parse(data: Vec<u8>) -> Result<Font, String> {
        let bad = || "unsupported or damaged font".to_string();
        let tag = data.get(0..4).ok_or_else(bad)?;
        let base = if tag == b"ttcf" { u32_at(&data, 12).ok_or_else(bad)? as usize } else { 0 };
        let version = data.get(base..base + 4).ok_or_else(bad)?;
        if version == b"OTTO" {
            return Err("CFF (PostScript) outlines are not supported; use a TrueType font".into());
        }
        if version != [0, 1, 0, 0] && version != b"true" {
            return Err(bad());
        }
        let (head, head_len) = table(&data, base, b"head").ok_or_else(bad)?;
        let (hhea, hhea_len) = table(&data, base, b"hhea").ok_or_else(bad)?;
        let (maxp, maxp_len) = table(&data, base, b"maxp").ok_or_else(bad)?;
        let (hmtx, hmtx_len) = table(&data, base, b"hmtx").ok_or_else(bad)?;
        let (cmap, cmap_len) = table(&data, base, b"cmap").ok_or_else(bad)?;
        let (loca, loca_len) = table(&data, base, b"loca").ok_or_else(bad)?;
        let (glyf, glyf_len) = table(&data, base, b"glyf").ok_or_else(bad)?;
        if head_len < 54 || hhea_len < 36 || maxp_len < 6 || cmap_len < 4 { return Err(bad()); }
        let upem = i64::from(u16_at(&data, head + 18).ok_or_else(bad)?);
        if upem == 0 {
            return Err(bad());
        }
        let loca_format = i16_at(&data, head + 50).ok_or_else(bad)?;
        if !(0..=1).contains(&loca_format) { return Err(bad()); }
        let long_loca = loca_format == 1;
        let num_glyphs = u32::from(u16_at(&data, maxp + 4).ok_or_else(bad)?);
        let ascender = i64::from(i16_at(&data, hhea + 4).ok_or_else(bad)?);
        let descender = i64::from(i16_at(&data, hhea + 6).ok_or_else(bad)?);
        let n_hmetrics = u32::from(u16_at(&data, hhea + 34).ok_or_else(bad)?);
        if num_glyphs == 0 || n_hmetrics == 0 || n_hmetrics > num_glyphs
            || hmtx_len < 4 * n_hmetrics as usize + 2 * (num_glyphs - n_hmetrics) as usize
            || loca_len < (num_glyphs as usize + 1) * if long_loca { 4 } else { 2 } {
            return Err(bad());
        }
        let mut previous = 0;
        for i in 0..=num_glyphs as usize {
            let off = if long_loca { u32_at(&data, loca + 4 * i).ok_or_else(bad)? as usize }
                else { 2 * u16_at(&data, loca + 2 * i).ok_or_else(bad)? as usize };
            if off < previous || off > glyf_len { return Err(bad()); }
            previous = off;
        }
        // cmap subtable preference: (3,10) or (0,4|6) format 12, then (3,1) or (0,*) format 4
        let n = u16_at(&data, cmap + 2).ok_or_else(bad)? as usize;
        if 4 + 8 * n > cmap_len { return Err(bad()); }
        let mut best: Option<(u8, usize, u16, usize)> = None;
        for i in 0..n {
            let rec = cmap + 4 + 8 * i;
            let (pid, eid) = (u16_at(&data, rec).ok_or_else(bad)?, u16_at(&data, rec + 2).ok_or_else(bad)?);
            let rel = u32_at(&data, rec + 4).ok_or_else(bad)? as usize;
            if rel > cmap_len.saturating_sub(2) { continue; }
            let off = cmap + rel;
            let Some(format) = u16_at(&data, off) else { continue };
            let sub_len = match format {
                12 => u32_at(&data[..cmap + cmap_len], off + 4).map(|v| v as usize),
                4 => u16_at(&data[..cmap + cmap_len], off + 2).map(|v| v as usize),
                _ => None,
            };
            let Some(sub_len) = sub_len else { continue };
            if sub_len > cmap_len - rel { continue; }
            if format == 12 {
                if sub_len < 16 || 16u64 + 12 * u32_at(&data, off + 12).ok_or_else(bad)? as u64 > sub_len as u64 { continue; }
            } else {
                if sub_len < 16 { continue; }
                let seg2 = u16_at(&data, off + 6).ok_or_else(bad)? as usize;
                if seg2 == 0 || seg2 % 2 != 0 || 16 + 4 * seg2 > sub_len { continue; }
            }
            let rank = match (pid, eid, format) {
                (3, 10, 12) => 4,
                (0, _, 12) => 3,
                (3, 1, 4) => 2,
                (0, _, 4) => 1,
                _ => 0,
            };
            if rank > 0 && best.is_none_or(|b| rank > b.0) {
                best = Some((rank, off, format, off + sub_len));
            }
        }
        let (_, cmap_sub, cmap_format, cmap_end) = best.ok_or("font has no Unicode character map")?;
        Ok(Font { data, upem, ascender, descender, num_glyphs, long_loca, loca, glyf, glyf_end: glyf + glyf_len, hmtx, n_hmetrics, cmap: cmap_sub, cmap_end, cmap_format })
    }

    /// Glyph index of a code point; 0 when the font has none.
    pub fn glyph_index(&self, cp: u32) -> u32 {
        let d = &self.data[..self.cmap_end];
        let t = self.cmap;
        if self.cmap_format == 12 {
            let Some(n) = u32_at(d, t + 12) else { return 0 };
            let (mut lo, mut hi) = (0u32, n);
            while lo < hi {
                let mid = lo + (hi - lo) / 2;
                let g = t + 16 + 12 * mid as usize;
                let (Some(s), Some(e), Some(sg)) = (u32_at(d, g), u32_at(d, g + 4), u32_at(d, g + 8)) else { return 0 };
                if cp < s {
                    hi = mid;
                } else if cp > e {
                    lo = mid + 1;
                } else {
                    let Some(gid) = sg.checked_add(cp - s) else { return 0 };
                    return if gid < self.num_glyphs { gid } else { 0 };
                }
            }
            return 0;
        }
        if cp > 0xFFFF {
            return 0;
        }
        let Some(seg2) = u16_at(d, t + 6) else { return 0 };
        let segs = (seg2 / 2) as usize;
        let ends = t + 14;
        let starts = ends + 2 * segs + 2;
        let deltas = starts + 2 * segs;
        let ranges = deltas + 2 * segs;
        for i in 0..segs {
            let Some(end) = u16_at(d, ends + 2 * i) else { return 0 };
            if u32::from(end) < cp {
                continue;
            }
            let (Some(start), Some(delta), Some(range)) = (u16_at(d, starts + 2 * i), u16_at(d, deltas + 2 * i), u16_at(d, ranges + 2 * i)) else { return 0 };
            if u32::from(start) > cp {
                return 0;
            }
            let gid = if range == 0 {
                (cp as u16).wrapping_add(delta)
            } else {
                let at = ranges + 2 * i + range as usize + 2 * (cp - u32::from(start)) as usize;
                match u16_at(d, at) {
                    Some(0) | None => 0,
                    Some(g) => g.wrapping_add(delta),
                }
            };
            return if u32::from(gid) < self.num_glyphs { u32::from(gid) } else { 0 };
        }
        0
    }

    pub fn advance(&self, gid: u32) -> i64 {
        if self.n_hmetrics == 0 {
            return 0;
        }
        let i = gid.min(self.n_hmetrics - 1) as usize;
        i64::from(u16_at(&self.data, self.hmtx + 4 * i).unwrap_or(0))
    }

    fn glyph_range(&self, gid: u32) -> Option<(usize, usize)> {
        if gid >= self.num_glyphs {
            return None;
        }
        let g = gid as usize;
        let (a, b) = if self.long_loca {
            (u32_at(&self.data, self.loca + 4 * g)? as usize, u32_at(&self.data, self.loca + 4 * g + 4)? as usize)
        } else {
            (2 * u16_at(&self.data, self.loca + 2 * g)? as usize, 2 * u16_at(&self.data, self.loca + 2 * g + 2)? as usize)
        };
        (b >= a && self.glyf + b <= self.glyf_end).then_some((self.glyf + a, self.glyf + b))
    }

    /// Contours of a glyph in font units (composites resolved, depth ≤ 8).
    pub fn outline(&self, gid: u32) -> Vec<Vec<Pt>> {
        let mut out = Vec::new();
        let mut budget = 1 << 20;
        if self.outline_into(gid, [16384, 0, 0, 16384, 0, 0], &mut Vec::new(), &mut budget, &mut out).is_none() {
            out.clear();
        }
        out
    }

    fn outline_into(&self, gid: u32, m: [i64; 6], active: &mut Vec<u32>, budget: &mut usize, out: &mut Vec<Vec<Pt>>) -> Option<()> {
        if active.len() > 8 || active.contains(&gid) || *budget == 0 {
            return None;
        }
        *budget -= 1;
        let (start, end) = self.glyph_range(gid)?;
        if start == end { return Some(()); }
        let d = &self.data[..end];
        let nc = i16_at(d, start)?;
        let tf = |x: i64, y: i64| ((m[0] * x + m[2] * y) / 16384 + m[4], (m[1] * x + m[3] * y) / 16384 + m[5]);
        if nc >= 0 {
            let nc = nc as usize;
            if nc == 0 { return Some(()); }
            let mut ends = Vec::with_capacity(nc);
            for i in 0..nc {
                ends.push(u16_at(d, start + 10 + 2 * i)? as usize);
            }
            let npts = ends.last().map_or(0, |e| e + 1);
            *budget = budget.checked_sub(npts)?;
            let ins = u16_at(d, start + 10 + 2 * nc)? as usize;
            let mut p = start + 12 + 2 * nc + ins;
            let mut flags = Vec::with_capacity(npts);
            while flags.len() < npts {
                let f = *d.get(p)?;
                p += 1;
                flags.push(f);
                if f & 8 != 0 {
                    let r = *d.get(p)?;
                    p += 1;
                    for _ in 0..r {
                        if flags.len() < npts {
                            flags.push(f);
                        }
                    }
                }
            }
            let mut xs = Vec::with_capacity(npts);
            let mut v = 0i64;
            for &f in &flags {
                if f & 2 != 0 {
                    let b = i64::from(*d.get(p)?);
                    p += 1;
                    v += if f & 16 != 0 { b } else { -b };
                } else if f & 16 == 0 {
                    v += i64::from(i16_at(d, p)?);
                    p += 2;
                }
                xs.push(v);
            }
            let mut ys = Vec::with_capacity(npts);
            v = 0;
            for &f in &flags {
                if f & 4 != 0 {
                    let b = i64::from(*d.get(p)?);
                    p += 1;
                    v += if f & 32 != 0 { b } else { -b };
                } else if f & 32 == 0 {
                    v += i64::from(i16_at(d, p)?);
                    p += 2;
                }
                ys.push(v);
            }
            let mut from = 0;
            for &e in &ends {
                if e < from || e >= npts {
                    return None;
                }
                out.push((from..=e).map(|i| {
                    let (x, y) = tf(xs[i], ys[i]);
                    Pt { x, y, on: flags[i] & 1 != 0 }
                }).collect());
                from = e + 1;
            }
            return Some(());
        }
        if nc != -1 { return None; }
        let parent_start = out.len();
        let mut p = start + 10;
        loop {
            let flags = u16_at(d, p)?;
            let child = u32::from(u16_at(d, p + 2)?);
            p += 4;
            let (a1, a2) = if flags & 1 != 0 {
                let r = (i64::from(i16_at(d, p)?), i64::from(i16_at(d, p + 2)?));
                p += 4;
                r
            } else {
                let r = (i64::from(*d.get(p)? as i8), i64::from(*d.get(p + 1)? as i8));
                p += 2;
                r
            };
            let (mut dx, mut dy) = if flags & 2 != 0 { (a1, a2) } else { (0, 0) };
            let (a1, a2) = if flags & 2 != 0 { (0, 0) } else if flags & 1 != 0 {
                (a1 as u16 as usize, a2 as u16 as usize)
            } else { (a1 as u8 as usize, a2 as u8 as usize) };
            let mut c = [16384i64, 0, 0, 16384];
            if flags & 8 != 0 {
                let s = i64::from(i16_at(d, p)?);
                p += 2;
                c = [s, 0, 0, s];
            } else if flags & 0x40 != 0 {
                c = [i64::from(i16_at(d, p)?), 0, 0, i64::from(i16_at(d, p + 2)?)];
                p += 4;
            } else if flags & 0x80 != 0 {
                c = [i64::from(i16_at(d, p)?), i64::from(i16_at(d, p + 2)?), i64::from(i16_at(d, p + 4)?), i64::from(i16_at(d, p + 6)?)];
                p += 8;
            }
            if flags & 0x800 != 0 && flags & 2 != 0 {
                (dx, dy) = ((c[0] * dx + c[2] * dy) / 16384, (c[1] * dx + c[3] * dy) / 16384);
            }
            // child' = m ∘ (c, offset)
            let (ox, oy) = tf(dx, dy);
            let mm = [
                (m[0] * c[0] + m[2] * c[1]) / 16384,
                (m[1] * c[0] + m[3] * c[1]) / 16384,
                (m[0] * c[2] + m[2] * c[3]) / 16384,
                (m[1] * c[2] + m[3] * c[3]) / 16384,
                ox,
                oy,
            ];
            let child_start = out.len();
            active.push(gid);
            let result = self.outline_into(child, mm, active, budget, out);
            active.pop();
            result?;
            if flags & 2 == 0 {
                let parent = *out[parent_start..child_start].iter().flatten().nth(a1)?;
                let child = *out[child_start..].iter().flatten().nth(a2)?;
                let (dx, dy) = (parent.x.checked_sub(child.x)?, parent.y.checked_sub(child.y)?);
                for point in out[child_start..].iter_mut().flatten() {
                    point.x = point.x.checked_add(dx)?;
                    point.y = point.y.checked_add(dy)?;
                }
            }
            if flags & 0x20 == 0 {
                return Some(());
            }
        }
    }
}

/// Loaded fonts, ids from 1, as in the native runtime.
#[derive(Default)]
pub struct FontTable {
    pub fonts: Vec<Font>,
}

impl FontTable {
    pub fn load(&mut self, path: &str) -> Result<u128, String> {
        if self.fonts.len() >= 64 {
            return Err("too many fonts".into());
        }
        let data = std::fs::read(path).map_err(|e| format!("cannot read `{path}` (os error {})", e.raw_os_error().unwrap_or(0)))?;
        self.fonts.push(Font::parse(data)?);
        Ok(self.fonts.len() as u128)
    }

    pub fn get(&self, id: u128) -> Option<&Font> {
        id.checked_sub(1).and_then(|i| usize::try_from(i).ok()).and_then(|i| self.fonts.get(i))
    }
}

/// Font directories searched by `font_find`, in order.
pub fn font_dirs() -> Vec<String> {
    let mut v = Vec::new();
    if let Ok(h) = std::env::var("HOME") {
        v.push(format!("{h}/.local/share/fonts"));
        v.push(format!("{h}/.fonts"));
    }
    v.extend(["/usr/share/fonts", "/usr/local/share/fonts", "/Library/Fonts", "/System/Library/Fonts", "C:/Windows/Fonts"].map(String::from));
    v
}

fn find_in(dir: &std::path::Path, needle: &str, depth: u32) -> Option<String> {
    if depth > 8 {
        return None;
    }
    let mut names: Vec<String> = std::fs::read_dir(dir).ok()?.filter_map(|e| e.ok()?.file_name().into_string().ok()).collect();
    names.sort();
    for n in &names {
        let p = dir.join(n);
        let lower = n.to_ascii_lowercase();
        let font = lower.ends_with(".ttf") || lower.ends_with(".ttc");
        if font && lower.contains(needle) && std::fs::metadata(&p).is_ok_and(|m| m.is_file()) {
            return Some(p.display().to_string());
        }
    }
    for n in &names {
        let p = dir.join(n);
        if std::fs::symlink_metadata(&p).is_ok_and(|m| m.is_dir()) {
            if let Some(f) = find_in(&p, needle, depth + 1) {
                return Some(f);
            }
        }
    }
    None
}

/// The first `.ttf`/`.ttc` file whose name contains `name` (ASCII case-insensitive).
pub fn find(name: &str) -> Result<String, String> {
    let needle = name.to_ascii_lowercase();
    for d in font_dirs() {
        if let Some(f) = find_in(std::path::Path::new(&d), &needle, 0) {
            return Ok(f);
        }
    }
    Err(format!("no font file matching `{name}`"))
}
