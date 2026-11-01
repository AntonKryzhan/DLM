//! Exact rational matrices. Every arithmetic method shares a caller-owned budget.
use super::exact::{ExactBudget, Rational};
use super::{require, MathError, MathResult};
pub const MAX_AXIS: usize = 32;
pub const MAX_TEXT_BYTES: usize = 262144;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Matrix {
    rows: usize,
    cols: usize,
    data: Vec<Rational>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Solution {
    pub particular: Matrix,
    pub kernel: Matrix,
}
struct Reduction {
    data: Vec<Rational>,
    pivots: Vec<usize>,
    scales: Vec<Rational>,
    odd_swaps: bool,
}
fn reduce(
    rows: usize,
    cols: usize,
    pivot_limit: usize,
    mut data: Vec<Rational>,
    budget: &mut ExactBudget,
) -> MathResult<Reduction> {
    let mut pivots = Vec::new();
    let mut scales = Vec::new();
    let mut row = 0;
    let mut odd_swaps = false;
    for col in 0..pivot_limit {
        budget.charge(rows + 1)?;
        let Some(pivot) = (row..rows).find(|&r| !data[r * cols + col].is_zero()) else {
            continue;
        };
        if row != pivot {
            for c in 0..cols {
                data.swap(row * cols + c, pivot * cols + c);
            }
            odd_swaps = !odd_swaps;
        }
        let scale = data[row * cols + col].clone();
        let inverse = scale.reciprocal(budget)?;
        scales.push(scale);
        for c in 0..cols {
            data[row * cols + c] = data[row * cols + c].checked_mul(&inverse, budget)?;
        }
        for r in 0..rows {
            if r == row {
                continue;
            }
            let a = data[r * cols + col].clone();
            if a.is_zero() {
                continue;
            }
            for c in 0..cols {
                let product = a.checked_mul(&data[row * cols + c], budget)?;
                data[r * cols + c] = data[r * cols + c].checked_sub(&product, budget)?;
            }
        }
        pivots.push(col);
        row += 1;
    }
    Ok(Reduction {
        data,
        pivots,
        scales,
        odd_swaps,
    })
}
impl Matrix {
    pub fn new(
        rows: usize,
        cols: usize,
        data: Vec<Rational>,
        budget: &mut ExactBudget,
    ) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "rational matrix axis budget exceeded",
        )?;
        require(
            data.len() == rows * cols,
            "rational matrix storage does not match shape",
        )?;
        for value in &data {
            value.check_budget(budget)?;
        }
        budget.charge(data.len() + 1)?;
        Ok(Self { rows, cols, data })
    }
    pub fn zero(rows: usize, cols: usize, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "rational matrix axis budget exceeded",
        )?;
        Self::new(rows, cols, vec![Rational::zero(); rows * cols], budget)
    }
    pub fn identity(n: usize, budget: &mut ExactBudget) -> MathResult<Self> {
        let mut out = Self::zero(n, n, budget)?;
        for i in 0..n {
            out.data[i * n + i] = Rational::one();
        }
        Ok(out)
    }
    pub fn rows(&self) -> usize {
        self.rows
    }
    pub fn cols(&self) -> usize {
        self.cols
    }
    pub fn entries(&self) -> &[Rational] {
        &self.data
    }
    pub fn is_zero(&self) -> bool {
        self.data.iter().all(Rational::is_zero)
    }
    fn check(&self, budget: &mut ExactBudget) -> MathResult<()> {
        for a in &self.data {
            a.check_budget(budget)?;
        }
        budget.charge(self.data.len() + 1)
    }
    pub fn transpose(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        self.check(budget)?;
        let mut out = Self::zero(self.cols, self.rows, budget)?;
        for r in 0..self.rows {
            for c in 0..self.cols {
                out.data[c * self.rows + r] = self.data[r * self.cols + c].clone();
            }
        }
        Ok(out)
    }
    pub fn add(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            (self.rows, self.cols) == (b.rows, b.cols),
            "rational matrix addition shape mismatch",
        )?;
        self.check(budget)?;
        b.check(budget)?;
        let data = self
            .data
            .iter()
            .zip(&b.data)
            .map(|(a, b)| a.checked_add(b, budget))
            .collect::<MathResult<_>>()?;
        Self::new(self.rows, self.cols, data, budget)
    }
    pub fn mul(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            self.cols == b.rows,
            "rational matrix multiplication shape mismatch",
        )?;
        self.check(budget)?;
        b.check(budget)?;
        let mut out = Self::zero(self.rows, b.cols, budget)?;
        for r in 0..self.rows {
            for k in 0..self.cols {
                if self.data[r * self.cols + k].is_zero() {
                    continue;
                }
                for c in 0..b.cols {
                    let product = self.data[r * self.cols + k]
                        .checked_mul(&b.data[k * b.cols + c], budget)?;
                    out.data[r * b.cols + c] =
                        out.data[r * b.cols + c].checked_add(&product, budget)?;
                }
            }
        }
        Ok(out)
    }
    pub fn rref(&self, budget: &mut ExactBudget) -> MathResult<(Self, Vec<usize>)> {
        self.check(budget)?;
        let r = reduce(self.rows, self.cols, self.cols, self.data.clone(), budget)?;
        Ok((Self::new(self.rows, self.cols, r.data, budget)?, r.pivots))
    }
    pub fn rank(&self, budget: &mut ExactBudget) -> MathResult<usize> {
        Ok(self.rref(budget)?.1.len())
    }
    fn kernel_from(
        rows: usize,
        cols: usize,
        data: &[Rational],
        stride: usize,
        pivots: &[usize],
        budget: &mut ExactBudget,
    ) -> MathResult<Self> {
        let free: Vec<_> = (0..cols).filter(|c| !pivots.contains(c)).collect();
        let mut out = Self::zero(cols, free.len(), budget)?;
        for (j, &f) in free.iter().enumerate() {
            out.data[f * free.len() + j] = Rational::one();
            for (i, &p) in pivots.iter().enumerate() {
                debug_assert!(i < rows);
                out.data[p * free.len() + j] = data[i * stride + f].negated();
            }
        }
        Ok(out)
    }
    pub fn kernel(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        let (r, p) = self.rref(budget)?;
        Self::kernel_from(self.rows, self.cols, &r.data, self.cols, &p, budget)
    }
    pub fn select_columns(&self, columns: &[usize], budget: &mut ExactBudget) -> MathResult<Self> {
        self.check(budget)?;
        require(
            columns.len() <= MAX_AXIS && columns.iter().all(|&c| c < self.cols),
            "invalid rational column selection",
        )?;
        let mut data = Vec::with_capacity(self.rows * columns.len());
        for r in 0..self.rows {
            for &c in columns {
                data.push(self.data[r * self.cols + c].clone());
            }
        }
        Self::new(self.rows, columns.len(), data, budget)
    }
    pub fn image(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        let pivots = self.rref(budget)?.1;
        self.select_columns(&pivots, budget)
    }
    pub fn determinant(&self, budget: &mut ExactBudget) -> MathResult<Rational> {
        require(
            self.rows == self.cols,
            "determinant requires square rational matrix",
        )?;
        self.check(budget)?;
        let r = reduce(self.rows, self.cols, self.cols, self.data.clone(), budget)?;
        if r.pivots.len() != self.rows {
            return Ok(Rational::zero());
        }
        let mut det = Rational::one();
        for scale in r.scales {
            det = det.checked_mul(&scale, budget)?;
        }
        Ok(if r.odd_swaps { det.negated() } else { det })
    }
    fn augmented(&self, b: &Self) -> Vec<Rational> {
        let mut data = Vec::with_capacity(self.rows * (self.cols + b.cols));
        for r in 0..self.rows {
            data.extend_from_slice(&self.data[r * self.cols..(r + 1) * self.cols]);
            data.extend_from_slice(&b.data[r * b.cols..(r + 1) * b.cols]);
        }
        data
    }
    pub fn solve(&self, b: &Self, budget: &mut ExactBudget) -> MathResult<Solution> {
        require(
            self.rows == b.rows,
            "rational solve right-hand-side shape mismatch",
        )?;
        self.check(budget)?;
        b.check(budget)?;
        let stride = self.cols + b.cols;
        let r = reduce(self.rows, stride, self.cols, self.augmented(b), budget)?;
        for row in r.pivots.len()..self.rows {
            for c in self.cols..stride {
                require(
                    r.data[row * stride + c].is_zero(),
                    "inconsistent rational linear system",
                )?;
            }
        }
        let mut particular = Self::zero(self.cols, b.cols, budget)?;
        for (row, &col) in r.pivots.iter().enumerate() {
            for c in 0..b.cols {
                particular.data[col * b.cols + c] = r.data[row * stride + self.cols + c].clone();
            }
        }
        let kernel = Self::kernel_from(self.rows, self.cols, &r.data, stride, &r.pivots, budget)?;
        Ok(Solution { particular, kernel })
    }
    pub fn inverse(&self, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            self.rows == self.cols,
            "inverse requires square rational matrix",
        )?;
        let identity = Self::identity(self.rows, budget)?;
        let solution = self.solve(&identity, budget)?;
        require(solution.kernel.cols == 0, "singular rational matrix")?;
        Ok(solution.particular)
    }
    /// Header: rows cols; then exactly rows*cols signed integer/fraction tokens.
    pub fn parse(text: &str, budget: &mut ExactBudget) -> MathResult<Self> {
        require(
            text.len() <= MAX_TEXT_BYTES,
            "rational matrix text budget exceeded",
        )?;
        budget.charge(text.len() + 1)?;
        let mut lines = text
            .lines()
            .map(|s| s.split('#').next().unwrap().trim())
            .filter(|s| !s.is_empty());
        let header = lines
            .next()
            .ok_or_else(|| MathError("missing rational matrix header".into()))?;
        let shape: Vec<_> = header.split_whitespace().collect();
        require(
            shape.len() == 2,
            "rational matrix header requires rows cols",
        )?;
        let rows = shape[0]
            .parse::<usize>()
            .map_err(|_| MathError("invalid rational row count".into()))?;
        let cols = shape[1]
            .parse::<usize>()
            .map_err(|_| MathError("invalid rational column count".into()))?;
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "rational matrix axis budget exceeded",
        )?;
        let mut tokens = lines.flat_map(str::split_whitespace);
        let mut data = Vec::with_capacity(rows * cols);
        for _ in 0..rows * cols {
            let token = tokens
                .next()
                .ok_or_else(|| MathError("missing rational matrix entry".into()))?;
            data.push(Rational::parse(token, budget)?);
        }
        require(tokens.next().is_none(), "trailing rational matrix entries")?;
        Self::new(rows, cols, data, budget)
    }
}
impl std::fmt::Display for Matrix {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "{} {}", self.rows, self.cols)?;
        for r in 0..self.rows {
            for c in 0..self.cols {
                if c > 0 {
                    f.write_str(" ")?;
                }
                write!(f, "{}", self.data[r * self.cols + c])?;
            }
            writeln!(f)?;
        }
        Ok(())
    }
}
