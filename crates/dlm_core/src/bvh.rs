//! v0.124: triangle-mesh queries. A bounding-volume hierarchy (median split on
//! the longest centroid axis, leaves of at most 4 triangles, padded boxes) for
//! nearest ray hits (Möller–Trumbore, ties broken by the smaller triangle
//! index, so the answer does not depend on the tree), generalized winding
//! numbers (Van Oosterom–Strackee solid angles, point-in-mesh), and AABB
//! overlap pairs by sweep and prune. The native runtime (`dvh_*`) performs the
//! same floating-point operations in the same order.
const LEAF: usize = 4;

pub struct Node {
    lo: [f64; 3],
    hi: [f64; 3],
    left: usize,
    right: usize,
    start: usize,
    count: usize,
}

fn finite(xs: &[f64]) -> bool {
    xs.iter().all(|x| x.is_finite())
}

fn vtx(t: &[f64], i: usize, k: usize) -> [f64; 3] {
    let b = 9 * i + 3 * k;
    [t[b], t[b + 1], t[b + 2]]
}

fn build(t: &[f64], idx: &mut [usize], start: usize, nodes: &mut Vec<Node>) -> usize {
    let mut lo = [f64::INFINITY; 3];
    let mut hi = [f64::NEG_INFINITY; 3];
    let mut clo = [f64::INFINITY; 3];
    let mut chi = [f64::NEG_INFINITY; 3];
    for &i in idx.iter() {
        for k in 0..3 {
            let v = vtx(t, i, k);
            for a in 0..3 {
                lo[a] = lo[a].min(v[a]);
                hi[a] = hi[a].max(v[a]);
            }
        }
        for a in 0..3 {
            let c = t[9 * i + a] + t[9 * i + 3 + a] + t[9 * i + 6 + a];
            clo[a] = clo[a].min(c);
            chi[a] = chi[a].max(c);
        }
    }
    for a in 0..3 {
        let pad = 1e-9 * (1.0 + lo[a].abs().max(hi[a].abs()));
        lo[a] -= pad;
        hi[a] += pad;
    }
    let me = nodes.len();
    nodes.push(Node { lo, hi, left: 0, right: 0, start, count: idx.len() });
    if idx.len() <= LEAF {
        return me;
    }
    let mut axis = 0;
    for a in 1..3 {
        if chi[a] - clo[a] > chi[axis] - clo[axis] {
            axis = a;
        }
    }
    let key = |i: usize| t[9 * i + axis] + t[9 * i + 3 + axis] + t[9 * i + 6 + axis];
    idx.sort_by(|&x, &y| key(x).total_cmp(&key(y)).then(x.cmp(&y)));
    let mid = idx.len() / 2;
    let (l, r) = idx.split_at_mut(mid);
    let left = build(t, l, start, nodes);
    let right = build(t, r, start + mid, nodes);
    nodes[me].left = left;
    nodes[me].right = right;
    nodes[me].count = 0;
    me
}

pub fn tree(t: &[f64]) -> Option<(Vec<Node>, Vec<usize>)> {
    if t.len() % 9 != 0 || !finite(t) {
        return None;
    }
    let n = t.len() / 9;
    let mut idx: Vec<usize> = (0..n).collect();
    let mut nodes = Vec::new();
    if n > 0 {
        build(t, &mut idx, 0, &mut nodes);
    }
    Some((nodes, idx))
}

fn sub(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

fn dot(a: [f64; 3], b: [f64; 3]) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn hit_scaled(t: &[f64], i: usize, o: [f64; 3], d: [f64; 3]) -> Option<f64> {
    let v0 = vtx(t, i, 0);
    let mut edges = [sub(vtx(t, i, 1), v0), sub(vtx(t, i, 2), v0), sub(o, v0)];
    let half = edges.iter().flatten().any(|x| !x.is_finite());
    if half {
        let vs = [vtx(t, i, 1), vtx(t, i, 2), o];
        for j in 0..3 { for k in 0..3 { edges[j][k] = vs[j][k] * 0.5 - v0[k] * 0.5; } }
    }
    let scale = edges.iter().flatten().fold(0.0f64, |s, x| s.max(x.abs()));
    let ds = d.iter().fold(0.0f64, |s, x| s.max(x.abs()));
    if scale == 0.0 || ds == 0.0 { return None; }
    for e in &mut edges { for x in e { *x /= scale; } }
    let dd = [d[0] / ds, d[1] / ds, d[2] / ds];
    let [e1, e2, s] = edges;
    let p = cross(dd, e2);
    let det = dot(e1, p);
    if det == 0.0 { return None; }
    let u = dot(s, p) / det;
    if !(0.0..=1.0).contains(&u) { return None; }
    let q = cross(s, e1);
    let v = dot(dd, q) / det;
    if !v.is_finite() || v < 0.0 || u + v > 1.0 { return None; }
    let h = dot(e2, q) / det;
    let factor = if half { 2.0 } else { 1.0 };
    for tt in [h * (scale / ds) * factor, ((h * scale) / ds) * factor, ((h / ds) * scale) * factor] {
        if tt.is_finite() && tt > 0.0 { return Some(tt); }
    }
    None
}

/// Möller–Trumbore; `t > 0` on a hit.
fn hit(t: &[f64], i: usize, o: [f64; 3], d: [f64; 3]) -> Option<f64> {
    let v0 = vtx(t, i, 0);
    let e1 = sub(vtx(t, i, 1), v0);
    let e2 = sub(vtx(t, i, 2), v0);
    let p = cross(d, e2);
    let det = dot(e1, p);
    if det == 0.0 || !det.is_finite() {
        return hit_scaled(t, i, o, d);
    }
    let inv = 1.0 / det;
    let s = sub(o, v0);
    let u = dot(s, p) * inv;
    if !u.is_finite() { return hit_scaled(t, i, o, d); }
    if !(0.0..=1.0).contains(&u) {
        return None;
    }
    let q = cross(s, e1);
    let v = dot(d, q) * inv;
    if !v.is_finite() { return hit_scaled(t, i, o, d); }
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let tt = dot(e2, q) * inv;
    if tt == 0.0 || !tt.is_finite() { return hit_scaled(t, i, o, d); }
    (tt > 0.0).then_some(tt)
}

fn slab(n: &Node, o: [f64; 3], d: [f64; 3], best: f64) -> bool {
    let (mut t0, mut t1) = (0.0f64, best);
    for a in 0..3 {
        if d[a] == 0.0 {
            if o[a] < n.lo[a] || o[a] > n.hi[a] {
                return false;
            }
            continue;
        }
        let lower = n.lo[a] - o[a];
        let upper = n.hi[a] - o[a];
        let mut ta = if lower.is_finite() { lower / d[a] } else { (n.lo[a] * 0.5 - o[a] * 0.5) / (d[a] * 0.5) };
        let mut tb = if upper.is_finite() { upper / d[a] } else { (n.hi[a] * 0.5 - o[a] * 0.5) / (d[a] * 0.5) };
        if ta > tb {
            std::mem::swap(&mut ta, &mut tb);
        }
        if ta > t0 {
            t0 = ta;
        }
        if tb < t1 {
            t1 = tb;
        }
        if t0 > t1 {
            return false;
        }
    }
    true
}

/// For each ray `(origin, direction)`: `[triangle, t]` of the nearest hit, `[-1, -1]` on a miss.
pub fn raycast(t: &[f64], origins: &[f64], dirs: &[f64]) -> Option<Vec<f64>> {
    if origins.len() != dirs.len() || origins.len() % 3 != 0 || !finite(origins) || !finite(dirs) {
        return None;
    }
    let (nodes, idx) = tree(t)?;
    let mut out = Vec::with_capacity(2 * origins.len() / 3);
    let mut stack = Vec::new();
    for r in 0..origins.len() / 3 {
        let o = [origins[3 * r], origins[3 * r + 1], origins[3 * r + 2]];
        let d = [dirs[3 * r], dirs[3 * r + 1], dirs[3 * r + 2]];
        let (mut bt, mut bi) = (f64::INFINITY, usize::MAX);
        stack.clear();
        if !nodes.is_empty() {
            stack.push(0usize);
        }
        while let Some(k) = stack.pop() {
            let nd = &nodes[k];
            if !slab(nd, o, d, bt) {
                continue;
            }
            if nd.count > 0 {
                for &i in &idx[nd.start..nd.start + nd.count] {
                    if let Some(h) = hit(t, i, o, d) {
                        if h < bt || (h == bt && i < bi) {
                            bt = h;
                            bi = i;
                        }
                    }
                }
            } else {
                stack.push(nd.right);
                stack.push(nd.left);
            }
        }
        if bi == usize::MAX {
            out.extend([-1.0, -1.0]);
        } else {
            out.extend([bi as f64, bt]);
        }
    }
    Some(out)
}

fn norm(a: [f64; 3]) -> f64 {
    dot(a, a).sqrt()
}

fn unit_from(v: [f64; 3], p: [f64; 3]) -> [f64; 3] {
    let mut a = sub(v, p);
    if a.iter().any(|x| !x.is_finite()) {
        for k in 0..3 { a[k] = v[k] * 0.5 - p[k] * 0.5; }
    }
    let scale = a.iter().fold(0.0f64, |s, x| s.max(x.abs()));
    if scale == 0.0 { return [0.0; 3]; }
    for x in &mut a { *x /= scale; }
    let length = norm(a);
    for x in &mut a { *x /= length; }
    a
}

/// Generalized winding number of each point (about 1 inside a closed
/// outward-oriented mesh, 0 outside).
pub fn winding(t: &[f64], points: &[f64]) -> Option<Vec<f64>> {
    if t.len() % 9 != 0 || points.len() % 3 != 0 || !finite(t) || !finite(points) {
        return None;
    }
    let mut out = Vec::with_capacity(points.len() / 3);
    for p in points.chunks(3) {
        let p = [p[0], p[1], p[2]];
        let mut w = 0.0f64;
        for i in 0..t.len() / 9 {
            let a = sub(vtx(t, i, 0), p);
            let b = sub(vtx(t, i, 1), p);
            let c = sub(vtx(t, i, 2), p);
            let (la, lb, lc) = (norm(a), norm(b), norm(c));
            let mut det = dot(a, cross(b, c));
            let mut den = la * lb * lc + dot(a, b) * lc + dot(a, c) * lb + dot(b, c) * la;
            if !det.is_finite() || !den.is_finite() || (det == 0.0 && den == 0.0) {
                let a = unit_from(vtx(t, i, 0), p);
                let b = unit_from(vtx(t, i, 1), p);
                let c = unit_from(vtx(t, i, 2), p);
                let (la, lb, lc) = (norm(a), norm(b), norm(c));
                det = dot(a, cross(b, c));
                den = la * lb * lc + dot(a, b) * lc + dot(a, c) * lb + dot(b, c) * la;
            }
            w += 2.0 * det.atan2(den);
        }
        out.push(w / (4.0 * std::f64::consts::PI));
    }
    Some(out)
}

/// Pairs `i < j` of closed boxes `[lo_x, lo_y, lo_z, hi_x, hi_y, hi_z]` that intersect, sorted.
pub fn aabb_pairs(b: &[f64]) -> Option<Vec<u128>> {
    if b.len() % 6 != 0 || !finite(b) {
        return None;
    }
    let n = b.len() / 6;
    if (0..n).any(|i| (0..3).any(|a| b[6 * i + a] > b[6 * i + 3 + a])) {
        return None;
    }
    let mut order: Vec<usize> = (0..n).collect();
    order.sort_by(|&x, &y| b[6 * x].total_cmp(&b[6 * y]).then(x.cmp(&y)));
    let mut active: Vec<usize> = Vec::new();
    let mut pairs: Vec<(usize, usize)> = Vec::new();
    for &i in &order {
        active.retain(|&j| b[6 * j + 3] >= b[6 * i]);
        for &j in &active {
            if (1..3).all(|a| b[6 * i + a] <= b[6 * j + 3 + a] && b[6 * j + a] <= b[6 * i + 3 + a]) {
                pairs.push((i.min(j), i.max(j)));
            }
        }
        active.push(i);
        if pairs.len() > crate::graph::VEC_MAX / 2 {
            return None;
        }
    }
    pairs.sort_unstable();
    Some(pairs.into_iter().flat_map(|(i, j)| [i as u128, j as u128]).collect())
}
