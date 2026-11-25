use super::{finite, valid_tolerance};
use crate::{float, linalg};

#[derive(Debug)]
pub struct CertifiedSolution {
    pub values: Vec<f64>,
    /// Upper bound on the infinity norm of the error against the exact solution.
    pub error: f64,
    pub cholesky: bool,
}

fn add_down(a: f64, b: f64) -> f64 {
    if b == 0.0 {
        a
    } else if a == 0.0 {
        b
    } else if a == -b {
        0.0
    } else {
        (a + b).next_down()
    }
}

fn add_up(a: f64, b: f64) -> f64 {
    if b == 0.0 {
        a
    } else if a == 0.0 {
        b
    } else if a == -b {
        0.0
    } else {
        (a + b).next_up()
    }
}

#[derive(Clone, Copy)]
struct Interval {
    lo: f64,
    hi: f64,
}

impl Interval {
    fn exact(value: f64) -> Self {
        Self {
            lo: value,
            hi: value,
        }
    }

    fn product(a: f64, b: f64) -> Self {
        let value = a * b;
        if a == 0.0 || b == 0.0 || a.abs() == 1.0 || b.abs() == 1.0 {
            return Self::exact(value);
        }
        Self {
            lo: value.next_down(),
            hi: value.next_up(),
        }
    }

    fn add(self, rhs: Self) -> Self {
        if !self.lo.is_finite()
            || !self.hi.is_finite()
            || !rhs.lo.is_finite()
            || !rhs.hi.is_finite()
        {
            return Self {
                lo: f64::NEG_INFINITY,
                hi: f64::INFINITY,
            };
        }
        Self {
            lo: add_down(self.lo, rhs.lo),
            hi: add_up(self.hi, rhs.hi),
        }
    }

    fn magnitude(self) -> f64 {
        if self.lo.is_finite() && self.hi.is_finite() {
            self.lo.abs().max(self.hi.abs())
        } else {
            f64::INFINITY
        }
    }
}

fn residual(row: &[f64], values: &[f64], rhs: f64) -> f64 {
    row.iter()
        .zip(values)
        .fold(Interval::exact(-rhs), |sum, (&a, &x)| {
            sum.add(Interval::product(a, x))
        })
        .magnitude()
}

fn dominance(a: &[f64], n: usize) -> f64 {
    a.chunks_exact(n)
        .enumerate()
        .fold(f64::INFINITY, |lower, (i, row)| {
            let radius = row
                .iter()
                .enumerate()
                .filter(|(j, _)| *j != i)
                .fold(0.0, |sum, (_, x)| add_up(sum, x.abs()));
            lower.min(add_down(row[i].abs(), -radius))
        })
}

fn inverse_triangular_norm(l: &[f64], n: usize, transpose: bool) -> f64 {
    let mut bounds = vec![0.0; n];
    for i in 0..n {
        let row = if transpose { n - 1 - i } else { i };
        let mut upper = 1.0;
        for (j, bound) in bounds[..i].iter().enumerate() {
            let column = if transpose { n - 1 - j } else { j };
            let element = if transpose {
                l[column * n + row]
            } else {
                l[row * n + column]
            };
            if element != 0.0 {
                upper = add_up(upper, (element.abs() * bound).next_up());
            }
        }
        bounds[i] = (upper / l[row * n + row]).next_up();
    }
    bounds.into_iter().fold(0.0, f64::max)
}

fn factor_margin(a: &[f64], l: &[f64], n: usize) -> f64 {
    let upper =
        (inverse_triangular_norm(l, n, false) * inverse_triangular_norm(l, n, true)).next_up();
    if !upper.is_finite() || upper <= 0.0 {
        return 0.0;
    }
    let mut perturbation = vec![0.0; n];
    for i in 0..n {
        for j in 0..=i {
            let e = residual(
                &l[i * n..i * n + j + 1],
                &l[j * n..j * n + j + 1],
                a[i * n + j],
            );
            perturbation[i] = add_up(perturbation[i], e);
            if i != j {
                perturbation[j] = add_up(perturbation[j], e);
            }
        }
    }
    // ||(L Lᵀ)^-1||∞ <= upper. Symmetry gives the same upper bound in
    // the spectral norm, so a smaller perturbation preserves positivity.
    // The infinity-norm margin also lower-bounds ||A x||∞ / ||x||∞.
    let error = perturbation.into_iter().fold(0.0, f64::max);
    add_down((1.0 / upper).next_down(), -error)
}

fn triangular_solve(l: &[f64], b: &[f64], n: usize) -> Vec<f64> {
    let mut x = b.to_vec();
    for i in 0..n {
        let mut value = x[i];
        for j in 0..i {
            value -= l[i * n + j] * x[j];
        }
        x[i] = value / l[i * n + i];
    }
    for i in (0..n).rev() {
        let mut value = x[i];
        for j in i + 1..n {
            value -= l[j * n + i] * x[j];
        }
        x[i] = value / l[i * n + i];
    }
    x
}

fn certify(
    a: &[f64],
    b: &[f64],
    values: Vec<f64>,
    lower: f64,
    tolerance: f64,
    cholesky: bool,
) -> Option<CertifiedSolution> {
    if !lower.is_finite() || lower <= 0.0 || !finite(&values) {
        return None;
    }
    let r = a
        .chunks_exact(b.len())
        .zip(b)
        .map(|(row, &rhs)| residual(row, &values, rhs))
        .fold(0.0, f64::max);
    let error = if r == 0.0 { 0.0 } else { (r / lower).next_up() };
    (error <= tolerance).then_some(CertifiedSolution {
        values,
        error,
        cholesky,
    })
}

pub fn solve_certified(a: &[f64], b: &[f64], n: u128, tolerance: f64) -> Option<CertifiedSolution> {
    let n = usize::try_from(n).ok()?;
    if n.checked_mul(n)? != a.len()
        || b.len() != n
        || a.len() > linalg::MAX
        || !valid_tolerance(tolerance)
        || !finite(a)
        || !finite(b)
    {
        return None;
    }
    if n == 0 {
        return Some(CertifiedSolution {
            values: Vec::new(),
            error: 0.0,
            cholesky: false,
        });
    }
    let dominance = dominance(a, n);
    let symmetric =
        (0..n).all(|i| a[i * n + i] > 0.0 && (0..i).all(|j| a[i * n + j] == a[j * n + i]));
    let mut lower = dominance;
    if symmetric {
        if let Some(l) = linalg::cholesky(a, n as u128) {
            if lower <= 0.0 {
                lower = factor_margin(a, &l, n);
            }
            if lower > 0.0 {
                if let Some(solution) =
                    certify(a, b, triangular_solve(&l, b, n), lower, tolerance, true)
                {
                    return Some(solution);
                }
            }
        }
    }
    if lower <= 0.0 {
        return None;
    }
    certify(
        a,
        b,
        float::solve(a, b, n as u128)?,
        lower,
        tolerance,
        false,
    )
}
