//! v0.123: iterative sparse solvers on CSR matrices (`linalg::csr` format):
//! Jacobi-preconditioned conjugate gradients, BiCGSTAB and restarted GMRES
//! with Givens rotations. Each is a fixed sequence of IEEE operations
//! mirrored by the native `dvs_*`. `None` on malformed input, breakdown or no
//! convergence within the iteration budget.
use crate::linalg::{csr, dot, spmv};

fn finite(a: &[f64]) -> bool {
    a.iter().all(|x| x.is_finite())
}

fn setup(rowptr: &[u128], idx: &[u128], vals: &[f64], b: &[f64], tol: f64) -> Option<usize> {
    let n = csr(rowptr, idx, vals, b.len())?;
    (n == b.len() && finite(vals) && finite(b) && tol.is_finite() && tol >= 0.0).then_some(n)
}

pub(crate) fn norm(a: &[f64]) -> f64 {
    let square = dot(a, a);
    if square.is_finite() && (square > 0.0 || a.iter().all(|x| *x == 0.0)) { return square.sqrt(); }
    let scale = a.iter().fold(0.0f64, |s, x| s.max(x.abs()));
    if scale == 0.0 || !scale.is_finite() { return scale; }
    let sum = a.iter().fold(0.0, |s, x| { let y = x / scale; s + y * y });
    scale * sum.sqrt()
}

pub(crate) fn solution(rowptr: &[u128], idx: &[u128], vals: &[f64], b: &[f64], tol: f64, x: Vec<f64>) -> Option<Vec<f64>> {
    if !finite(&x) { return None; }
    let ax = spmv(rowptr, idx, vals, &x, b.len());
    let r: Vec<f64> = b.iter().zip(ax).map(|(b, a)| b - a).collect();
    (finite(&r) && norm(&r) <= tol).then_some(x)
}

/// Diagonal of a CSR matrix (duplicates summed in storage order).
pub fn diagonal(rowptr: &[u128], idx: &[u128], vals: &[f64], n: usize) -> Vec<f64> {
    let mut d = vec![0.0; n];
    for i in 0..n {
        for k in rowptr[i] as usize..rowptr[i + 1] as usize {
            if idx[k] as usize == i {
                d[i] = d[i] + vals[k];
            }
        }
    }
    d
}

pub fn pcg(
    rowptr: &[u128],
    idx: &[u128],
    vals: &[f64],
    b: &[f64],
    tol: f64,
    iterations: u128,
) -> Option<Vec<f64>> {
    let n = setup(rowptr, idx, vals, b, tol)?;
    let d = diagonal(rowptr, idx, vals, n);
    if d.iter().any(|x| !(*x > 0.0) || !x.is_finite()) {
        return None;
    }
    let mut x = vec![0.0; n];
    let mut r = b.to_vec();
    let mut z: Vec<f64> = (0..n).map(|i| r[i] / d[i]).collect();
    let mut p = z.clone();
    let mut rz = dot(&r, &z);
    let mut it = 0u128;
    loop {
        if !finite(&r) || !finite(&x) { return None; }
        let residual = norm(&r);
        if !residual.is_finite() { return None; }
        if residual <= tol { break; }
        if it == iterations {
            return None;
        }
        it += 1;
        let ap = spmv(rowptr, idx, vals, &p, n);
        let pap = dot(&p, &ap);
        if !(pap > 0.0) || !pap.is_finite() {
            return None;
        }
        let alpha = rz / pap;
        if !alpha.is_finite() { return None; }
        for i in 0..n {
            let t = alpha * p[i];
            x[i] = x[i] + t;
            let u = alpha * ap[i];
            r[i] = r[i] - u;
        }
        for i in 0..n {
            z[i] = r[i] / d[i];
        }
        let rz2 = dot(&r, &z);
        if finite(&r) && norm(&r) <= tol {
            return solution(rowptr, idx, vals, b, tol, x);
        }
        let beta = rz2 / rz;
        if !beta.is_finite() { return None; }
        for i in 0..n {
            let t = beta * p[i];
            p[i] = z[i] + t;
        }
        rz = rz2;
    }
    solution(rowptr, idx, vals, b, tol, x)
}

pub fn bicgstab(
    rowptr: &[u128],
    idx: &[u128],
    vals: &[f64],
    b: &[f64],
    tol: f64,
    iterations: u128,
) -> Option<Vec<f64>> {
    let n = setup(rowptr, idx, vals, b, tol)?;
    let mut x = vec![0.0; n];
    let mut r = b.to_vec();
    let rhat = r.clone();
    let (mut rho, mut alpha, mut omega) = (1.0f64, 1.0f64, 1.0f64);
    let mut v = vec![0.0; n];
    let mut p = vec![0.0; n];
    let mut it = 0u128;
    loop {
        if !finite(&r) || !finite(&x) { return None; }
        let residual = norm(&r);
        if !residual.is_finite() { return None; }
        if residual <= tol { break; }
        if it == iterations {
            return None;
        }
        it += 1;
        let rho1 = dot(&rhat, &r);
        if rho1 == 0.0 || !rho1.is_finite() {
            return None;
        }
        let beta = (rho1 / rho) * (alpha / omega);
        if !beta.is_finite() { return None; }
        for i in 0..n {
            let t = omega * v[i];
            let u = p[i] - t;
            let w = beta * u;
            p[i] = r[i] + w;
        }
        v = spmv(rowptr, idx, vals, &p, n);
        let den = dot(&rhat, &v);
        if den == 0.0 || !den.is_finite() {
            return None;
        }
        alpha = rho1 / den;
        if !alpha.is_finite() { return None; }
        let s: Vec<f64> = (0..n).map(|i| r[i] - alpha * v[i]).collect();
        if finite(&s) && norm(&s) <= tol {
            for i in 0..n {
                let t = alpha * p[i];
                x[i] = x[i] + t;
            }
            return solution(rowptr, idx, vals, b, tol, x);
        }
        let t = spmv(rowptr, idx, vals, &s, n);
        let tt = dot(&t, &t);
        if tt == 0.0 || !tt.is_finite() {
            return None;
        }
        omega = dot(&t, &s) / tt;
        for i in 0..n {
            let a = alpha * p[i];
            let c = omega * s[i];
            x[i] = x[i] + a + c;
            r[i] = s[i] - omega * t[i];
        }
        rho = rho1;
        if omega == 0.0 || !omega.is_finite() {
            return None;
        }
    }
    solution(rowptr, idx, vals, b, tol, x)
}

pub fn gmres(
    rowptr: &[u128],
    idx: &[u128],
    vals: &[f64],
    b: &[f64],
    tol: f64,
    restart: u128,
    iterations: u128,
) -> Option<Vec<f64>> {
    let n = setup(rowptr, idx, vals, b, tol)?;
    let m = usize::try_from(restart)
        .ok()
        .filter(|m| *m >= 1 && *m <= 1024)?;
    if (m + 1).checked_mul(n)?.checked_add((m + 1) * m)? > crate::float::VEC_MAX { return None; }
    let mut x = vec![0.0; n];
    let mut total = 0u128;
    loop {
        let ax = spmv(rowptr, idx, vals, &x, n);
        let r: Vec<f64> = (0..n).map(|i| b[i] - ax[i]).collect();
        if !finite(&r) || !finite(&x) { return None; }
        let beta = norm(&r);
        if !beta.is_finite() { return None; }
        if beta <= tol {
            return solution(rowptr, idx, vals, b, tol, x);
        }
        if total >= iterations {
            return None;
        }
        let mut basis: Vec<Vec<f64>> = vec![r.iter().map(|v| v / beta).collect()];
        let mut h = vec![vec![0.0f64; m]; m + 1];
        let (mut cs, mut sn) = (vec![0.0f64; m], vec![0.0f64; m]);
        let mut g = vec![0.0f64; m + 1];
        g[0] = beta;
        let mut k = 0;
        for j in 0..m {
            total += 1;
            let mut w = spmv(rowptr, idx, vals, &basis[j], n);
            for i in 0..=j {
                h[i][j] = dot(&w, &basis[i]);
                for l in 0..n {
                    let t = h[i][j] * basis[i][l];
                    w[l] = w[l] - t;
                }
            }
            if !finite(&w) { return None; }
            h[j + 1][j] = norm(&w);
            if !h[j + 1][j].is_finite() { return None; }
            let lucky = h[j + 1][j] == 0.0;
            if !lucky {
                let d = h[j + 1][j];
                basis.push(w.iter().map(|v| v / d).collect());
            }
            for i in 0..j {
                let (a, c) = (h[i][j], h[i + 1][j]);
                let temp = cs[i] * a + sn[i] * c;
                h[i + 1][j] = -sn[i] * a + cs[i] * c;
                h[i][j] = temp;
            }
            let (a, c) = (h[j][j], h[j + 1][j]);
            let denom = (a * a + c * c).sqrt();
            if denom == 0.0 || !denom.is_finite() {
                return None;
            }
            cs[j] = a / denom;
            sn[j] = c / denom;
            h[j][j] = denom;
            h[j + 1][j] = 0.0;
            g[j + 1] = -sn[j] * g[j];
            g[j] = cs[j] * g[j];
            k = j + 1;
            if g[j + 1].abs() <= tol || lucky || total >= iterations {
                break;
            }
        }
        let mut y = vec![0.0; k];
        for i in (0..k).rev() {
            let mut s = g[i];
            for l in i + 1..k {
                let t = h[i][l] * y[l];
                s = s - t;
            }
            y[i] = s / h[i][i];
        }
        for i in 0..k {
            for l in 0..n {
                let t = y[i] * basis[i][l];
                x[l] = x[l] + t;
            }
        }
    }
}
