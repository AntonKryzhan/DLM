//! v0.123: exact geometric predicates, Delaunay triangulation and k-d tree
//! queries on machine floats.
//!
//! * Predicates evaluate the sign of the determinant exactly with Shewchuk-style
//!   floating-point expansions (two-sum, Dekker two-product, no fused
//!   multiply-add). Coordinates must be 0 or have magnitude in
//!   `[2^-150, 2^150]`; otherwise `None`. Result: 0 zero, 1 positive, 2 negative.
//! * Delaunay: lexicographic sweep triangulation followed by Lawson flips with
//!   the exact in-circle test until every interior edge is locally Delaunay;
//!   triangles are counter-clockwise, rotated to start at their smallest
//!   vertex and sorted. Duplicate points keep their first occurrence.
//! * k-d tree: median splits on `depth mod d` (ties by index); k nearest
//!   neighbours by (squared distance, index), radius queries.
//! The native runtime (`dvx_*`) gives the same results.

fn two_sum(a: f64, b: f64) -> (f64, f64) {
    let x = a + b;
    let bv = x - a;
    let av = x - bv;
    let br = b - bv;
    let ar = a - av;
    (x, ar + br)
}

fn split(a: f64) -> (f64, f64) {
    let c = 134217729.0 * a;
    let abig = c - a;
    let hi = c - abig;
    (hi, a - hi)
}

fn two_product(a: f64, b: f64) -> (f64, f64) {
    let x = a * b;
    let (ah, al) = split(a);
    let (bh, bl) = split(b);
    let e1 = x - ah * bh;
    let e2 = e1 - al * bh;
    let e3 = e2 - ah * bl;
    (x, al * bl - e3)
}

type Exp = Vec<f64>;

fn grow(e: &[f64], b: f64) -> Exp {
    let mut out = Vec::with_capacity(e.len() + 1);
    let mut q = b;
    for &c in e {
        let (s, h) = two_sum(q, c);
        if h != 0.0 {
            out.push(h);
        }
        q = s;
    }
    if q != 0.0 || out.is_empty() {
        out.push(q);
    }
    out
}

fn add(e: &[f64], f: &[f64]) -> Exp {
    let mut r = e.to_vec();
    for &c in f {
        r = grow(&r, c);
    }
    r
}

fn neg(e: &[f64]) -> Exp {
    e.iter().map(|x| -x).collect()
}

fn scale(e: &[f64], b: f64) -> Exp {
    let mut r: Exp = vec![0.0];
    for &c in e {
        let (x, y) = two_product(c, b);
        r = grow(&grow(&r, y), x);
    }
    r
}

fn mul(e: &[f64], f: &[f64]) -> Exp {
    let mut r: Exp = vec![0.0];
    for &c in f {
        r = add(&r, &scale(e, c));
    }
    r
}

fn diff(a: f64, b: f64) -> Exp {
    let (x, y) = two_sum(a, -b);
    grow(&[y], x)
}

fn sign(e: &[f64]) -> u128 {
    match e.iter().rev().find(|x| **x != 0.0) {
        None => 0,
        Some(x) if *x > 0.0 => 1,
        _ => 2,
    }
}

fn ok(xs: &[f64]) -> bool {
    let lo = 2f64.powi(-150);
    let hi = 2f64.powi(150);
    xs.iter()
        .all(|x| *x == 0.0 || (x.is_finite() && x.abs() >= lo && x.abs() <= hi))
}

fn orient2d_exp(a: (f64, f64), b: (f64, f64), c: (f64, f64)) -> Exp {
    let acx = diff(a.0, c.0);
    let bcy = diff(b.1, c.1);
    let acy = diff(a.1, c.1);
    let bcx = diff(b.0, c.0);
    add(&mul(&acx, &bcy), &neg(&mul(&acy, &bcx)))
}

/// Sign of `(a - c) × (b - c)`: 1 counter-clockwise, 2 clockwise, 0 collinear.
pub fn orient2d(p: &[f64]) -> Option<u128> {
    if p.len() != 6 || !ok(p) {
        return None;
    }
    Some(sign(&orient2d_exp(
        (p[0], p[1]),
        (p[2], p[3]),
        (p[4], p[5]),
    )))
}

fn incircle_exp(a: (f64, f64), b: (f64, f64), c: (f64, f64), d: (f64, f64)) -> Exp {
    let (adx, ady) = (diff(a.0, d.0), diff(a.1, d.1));
    let (bdx, bdy) = (diff(b.0, d.0), diff(b.1, d.1));
    let (cdx, cdy) = (diff(c.0, d.0), diff(c.1, d.1));
    let lift = |x: &Exp, y: &Exp| add(&mul(x, x), &mul(y, y));
    let cross = |x1: &Exp, y1: &Exp, x2: &Exp, y2: &Exp| add(&mul(x1, y2), &neg(&mul(x2, y1)));
    let t1 = mul(&lift(&adx, &ady), &cross(&bdx, &bdy, &cdx, &cdy));
    let t2 = mul(&lift(&bdx, &bdy), &cross(&cdx, &cdy, &adx, &ady));
    let t3 = mul(&lift(&cdx, &cdy), &cross(&adx, &ady, &bdx, &bdy));
    add(&add(&t1, &t2), &t3)
}

/// 1 when `d` lies strictly inside the circle through counter-clockwise `a, b, c`, 2 outside, 0 on it.
pub fn incircle(p: &[f64]) -> Option<u128> {
    if p.len() != 8 || !ok(p) {
        return None;
    }
    Some(sign(&incircle_exp(
        (p[0], p[1]),
        (p[2], p[3]),
        (p[4], p[5]),
        (p[6], p[7]),
    )))
}

/// Sign of `det[a - d, b - d, c - d]` (1 when `d` is below the plane of counter-clockwise `a, b, c` seen from above).
pub fn orient3d(p: &[f64]) -> Option<u128> {
    if p.len() != 12 || !ok(p) {
        return None;
    }
    let d = |i: usize, k: usize| diff(p[3 * i + k], p[9 + k]);
    let (ax, ay, az) = (d(0, 0), d(0, 1), d(0, 2));
    let (bx, by, bz) = (d(1, 0), d(1, 1), d(1, 2));
    let (cx, cy, cz) = (d(2, 0), d(2, 1), d(2, 2));
    let minor = |p1: &Exp, q1: &Exp, p2: &Exp, q2: &Exp| add(&mul(p1, q2), &neg(&mul(p2, q1)));
    let t1 = mul(&ax, &minor(&by, &bz, &cy, &cz));
    let t2 = mul(&ay, &minor(&bz, &bx, &cz, &cx));
    let t3 = mul(&az, &minor(&bx, &by, &cx, &cy));
    Some(sign(&add(&add(&t1, &t2), &t3)))
}

// ---------------- Delaunay ----------------

fn orient(pts: &[(f64, f64)], a: usize, b: usize, c: usize) -> u128 {
    sign(&orient2d_exp(pts[a], pts[b], pts[c]))
}

fn inside(pts: &[(f64, f64)], a: usize, b: usize, c: usize, d: usize) -> bool {
    sign(&incircle_exp(pts[a], pts[b], pts[c], pts[d])) == 1
}

/// Delaunay triangulation of `n` points `[x0, y0, x1, y1, ...]`: triangles as index triples.
pub fn delaunay(p: &[f64]) -> Option<Vec<u128>> {
    if p.len() % 2 != 0 || p.len() / 2 > 1 << 16 || !ok(p) {
        return None;
    }
    let all: Vec<(f64, f64)> = p.chunks(2).map(|c| (c[0], c[1])).collect();
    let mut order: Vec<usize> = (0..all.len()).collect();
    order.sort_by(|&i, &j| all[i].partial_cmp(&all[j]).unwrap().then(i.cmp(&j)));
    order.dedup_by(|b, a| all[*a] == all[*b]);
    let n = order.len();
    let pts: Vec<(f64, f64)> = order.iter().map(|&i| all[i]).collect();
    // first point off the line through pts[0], pts[1]
    let Some(k) = (2..n).find(|&k| orient(&pts, 0, 1, k) != 0) else {
        return Some(Vec::new());
    };
    let mut tris: Vec<[usize; 3]> = Vec::new();
    let ccw = |t: [usize; 3], pts: &[(f64, f64)]| {
        if orient(pts, t[0], t[1], t[2]) == 2 {
            [t[0], t[2], t[1]]
        } else {
            t
        }
    };
    for i in 0..k - 1 {
        tris.push(ccw([i, i + 1, k], &pts));
    }
    // counter-clockwise hull of points 0..=k
    let mut hull: Vec<usize> = if orient(&pts, 0, 1, k) == 1 {
        let mut h: Vec<usize> = (0..k).collect();
        h.push(k);
        h
    } else {
        let mut h = vec![0, k];
        h.extend((1..k).rev());
        h
    };
    for q in k + 1..n {
        let m = hull.len();
        let visible: Vec<bool> = (0..m)
            .map(|e| orient(&pts, hull[e], hull[(e + 1) % m], q) == 2)
            .collect();
        // first visible edge after an invisible one
        let start = (0..m)
            .find(|&e| visible[e] && !visible[(e + m - 1) % m])
            .unwrap_or(0);
        let mut e = start;
        let mut count = 0;
        while visible[e] && count < m {
            tris.push([hull[(e + 1) % m], hull[e], q]);
            e = (e + 1) % m;
            count += 1;
        }
        // hull[start] .. hull[e] are replaced by hull[start], q, hull[e]
        let mut next = Vec::with_capacity(m + 1);
        let mut i = e;
        loop {
            next.push(hull[i]);
            if i == start {
                break;
            }
            i = (i + 1) % m;
        }
        next.push(q);
        hull = next;
    }
    // Lawson flips
    let mut flips = 0usize;
    loop {
        let mut edges: Vec<(usize, usize, usize)> = Vec::with_capacity(3 * tris.len());
        for (t, tri) in tris.iter().enumerate() {
            for s in 0..3 {
                let (a, b) = (tri[s], tri[(s + 1) % 3]);
                edges.push((a.min(b), a.max(b), t));
            }
        }
        edges.sort_unstable();
        let mut dirty = vec![false; tris.len()];
        let mut changed = false;
        for w in edges.windows(2) {
            let ((a, b, t1), (c, d, t2)) = (w[0], w[1]);
            if (a, b) != (c, d) || dirty[t1] || dirty[t2] {
                continue;
            }
            let opp = |t: [usize; 3]| t.iter().copied().find(|v| *v != a && *v != b).unwrap();
            let (x, y) = (opp(tris[t1]), opp(tris[t2]));
            // tris[t1] is ccw (u, v, x) with {u, v} = {a, b}
            let r = tris[t1].iter().position(|v| *v == x).unwrap();
            let (u, v) = (tris[t1][(r + 1) % 3], tris[t1][(r + 2) % 3]);
            if inside(&pts, u, v, x, y) {
                tris[t1] = [x, u, y];
                tris[t2] = [y, v, x];
                dirty[t1] = true;
                dirty[t2] = true;
                changed = true;
                flips += 1;
                if flips > 1 << 26 {
                    return None;
                }
            }
        }
        if !changed {
            break;
        }
    }
    let mut out: Vec<[usize; 3]> = tris
        .into_iter()
        .map(|t| {
            let t = [order[t[0]], order[t[1]], order[t[2]]];
            let r = (0..3).min_by_key(|i| t[*i]).unwrap();
            [t[r], t[(r + 1) % 3], t[(r + 2) % 3]]
        })
        .collect();
    out.sort_unstable();
    Some(out.into_iter().flatten().map(|v| v as u128).collect())
}

// ---------------- k-d tree ----------------

// Squared distances as a mantissa and an unbounded binary exponent: neither
// very large nor very small finite coordinates collapse to infinity or zero.
type Distance = (i32, f64);
fn distance(p: &[f64], q: &[f64]) -> Distance {
    let half = p.iter().zip(q).any(|(a, b)| !(a - b).is_finite());
    let mut delta = [0.0f64; 64];
    let mut scale = 0.0f64;
    for i in 0..p.len() {
        delta[i] = if half { p[i] * 0.5 - q[i] * 0.5 } else { p[i] - q[i] };
        scale = scale.max(delta[i].abs());
    }
    if scale == 0.0 { return (i32::MIN, 0.0); }
    if scale.is_infinite() { return (i32::MAX, 0.0); }
    let mut bits = scale.to_bits();
    let mut exponent = if half { 1 } else { 0 };
    if bits >> 52 == 0 { bits = (scale * 4503599627370496.0).to_bits(); exponent -= 52; }
    exponent += (bits >> 52) as i32 - 1023;
    let mantissa = f64::from_bits((bits & ((1u64 << 52) - 1)) | (1023u64 << 52));
    let mut sum = 0.0;
    for x in &delta[..p.len()] { let v = x / scale; sum = sum + v * v; }
    let mut square = mantissa * mantissa * sum;
    while square >= 4.0 { square *= 0.25; exponent += 1; }
    (exponent, square)
}

struct Kd<'a> {
    p: &'a [f64],
    d: usize,
    nodes: Vec<(usize, usize, usize)>, // (point, left, right), usize::MAX = none
}

const NIL: usize = usize::MAX;

impl Kd<'_> {
    fn build(&mut self, idx: &mut [usize], depth: usize) -> usize {
        if idx.is_empty() {
            return NIL;
        }
        let axis = depth % self.d;
        let p = self.p;
        let d = self.d;
        idx.sort_by(|&i, &j| {
            p[i * d + axis]
                .partial_cmp(&p[j * d + axis])
                .unwrap()
                .then(i.cmp(&j))
        });
        let mid = idx.len() / 2;
        let point = idx[mid];
        let node = self.nodes.len();
        self.nodes.push((point, NIL, NIL));
        let (left, right) = idx.split_at_mut(mid);
        let l = self.build(left, depth + 1);
        let r = self.build(&mut right[1..], depth + 1);
        self.nodes[node].1 = l;
        self.nodes[node].2 = r;
        node
    }
    fn dist(&self, i: usize, q: &[f64]) -> Distance {
        distance(&self.p[i * self.d..(i + 1) * self.d], q)
    }
    fn knn(&self, node: usize, depth: usize, q: &[f64], k: usize, best: &mut Vec<(Distance, usize)>) {
        if node == NIL {
            return;
        }
        let (i, l, r) = self.nodes[node];
        let dd = self.dist(i, q);
        let pos = best.partition_point(|e| e.0 < dd || (e.0 == dd && e.1 < i));
        if pos < k {
            best.insert(pos, (dd, i));
            best.truncate(k);
        }
        let axis = depth % self.d;
        let diff = q[axis] - self.p[i * self.d + axis];
        let (near, far) = if diff < 0.0 { (l, r) } else { (r, l) };
        self.knn(near, depth + 1, q, k, best);
        let plane = distance(&[q[axis]], &[self.p[i * self.d + axis]]);
        if best.len() < k || plane <= best[best.len() - 1].0 {
            self.knn(far, depth + 1, q, k, best);
        }
    }
    fn radius(&self, node: usize, depth: usize, q: &[f64], r2: Distance, out: &mut Vec<usize>) {
        if node == NIL {
            return;
        }
        let (i, l, r) = self.nodes[node];
        if self.dist(i, q) <= r2 {
            out.push(i);
        }
        let axis = depth % self.d;
        let diff = q[axis] - self.p[i * self.d + axis];
        let plane = distance(&[q[axis]], &[self.p[i * self.d + axis]]);
        if diff < 0.0 || plane <= r2 {
            self.radius(l, depth + 1, q, r2, out);
        }
        if diff >= 0.0 || plane <= r2 {
            self.radius(r, depth + 1, q, r2, out);
        }
    }
}

fn kd_tree(p: &[f64], d: u128) -> Option<(Kd<'_>, usize)> {
    let d = usize::try_from(d).ok().filter(|d| *d > 0 && *d <= 64)?;
    if p.len() % d != 0 || p.iter().any(|x| !x.is_finite()) {
        return None;
    }
    let n = p.len() / d;
    let mut kd = Kd {
        p,
        d,
        nodes: Vec::with_capacity(n),
    };
    let mut idx: Vec<usize> = (0..n).collect();
    let root = kd.build(&mut idx, 0);
    Some((kd, root))
}

/// For each query row, the indices of its `k` nearest points (ordered by distance, then index).
pub fn knn(points: &[f64], queries: &[f64], d: u128, k: u128) -> Option<Vec<u128>> {
    let (kd, root) = kd_tree(points, d)?;
    let dd = kd.d;
    if queries.len() % dd != 0 || queries.iter().any(|x| !x.is_finite()) {
        return None;
    }
    let k = usize::try_from(k)
        .unwrap_or(usize::MAX)
        .min(points.len() / dd);
    if (queries.len() / dd).checked_mul(k)? > crate::graph::VEC_MAX {
        return None;
    }
    let mut out = Vec::new();
    for q in queries.chunks(dd) {
        let mut best = Vec::with_capacity(k + 1);
        if k > 0 {
            kd.knn(root, 0, q, k, &mut best);
        }
        out.extend(best.into_iter().map(|e| e.1 as u128));
    }
    Some(out)
}

/// Indices (increasing) of the points within distance `r` of `center`.
pub fn radius(points: &[f64], d: u128, center: &[f64], r: f64) -> Option<Vec<u128>> {
    let (kd, root) = kd_tree(points, d)?;
    if center.len() != kd.d || center.iter().any(|x| !x.is_finite()) || r.is_nan() || r < 0.0 {
        return None;
    }
    let mut out = Vec::new();
    kd.radius(root, 0, center, distance(&[r], &[0.0]), &mut out);
    out.sort_unstable();
    Some(out.into_iter().map(|v| v as u128).collect())
}
