//! v0.116: numerical linear algebra on row-major `FloatVec` matrices
//! (`la_*`): LU with partial pivoting, determinant, inverse, Cholesky,
//! Householder QR, least squares, symmetric eigen-decomposition (cyclic
//! Jacobi), singular values (one-sided Jacobi), rank, and CSR sparse
//! matrix–vector products with conjugate gradients. Every operation is a
//! fixed sequence of IEEE operations (no fused multiply-add) so the native
//! `dvl_*` in `backend_c_runtime.c` reproduces it bit for bit. Shape errors,
//! singular or non-finite inputs give `None`.
pub const MAX: usize = 1 << 24;
const SWEEPS: usize = 100;
const EPS: f64 = f64::EPSILON;

fn square(a: &[f64], n: u128) -> Option<usize> {
    let n = usize::try_from(n).ok()?;
    (n <= MAX && a.len() <= MAX && n.checked_mul(n)? == a.len() && finite(a)).then_some(n)
}

fn shape(a: &[f64], m: u128, n: u128) -> Option<(usize, usize)> {
    let (m, n) = (usize::try_from(m).ok()?, usize::try_from(n).ok()?);
    (m <= MAX && n <= MAX && a.len() <= MAX && m.checked_mul(n)? == a.len() && finite(a))
        .then_some((m, n))
}

fn finite(a: &[f64]) -> bool {
    a.iter().all(|x| x.is_finite())
}

fn sumsq(xs: impl Iterator<Item = f64>) -> f64 {
    let mut s = 0.0;
    for x in xs {
        let p = x * x;
        s = s + p;
    }
    s
}

// Scale only extreme inputs, preserving the operation sequence for ordinary values.
fn scale(a: &[f64]) -> f64 {
    let hi = a.iter().fold(0.0f64, |s, x| s.max(x.abs()));
    if hi != 0.0 && (hi > 1e50 || hi < 1e-50) {
        hi
    } else {
        1.0
    }
}

pub enum Lu {
    Factored(Vec<f64>, Vec<usize>, f64),
    Singular(f64),
}

/// Packed LU (unit L below the diagonal, U on and above), row permutation and its sign.
pub fn lu_factor(a: &[f64], n: usize) -> Lu {
    let mut m = a.to_vec();
    let mut perm: Vec<usize> = (0..n).collect();
    let mut sign = 1.0;
    for col in 0..n {
        let mut p = col;
        for r in col + 1..n {
            if m[r * n + col].abs() > m[p * n + col].abs() {
                p = r;
            }
        }
        let piv = m[p * n + col];
        if piv == 0.0 {
            return Lu::Singular(0.0);
        }
        if piv.is_nan() {
            return Lu::Singular(f64::NAN);
        }
        if p != col {
            for j in 0..n {
                m.swap(col * n + j, p * n + j);
            }
            perm.swap(col, p);
            sign = -sign;
        }
        for r in col + 1..n {
            let f = m[r * n + col] / m[col * n + col];
            m[r * n + col] = f;
            for j in col + 1..n {
                let t = f * m[col * n + j];
                m[r * n + j] = m[r * n + j] - t;
            }
        }
    }
    if finite(&m) {
        Lu::Factored(m, perm, sign)
    } else {
        Lu::Singular(f64::NAN)
    }
}

pub fn lu(a: &[f64], n: u128) -> Option<Vec<f64>> {
    let n = square(a, n)?;
    match lu_factor(a, n) {
        Lu::Factored(m, _, _) => Some(m),
        Lu::Singular(_) => None,
    }
}

pub fn lu_perm(a: &[f64], n: u128) -> Option<Vec<u128>> {
    let n = square(a, n)?;
    match lu_factor(a, n) {
        Lu::Factored(_, p, _) => Some(p.into_iter().map(|x| x as u128).collect()),
        Lu::Singular(_) => None,
    }
}

pub fn det(a: &[f64], n: u128) -> Option<f64> {
    let n = square(a, n)?;
    let d = match lu_factor(a, n) {
        Lu::Singular(v) => v,
        Lu::Factored(m, _, sign) => {
            let mut d = sign;
            for i in 0..n {
                d = d * m[i * n + i];
            }
            d
        }
    };
    d.is_finite().then_some(d)
}

pub fn inverse(a: &[f64], n: u128) -> Option<Vec<f64>> {
    let n = square(a, n)?;
    let Lu::Factored(m, perm, _) = lu_factor(a, n) else {
        return None;
    };
    let mut inv = vec![0.0; n * n];
    let mut x = vec![0.0; n];
    for j in 0..n {
        for i in 0..n {
            x[i] = if perm[i] == j { 1.0 } else { 0.0 };
        }
        for i in 0..n {
            let mut s = x[i];
            for k in 0..i {
                let t = m[i * n + k] * x[k];
                s = s - t;
            }
            x[i] = s;
        }
        for i in (0..n).rev() {
            let mut s = x[i];
            for k in i + 1..n {
                let t = m[i * n + k] * x[k];
                s = s - t;
            }
            x[i] = s / m[i * n + i];
        }
        for i in 0..n {
            inv[i * n + j] = x[i];
        }
    }
    finite(&inv).then_some(inv)
}

/// Lower-triangular `L` with `A = L·Lᵀ` (lower triangle of `A` is read); `None` unless positive definite.
pub fn cholesky(a: &[f64], n: u128) -> Option<Vec<f64>> {
    let n = square(a, n)?;
    let mut l = vec![0.0; n * n];
    for j in 0..n {
        let mut s = a[j * n + j];
        for k in 0..j {
            let t = l[j * n + k] * l[j * n + k];
            s = s - t;
        }
        if !(s > 0.0) || !s.is_finite() {
            return None;
        }
        let d = s.sqrt();
        l[j * n + j] = d;
        for i in j + 1..n {
            let mut s = a[i * n + j];
            for k in 0..j {
                let t = l[i * n + k] * l[j * n + k];
                s = s - t;
            }
            l[i * n + j] = s / d;
        }
    }
    finite(&l).then_some(l)
}

/// Thin Householder QR of an `m × n` matrix, `m >= n`: `(Q m×n, R n×n)`.
pub fn qr(a: &[f64], m: u128, n: u128) -> Option<(Vec<f64>, Vec<f64>)> {
    let (m, n) = shape(a, m, n)?;
    if m < n || !finite(a) {
        return None;
    }
    let scaling = scale(a);
    let mut r: Vec<f64> = a.iter().map(|x| x / scaling).collect();
    let mut vs: Vec<(Vec<f64>, f64)> = Vec::with_capacity(n);
    for k in 0..n {
        let norm = sumsq((k..m).map(|i| r[i * n + k])).sqrt();
        let mut v: Vec<f64> = (k..m).map(|i| r[i * n + k]).collect();
        let alpha = if v[0] > 0.0 { -norm } else { norm };
        v[0] = v[0] - alpha;
        let vn = sumsq(v.iter().copied());
        if vn != 0.0 {
            for j in k..n {
                let mut s = 0.0;
                for i in 0..v.len() {
                    let p = v[i] * r[(k + i) * n + j];
                    s = s + p;
                }
                let f = (2.0 * s) / vn;
                for i in 0..v.len() {
                    let t = f * v[i];
                    r[(k + i) * n + j] = r[(k + i) * n + j] - t;
                }
            }
        }
        vs.push((v, vn));
    }
    let mut q = vec![0.0; m * n];
    for j in 0..n {
        q[j * n + j] = 1.0;
    }
    for j in 0..n {
        for k in (0..n).rev() {
            let (v, vn) = &vs[k];
            if *vn == 0.0 {
                continue;
            }
            let mut s = 0.0;
            for i in 0..v.len() {
                let p = v[i] * q[(k + i) * n + j];
                s = s + p;
            }
            let f = (2.0 * s) / vn;
            for i in 0..v.len() {
                let t = f * v[i];
                q[(k + i) * n + j] = q[(k + i) * n + j] - t;
            }
        }
    }
    let mut rr = vec![0.0; n * n];
    for i in 0..n {
        for j in i..n {
            rr[i * n + j] = r[i * n + j] * scaling;
        }
    }
    (finite(&q) && finite(&rr)).then_some((q, rr))
}

/// Least-squares solution of `A x ≈ b` (`m × n`, `m >= n`, full column rank).
pub fn lstsq(a: &[f64], b: &[f64], m: u128, n: u128) -> Option<Vec<f64>> {
    let (q, r) = qr(a, m, n)?;
    let (m, n) = (m as usize, n as usize);
    if b.len() != m || !finite(b) {
        return None;
    }
    let mut c = vec![0.0; n];
    for j in 0..n {
        let mut s = 0.0;
        for i in 0..m {
            let p = q[i * n + j] * b[i];
            s = s + p;
        }
        c[j] = s;
    }
    for i in (0..n).rev() {
        if r[i * n + i] == 0.0 {
            return None;
        }
        let mut s = c[i];
        for k in i + 1..n {
            let t = r[i * n + k] * c[k];
            s = s - t;
        }
        c[i] = s / r[i * n + i];
    }
    finite(&c).then_some(c)
}

fn rotation(theta: f64) -> (f64, f64) {
    let sign = if theta >= 0.0 { 1.0 } else { -1.0 };
    let t = if theta.abs() > 1e150 {
        (0.5 / theta.abs()) * sign
    } else {
        sign / (theta.abs() + (theta * theta + 1.0).sqrt())
    };
    let c = 1.0 / (t * t + 1.0).sqrt();
    (c, t * c)
}

/// Eigenvalues ascending and the matching orthonormal eigenvectors (columns) of a symmetric matrix.
pub fn eigh(a: &[f64], n: u128) -> Option<(Vec<f64>, Vec<f64>)> {
    let n = square(a, n)?;
    if !finite(a) {
        return None;
    }
    for i in 0..n {
        for j in 0..i {
            if a[i * n + j] != a[j * n + i] {
                return None;
            }
        }
    }
    let scaling = scale(a);
    let mut m: Vec<f64> = a.iter().map(|x| x / scaling).collect();
    let mut v = vec![0.0; n * n];
    for i in 0..n {
        v[i * n + i] = 1.0;
    }
    let total = sumsq(m.iter().copied());
    for _ in 0..SWEEPS {
        let mut off = 0.0;
        for p in 0..n {
            for q in p + 1..n {
                let x = m[p * n + q] * m[p * n + q];
                off = off + x;
            }
        }
        if off <= EPS * EPS * total {
            break;
        }
        for p in 0..n {
            for q in p + 1..n {
                let apq = m[p * n + q];
                if apq == 0.0 {
                    continue;
                }
                let theta = (m[q * n + q] - m[p * n + p]) / (2.0 * apq);
                let (c, s) = rotation(theta);
                for k in 0..n {
                    let (x, y) = (m[k * n + p], m[k * n + q]);
                    m[k * n + p] = c * x - s * y;
                    m[k * n + q] = s * x + c * y;
                }
                for k in 0..n {
                    let (x, y) = (m[p * n + k], m[q * n + k]);
                    m[p * n + k] = c * x - s * y;
                    m[q * n + k] = s * x + c * y;
                }
                for k in 0..n {
                    let (x, y) = (v[k * n + p], v[k * n + q]);
                    v[k * n + p] = c * x - s * y;
                    v[k * n + q] = s * x + c * y;
                }
            }
        }
    }
    let mut off = 0.0;
    for p in 0..n {
        for q in p + 1..n {
            off += m[p * n + q] * m[p * n + q];
        }
    }
    if !finite(&m) || !finite(&v) || !off.is_finite() || off > EPS * EPS * total {
        return None;
    }
    let mut order: Vec<usize> = (0..n).collect();
    order.sort_by(|x, y| {
        m[x * n + x]
            .partial_cmp(&m[y * n + y])
            .unwrap()
            .then(x.cmp(y))
    });
    let values: Vec<f64> = order.iter().map(|i| m[i * n + i] * scaling).collect();
    if !finite(&values) {
        return None;
    }
    let mut vectors = vec![0.0; n * n];
    for (j, src) in order.iter().enumerate() {
        for k in 0..n {
            vectors[k * n + j] = v[k * n + src];
        }
    }
    Some((values, vectors))
}

/// The min(m, n) singular values in decreasing order (one-sided Jacobi).
pub fn svd_values(a: &[f64], m: u128, n: u128) -> Option<Vec<f64>> {
    let (m, n) = shape(a, m, n)?;
    if !finite(a) {
        return None;
    }
    if m < n {
        let mut at = vec![0.0; a.len()];
        for i in 0..m {
            for j in 0..n {
                at[j * m + i] = a[i * n + j];
            }
        }
        return svd_values(&at, n as u128, m as u128);
    }
    let scaling = scale(a);
    let mut u: Vec<f64> = a.iter().map(|x| x / scaling).collect();
    let mut converged = false;
    for _ in 0..SWEEPS {
        let mut rotated = false;
        for p in 0..n {
            for q in p + 1..n {
                let (mut alpha, mut beta, mut gamma) = (0.0, 0.0, 0.0);
                for i in 0..m {
                    let (x, y) = (u[i * n + p], u[i * n + q]);
                    let (xx, yy, xy) = (x * x, y * y, x * y);
                    alpha = alpha + xx;
                    beta = beta + yy;
                    gamma = gamma + xy;
                }
                if alpha == 0.0
                    || beta == 0.0
                    || gamma == 0.0
                    || gamma.abs() <= EPS * (alpha * beta).sqrt()
                {
                    continue;
                }
                rotated = true;
                let (c, s) = rotation((beta - alpha) / (2.0 * gamma));
                for i in 0..m {
                    let (x, y) = (u[i * n + p], u[i * n + q]);
                    u[i * n + p] = c * x - s * y;
                    u[i * n + q] = s * x + c * y;
                }
            }
        }
        if !rotated {
            converged = true;
            break;
        }
    }
    if !converged || !finite(&u) {
        return None;
    }
    let mut sv: Vec<f64> = (0..n)
        .map(|j| sumsq((0..m).map(|i| u[i * n + j])).sqrt() * scaling)
        .collect();
    if !finite(&sv) {
        return None;
    }
    sv.sort_by(|x, y| y.partial_cmp(x).unwrap());
    Some(sv)
}

pub fn rank(a: &[f64], m: u128, n: u128, tol: f64) -> Option<u128> {
    if !tol.is_finite() || tol < 0.0 {
        return None;
    }
    Some(
        svd_values(a, m, n)?
            .into_iter()
            .filter(|s| *s > tol)
            .count() as u128,
    )
}

pub fn identity(n: u128) -> Option<Vec<f64>> {
    let n = usize::try_from(n).ok()?;
    if n.checked_mul(n)? > MAX {
        return None;
    }
    let mut a = vec![0.0; n * n];
    for i in 0..n {
        a[i * n + i] = 1.0;
    }
    Some(a)
}

pub fn trace(a: &[f64], n: u128) -> Option<f64> {
    let n = square(a, n)?;
    let mut s = 0.0;
    for i in 0..n {
        s = s + a[i * n + i];
    }
    Some(s)
}

/// CSR shape check: `rowptr[0] = 0`, non-decreasing, `rowptr[r] = nnz`, columns `< cols`.
pub(crate) fn csr(rowptr: &[u128], idx: &[u128], vals: &[f64], cols: usize) -> Option<usize> {
    let r = rowptr.len().checked_sub(1)?;
    if rowptr[0] != 0 || rowptr[r] != idx.len() as u128 || idx.len() != vals.len() {
        return None;
    }
    if rowptr.windows(2).any(|w| w[0] > w[1]) || idx.iter().any(|c| *c >= cols as u128) {
        return None;
    }
    Some(r)
}

pub(crate) fn spmv(rowptr: &[u128], idx: &[u128], vals: &[f64], x: &[f64], r: usize) -> Vec<f64> {
    let mut y = vec![0.0; r];
    for i in 0..r {
        let mut s = 0.0;
        for k in rowptr[i] as usize..rowptr[i + 1] as usize {
            let p = vals[k] * x[idx[k] as usize];
            s = s + p;
        }
        y[i] = s;
    }
    y
}

pub fn csr_matvec(rowptr: &[u128], idx: &[u128], vals: &[f64], x: &[f64]) -> Option<Vec<f64>> {
    let r = csr(rowptr, idx, vals, x.len())?;
    Some(spmv(rowptr, idx, vals, x, r))
}

pub(crate) fn dot(a: &[f64], b: &[f64]) -> f64 {
    let mut s = 0.0;
    for i in 0..a.len() {
        let p = a[i] * b[i];
        s = s + p;
    }
    s
}

/// Conjugate gradients for a symmetric positive definite CSR matrix; `None` without convergence.
pub fn cg(
    rowptr: &[u128],
    idx: &[u128],
    vals: &[f64],
    b: &[f64],
    tol: f64,
    iterations: u128,
) -> Option<Vec<f64>> {
    let n = csr(rowptr, idx, vals, b.len())?;
    if n != b.len() || !finite(vals) || !finite(b) || !tol.is_finite() || tol < 0.0 {
        return None;
    }
    let mut x = vec![0.0; n];
    let mut r = b.to_vec();
    let mut p = r.clone();
    let mut rs = dot(&r, &r);
    if !rs.is_finite() {
        return None;
    }
    let mut it = 0u128;
    while crate::sparse::norm(&r) > tol {
        if it == iterations {
            return None;
        }
        it += 1;
        let ap = spmv(rowptr, idx, vals, &p, n);
        let pap = dot(&p, &ap);
        if !(pap > 0.0) || !pap.is_finite() {
            return None;
        }
        let alpha = rs / pap;
        if !alpha.is_finite() {
            return None;
        }
        for i in 0..n {
            let t = alpha * p[i];
            x[i] = x[i] + t;
            let u = alpha * ap[i];
            r[i] = r[i] - u;
        }
        let rs2 = dot(&r, &r);
        if !rs2.is_finite() || !finite(&x) {
            return None;
        }
        if crate::sparse::norm(&r) <= tol {
            return crate::sparse::solution(rowptr, idx, vals, b, tol, x);
        }
        let beta = rs2 / rs;
        if !beta.is_finite() {
            return None;
        }
        for i in 0..n {
            let t = beta * p[i];
            p[i] = r[i] + t;
        }
        rs = rs2;
    }
    crate::sparse::solution(rowptr, idx, vals, b, tol, x)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn small_matrices() {
        let a = [4.0, 3.0, 6.0, 3.0];
        assert_eq!(det(&a, 2), Some(-6.0));
        assert_eq!(
            inverse(&a, 2).unwrap(),
            vec![-0.5, 0.5, 1.0, -0.6666666666666666]
        );
        assert_eq!(
            cholesky(&[4.0, 2.0, 2.0, 3.0], 2).unwrap(),
            vec![2.0, 0.0, 1.0, 2.0f64.sqrt()]
        );
        let (v, _) = eigh(&[2.0, 1.0, 1.0, 2.0], 2).unwrap();
        assert!((v[0] - 1.0).abs() < 1e-15 && (v[1] - 3.0).abs() < 1e-15);
        let s = svd_values(&[3.0, 0.0, 0.0, 4.0], 2, 2).unwrap();
        assert_eq!(s, vec![4.0, 3.0]);
    }
}
