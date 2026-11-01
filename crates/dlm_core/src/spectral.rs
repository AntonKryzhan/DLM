//! v0.117: fast Fourier transforms, FFT convolution and dense linear
//! programming. Complex vectors are interleaved `FloatVec`s `[re0, im0, re1,
//! im1, ...]`. Power-of-two lengths use the iterative radix-2 transform,
//! other lengths a direct DFT (at most `2^13` points); twiddle factors are
//! `cos`/`sin` of `sign·τ·j/len` evaluated per index. `lp_*` is a two-phase
//! tableau simplex with Bland's rule. The native runtime (`dva_*`) performs
//! the same operation sequences.
use std::f64::consts::TAU;

pub const DFT_MAX: usize = 1 << 13;
pub const LP_EPS: f64 = 1e-9;
pub const LP_ITERATIONS: usize = 50_000;

pub fn fft(x: &[f64], inverse: bool) -> Option<Vec<f64>> {
    if x.len() % 2 != 0 {
        return None;
    }
    let n = x.len() / 2;
    let sign = if inverse { 1.0 } else { -1.0 };
    let mut out = if n.is_power_of_two() || n == 0 {
        let mut a = x.to_vec();
        let bits = n.trailing_zeros();
        for i in 0..n {
            let j = if bits == 0 {
                0
            } else {
                i.reverse_bits() >> (usize::BITS - bits)
            };
            if j > i {
                a.swap(2 * i, 2 * j);
                a.swap(2 * i + 1, 2 * j + 1);
            }
        }
        let mut len = 2;
        while len <= n {
            let ang = sign * TAU / (len as f64);
            let half = len / 2;
            for i in (0..n).step_by(len) {
                for j in 0..half {
                    let t = ang * (j as f64);
                    let (wr, wi) = (t.cos(), t.sin());
                    let (p, q) = (2 * (i + j), 2 * (i + j + half));
                    let (br, bi) = (a[q], a[q + 1]);
                    let (r1, r2, i1, i2) = (br * wr, bi * wi, br * wi, bi * wr);
                    let (vr, vi) = (r1 - r2, i1 + i2);
                    let (ur, ui) = (a[p], a[p + 1]);
                    a[p] = ur + vr;
                    a[p + 1] = ui + vi;
                    a[q] = ur - vr;
                    a[q + 1] = ui - vi;
                }
            }
            len *= 2;
        }
        a
    } else {
        if n > DFT_MAX {
            return None;
        }
        let ang = sign * TAU / (n as f64);
        let mut a = vec![0.0; 2 * n];
        for k in 0..n {
            let (mut sr, mut si) = (0.0, 0.0);
            for t in 0..n {
                let e = ang * (((k * t) % n) as f64);
                let (c, s) = (e.cos(), e.sin());
                let (xr, xi) = (x[2 * t], x[2 * t + 1]);
                let (r1, r2, i1, i2) = (xr * c, xi * s, xr * s, xi * c);
                let (pr, pi) = (r1 - r2, i1 + i2);
                sr = sr + pr;
                si = si + pi;
            }
            a[2 * k] = sr;
            a[2 * k + 1] = si;
        }
        a
    };
    if inverse && n > 0 {
        let d = n as f64;
        for v in &mut out {
            *v = *v / d;
        }
    }
    Some(out)
}

/// Linear convolution of two real sequences through a power-of-two FFT.
pub fn convolve(a: &[f64], b: &[f64]) -> Option<Vec<f64>> {
    if a.is_empty() || b.is_empty() {
        return Some(Vec::new());
    }
    let len = a.len() + b.len() - 1;
    let size = len.checked_next_power_of_two()?;
    if size > crate::float::VEC_MAX / 2 {
        return None;
    }
    let pad = |x: &[f64]| {
        let mut v = vec![0.0; 2 * size];
        for (i, y) in x.iter().enumerate() {
            v[2 * i] = *y;
        }
        v
    };
    let (fa, fb) = (fft(&pad(a), false)?, fft(&pad(b), false)?);
    let mut prod = vec![0.0; 2 * size];
    for k in 0..size {
        let (ar, ai, br, bi) = (fa[2 * k], fa[2 * k + 1], fb[2 * k], fb[2 * k + 1]);
        let (r1, r2, i1, i2) = (ar * br, ai * bi, ar * bi, ai * br);
        prod[2 * k] = r1 - r2;
        prod[2 * k + 1] = i1 + i2;
    }
    let back = fft(&prod, true)?;
    Some((0..len).map(|i| back[2 * i]).collect())
}

/// `0` optimal, `1` infeasible, `2` unbounded, `3` malformed input, `4` iteration limit.
pub fn simplex(c: &[f64], a: &[f64], b: &[f64], m: u128, n: u128) -> (u128, Vec<f64>) {
    simplex_inner(c, a, b, m, n, false)
}

fn simplex_inner(
    c: &[f64],
    a: &[f64],
    b: &[f64],
    m: u128,
    n: u128,
    feasible_unbounded: bool,
) -> (u128, Vec<f64>) {
    let bad = (3, Vec::new());
    let (Ok(m), Ok(n)) = (usize::try_from(m), usize::try_from(n)) else {
        return bad;
    };
    if c.len() != n
        || b.len() != m
        || m.checked_mul(n) != Some(a.len())
        || !c.iter().chain(a).chain(b).all(|x| x.is_finite())
    {
        return bad;
    }
    let k = b.iter().filter(|x| **x < 0.0).count();
    let cols = n + m + k;
    let w = cols + 1;
    if (m + 1)
        .checked_mul(w)
        .map_or(true, |s| s > crate::float::VEC_MAX)
    {
        return bad;
    }
    let rhs = cols;
    let mut t = vec![0.0; (m + 1) * w];
    let mut basis = vec![0usize; m];
    let mut art = n + m;
    for i in 0..m {
        let neg = b[i] < 0.0;
        for j in 0..n {
            t[i * w + j] = if neg { -a[i * n + j] } else { a[i * n + j] };
        }
        t[i * w + n + i] = if neg { -1.0 } else { 1.0 };
        t[i * w + rhs] = if neg { -b[i] } else { b[i] };
        if neg {
            t[i * w + art] = 1.0;
            basis[i] = art;
            art += 1;
        } else {
            basis[i] = n + i;
        }
    }
    let obj = m * w;
    let pivot = |t: &mut Vec<f64>, r: usize, col: usize| {
        let p = t[r * w + col];
        for j in 0..w {
            t[r * w + j] = t[r * w + j] / p;
        }
        for i in 0..=m {
            if i == r {
                continue;
            }
            let f = t[i * w + col];
            if f != 0.0 {
                for j in 0..w {
                    let u = f * t[r * w + j];
                    t[i * w + j] = t[i * w + j] - u;
                }
            }
        }
    };
    // runs the simplex on the objective row over columns 0..limit; 0 optimal, 2 unbounded, 4 limit
    let run = |t: &mut Vec<f64>, basis: &mut Vec<usize>, limit: usize| -> u128 {
        for _ in 0..LP_ITERATIONS {
            let Some(col) = (0..limit).find(|j| t[obj + j] < -LP_EPS) else {
                return 0;
            };
            let mut row: Option<usize> = None;
            for i in 0..m {
                let v = t[i * w + col];
                if v > LP_EPS {
                    let ratio = t[i * w + rhs] / v;
                    row = match row {
                        None => Some(i),
                        Some(r) => {
                            let best = t[r * w + rhs] / t[r * w + col];
                            if ratio < best || (ratio == best && basis[i] < basis[r]) {
                                Some(i)
                            } else {
                                Some(r)
                            }
                        }
                    };
                }
            }
            let Some(r) = row else {
                return 2;
            };
            pivot(t, r, col);
            basis[r] = col;
        }
        4
    };
    if k > 0 {
        for j in n + m..cols {
            t[obj + j] = 1.0;
        }
        for i in 0..m {
            if basis[i] >= n + m {
                for j in 0..w {
                    let u = t[i * w + j];
                    t[obj + j] = t[obj + j] - u;
                }
            }
        }
        let s = run(&mut t, &mut basis, cols);
        if s == 4 {
            return (4, Vec::new());
        }
        if t[obj + rhs] < -LP_EPS {
            return (1, Vec::new());
        }
        for i in 0..m {
            if basis[i] >= n + m {
                if let Some(col) = (0..n + m).find(|j| t[i * w + j].abs() > LP_EPS) {
                    pivot(&mut t, i, col);
                    basis[i] = col;
                }
            }
        }
    }
    for j in 0..w {
        t[obj + j] = 0.0;
    }
    for j in 0..n {
        t[obj + j] = -c[j];
    }
    for i in 0..m {
        let f = t[obj + basis[i]];
        if f != 0.0 {
            for j in 0..w {
                let u = f * t[i * w + j];
                t[obj + j] = t[obj + j] - u;
            }
        }
    }
    let s = run(&mut t, &mut basis, n + m);
    if s != 0 && !(s == 2 && feasible_unbounded) {
        return (s, Vec::new());
    }
    let mut x = vec![0.0; n];
    for i in 0..m {
        if basis[i] < n {
            x[basis[i]] = t[i * w + rhs];
        }
    }
    (s, x)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fft_and_simplex() {
        let x = [1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0];
        let f = fft(&x, false).unwrap();
        assert_eq!(&f[..2], &[10.0, 0.0]);
        let back = fft(&f, true).unwrap();
        assert!(back.iter().zip(&x).all(|(a, b)| (a - b).abs() < 1e-15));
        let c = convolve(&[1.0, 2.0], &[3.0, 4.0, 5.0]).unwrap();
        assert!(c
            .iter()
            .zip(&[3.0, 10.0, 13.0, 10.0])
            .all(|(a, b)| (a - b).abs() < 1e-12));
        let (s, x) = simplex(
            &[3.0, 5.0],
            &[1.0, 0.0, 0.0, 2.0, 3.0, 2.0],
            &[4.0, 12.0, 18.0],
            3,
            2,
        );
        assert_eq!(s, 0);
        assert!((x[0] - 2.0).abs() < 1e-12 && (x[1] - 6.0).abs() < 1e-12);
        assert_eq!(simplex(&[1.0], &[1.0, -1.0], &[1.0, -2.0], 2, 1).0, 1);
        assert_eq!(simplex(&[1.0], &[-1.0], &[1.0], 1, 1).0, 2);
    }
}

pub const ILP_NODES: usize = 10_000;
const ILP_TOL: f64 = 1e-6;

/// Integer linear program: maximize `c·x`, `A x <= b`, `x >= 0`, `x_j` integral
/// where `integer[j] != 0`. Depth-first branch and bound on the simplex
/// relaxation (first fractional variable, floor branch first). Status as in
/// [`simplex`]; `4` also when the node limit is reached.
pub fn ilp(
    c: &[f64],
    a: &[f64],
    b: &[f64],
    m: u128,
    n: u128,
    integer: &[u128],
) -> (u128, Vec<f64>) {
    let (Ok(mm), Ok(nn)) = (usize::try_from(m), usize::try_from(n)) else {
        return (3, Vec::new());
    };
    if integer.len() != nn || c.len() != nn || b.len() != mm || mm.checked_mul(nn) != Some(a.len())
    {
        return (3, Vec::new());
    }
    // node = list of (var, upper?, bound)
    let mut stack: Vec<Vec<(usize, bool, f64)>> = vec![Vec::new()];
    let mut best: Option<(f64, Vec<f64>)> = None;
    let mut nodes = 0usize;
    let mut work = 20_000_000u64;
    while let Some(bounds) = stack.pop() {
        nodes += 1;
        if nodes > ILP_NODES {
            return (4, Vec::new());
        }
        let rows = mm + bounds.len();
        if rows > crate::float::VEC_MAX
            || rows
                .checked_mul(nn)
                .map_or(true, |v| v > crate::float::VEC_MAX)
        {
            return (3, Vec::new());
        }
        let cost = rows as u64 * (nn as u64 + 1);
        if work < cost {
            return (4, Vec::new());
        }
        work -= cost;
        let mut aa = a.to_vec();
        let mut bb = b.to_vec();
        for (j, upper, k) in &bounds {
            for col in 0..nn {
                aa.push(if col == *j {
                    if *upper {
                        1.0
                    } else {
                        -1.0
                    }
                } else {
                    0.0
                });
            }
            bb.push(if *upper { *k } else { -*k });
        }
        let (s, x) = simplex_inner(c, &aa, &bb, rows as u128, n, true);
        match s {
            0 | 2 => {}
            1 => continue,
            other => return (other, Vec::new()),
        }
        if x.iter().any(|v| !v.is_finite()) {
            return (4, Vec::new());
        }
        let mut value = 0.0;
        for j in 0..nn {
            let p = c[j] * x[j];
            value = value + p;
        }
        if !value.is_finite() {
            return (4, Vec::new());
        }
        if s == 0 {
            if let Some((bv, _)) = &best {
                if value <= *bv {
                    continue;
                }
            }
        }
        let rounded: Vec<f64> = (0..nn)
            .map(|j| {
                if integer[j] != 0 {
                    x[j].round() + 0.0
                } else {
                    x[j]
                }
            })
            .collect();
        let feasible = rounded.iter().all(|v| *v >= 0.0)
            && (0..rows).all(|i| {
                let mut sum = 0.0;
                for j in 0..nn {
                    let p = aa[i * nn + j] * rounded[j];
                    sum = sum + p;
                }
                sum.is_finite() && sum <= bb[i] + LP_EPS
            });
        let fractional = (0..nn).find(|j| {
            integer[*j] != 0
                && if feasible {
                    (x[*j] - rounded[*j]).abs() > ILP_TOL
                } else {
                    x[*j] != rounded[*j]
                }
        });
        match fractional {
            Some(j) => {
                let mut up = bounds.clone();
                up.push((j, false, x[j].ceil()));
                let mut down = bounds;
                down.push((j, true, x[j].floor()));
                stack.push(up);
                stack.push(down);
            }
            None => {
                if !feasible {
                    return (4, Vec::new());
                }
                if s == 2 {
                    return (2, Vec::new());
                }
                let xr = rounded;
                let mut v = 0.0;
                for j in 0..nn {
                    let p = c[j] * xr[j];
                    v = v + p;
                }
                if !v.is_finite() {
                    return (4, Vec::new());
                }
                best = Some((v, xr));
            }
        }
    }
    match best {
        Some((_, x)) => (0, x),
        None => (1, Vec::new()),
    }
}

// ---------------- v0.123: 2-D FFT, STFT, wavelets, IIR ----------------

/// 2-D transform of a row-major `rows × cols` interleaved complex array (rows, then columns).
pub fn fft2(x: &[f64], rows: u128, cols: u128, inverse: bool) -> Option<Vec<f64>> {
    let (r, c) = (usize::try_from(rows).ok()?, usize::try_from(cols).ok()?);
    if r.checked_mul(c)?.checked_mul(2)? != x.len() {
        return None;
    }
    if r > crate::float::VEC_MAX || c > crate::float::VEC_MAX { return None; }
    if x.is_empty() { return Some(Vec::new()); }
    let mut a = x.to_vec();
    for i in 0..r {
        let row = fft(&a[2 * i * c..2 * (i + 1) * c], inverse)?;
        a[2 * i * c..2 * (i + 1) * c].copy_from_slice(&row);
    }
    let mut col = vec![0.0; 2 * r];
    for j in 0..c {
        for i in 0..r {
            col[2 * i] = a[2 * (i * c + j)];
            col[2 * i + 1] = a[2 * (i * c + j) + 1];
        }
        let t = fft(&col, inverse)?;
        for i in 0..r {
            a[2 * (i * c + j)] = t[2 * i];
            a[2 * (i * c + j) + 1] = t[2 * i + 1];
        }
    }
    Some(a)
}

/// Power spectrogram: frames `1 + (n - w) / hop` (0 when `n < w`) × `w/2 + 1` bins of `|FFT(x·window)|²`.
pub fn stft(x: &[f64], window: &[f64], hop: u128) -> Option<Vec<f64>> {
    let w = window.len();
    if hop == 0 {
        return None;
    }
    let hop = usize::try_from(hop).unwrap_or(usize::MAX);
    if w == 0 {
        return None;
    }
    let frames = if x.len() < w {
        0
    } else {
        1 + (x.len() - w) / hop
    };
    let bins = w / 2 + 1;
    if frames.checked_mul(bins)? > crate::float::VEC_MAX {
        return None;
    }
    if frames == 0 { return Some(Vec::new()); }
    let mut out = Vec::with_capacity(frames * bins);
    let mut seg = vec![0.0; 2 * w];
    for f in 0..frames {
        for i in 0..w {
            seg[2 * i] = x[f * hop + i] * window[i];
            seg[2 * i + 1] = 0.0;
        }
        let s = fft(&seg, false)?;
        for k in 0..bins {
            let (re, im) = (s[2 * k], s[2 * k + 1]);
            out.push(re * re + im * im);
        }
    }
    Some(out)
}

const DB4: [f64; 4] = [
    0.48296291314453416,
    0.8365163037378079,
    0.2241438680420134,
    -0.12940952255126037,
];
const SQRT_HALF: f64 = 0.7071067811865476;

fn wavelet_ok(n: usize, levels: usize, kind: u128) -> bool {
    kind <= 1
        && levels < usize::BITS as usize && levels <= 60
        && n % (1usize << levels) == 0
        && (levels == 0 || (n >> (levels - 1)) >= if kind == 0 { 2 } else { 4 })
}

/// Periodic multi-level DWT (`0` Haar, `1` Daubechies-4): `[a_L, d_L, ..., d_1]`.
pub fn dwt(x: &[f64], levels: u128, kind: u128) -> Option<Vec<f64>> {
    let levels = usize::try_from(levels).ok()?;
    if !wavelet_ok(x.len(), levels, kind) {
        return None;
    }
    let mut a = x.to_vec();
    let mut len = x.len();
    for _ in 0..levels {
        let half = len / 2;
        let mut out = vec![0.0; len];
        for i in 0..half {
            if kind == 0 {
                let (p, q) = (a[2 * i], a[2 * i + 1]);
                out[i] = (p + q) * SQRT_HALF;
                out[half + i] = (p - q) * SQRT_HALF;
            } else {
                let (mut s, mut d) = (0.0, 0.0);
                for k in 0..4 {
                    let v = a[(2 * i + k) % len];
                    let g = if k % 2 == 0 { DB4[3 - k] } else { -DB4[3 - k] };
                    let t = DB4[k] * v;
                    s = s + t;
                    let u = g * v;
                    d = d + u;
                }
                out[i] = s;
                out[half + i] = d;
            }
        }
        a[..len].copy_from_slice(&out);
        len = half;
    }
    Some(a)
}

pub fn idwt(c: &[f64], levels: u128, kind: u128) -> Option<Vec<f64>> {
    let levels = usize::try_from(levels).ok()?;
    if !wavelet_ok(c.len(), levels, kind) {
        return None;
    }
    let mut a = c.to_vec();
    for l in (0..levels).rev() {
        let len = c.len() >> l;
        let half = len / 2;
        let mut out = vec![0.0; len];
        for i in 0..half {
            let (s, d) = (a[i], a[half + i]);
            if kind == 0 {
                out[2 * i] = (s + d) * SQRT_HALF;
                out[2 * i + 1] = (s - d) * SQRT_HALF;
            } else {
                for k in 0..4 {
                    let j = (2 * i + k) % len;
                    let g = if k % 2 == 0 { DB4[3 - k] } else { -DB4[3 - k] };
                    let t = DB4[k] * s;
                    let u = g * d;
                    out[j] = out[j] + (t + u);
                }
            }
        }
        a[..len].copy_from_slice(&out);
    }
    Some(a)
}

/// Direct-form IIR filter `y[n] = (Σ b_k x[n-k] - Σ_{k>=1} a_k y[n-k]) / a_0`.
pub fn iir(b: &[f64], a: &[f64], x: &[f64]) -> Option<Vec<f64>> {
    if a.is_empty() || a[0] == 0.0 || b.iter().chain(a).chain(x).any(|v| !v.is_finite()) {
        return None;
    }
    let mut y = vec![0.0; x.len()];
    for n in 0..x.len() {
        let mut s = 0.0;
        for k in 0..b.len().min(n + 1) {
            let t = b[k] * x[n - k];
            s = s + t;
        }
        for k in 1..a.len().min(n + 1) {
            let t = a[k] * y[n - k];
            s = s - t;
        }
        y[n] = s / a[0];
    }
    Some(y)
}

// ---------------- v0.123: SHA-256 ----------------

const K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

/// SHA-256 of bytes as 64 lowercase hex digits.
pub fn sha256(data: &[u8]) -> String {
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];
    let mut msg = data.to_vec();
    let bits = (data.len() as u64).wrapping_mul(8);
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bits.to_be_bytes());
    for block in msg.chunks(64) {
        let mut w = [0u32; 64];
        for t in 0..16 {
            w[t] = u32::from_be_bytes([
                block[4 * t],
                block[4 * t + 1],
                block[4 * t + 2],
                block[4 * t + 3],
            ]);
        }
        for t in 16..64 {
            let s0 = w[t - 15].rotate_right(7) ^ w[t - 15].rotate_right(18) ^ (w[t - 15] >> 3);
            let s1 = w[t - 2].rotate_right(17) ^ w[t - 2].rotate_right(19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16]
                .wrapping_add(s0)
                .wrapping_add(w[t - 7])
                .wrapping_add(s1);
        }
        let mut v = h;
        for t in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7]
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[t])
                .wrapping_add(w[t]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v = [
                t1.wrapping_add(t2),
                v[0],
                v[1],
                v[2],
                v[3].wrapping_add(t1),
                v[4],
                v[5],
                v[6],
            ];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(v[i]);
        }
    }
    h.iter().map(|x| format!("{x:08x}")).collect()
}
