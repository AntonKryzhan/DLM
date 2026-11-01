//! Dense F2 matrices, including zero-dimensional shapes. Storage is private:
//! construction validates dimensions and entries. All algorithms are exact.
use super::{require, MathResult};
pub const MAX_AXIS: usize = 256;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Matrix {
    rows: usize,
    cols: usize,
    data: Vec<u8>,
}
impl Matrix {
    pub fn new(rows: usize, cols: usize, data: Vec<u8>) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        require(
            data.len() == rows * cols,
            "matrix storage does not match shape",
        )?;
        require(data.iter().all(|&x| x <= 1), "F2 entries must be 0 or 1")?;
        Ok(Self { rows, cols, data })
    }
    pub fn zero(rows: usize, cols: usize) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        Self::new(rows, cols, vec![0; rows * cols])
    }
    pub fn identity(n: usize) -> MathResult<Self> {
        let mut m = Self::zero(n, n)?;
        for i in 0..n {
            m.data[i * n + i] = 1;
        }
        Ok(m)
    }
    pub fn rows(&self) -> usize {
        self.rows
    }
    pub fn cols(&self) -> usize {
        self.cols
    }
    pub fn entries(&self) -> &[u8] {
        &self.data
    }
    pub fn is_zero(&self) -> bool {
        self.data.iter().all(|&x| x == 0)
    }
    pub fn transpose(&self) -> Self {
        let mut out = Self::zero(self.cols, self.rows).unwrap();
        for r in 0..self.rows {
            for c in 0..self.cols {
                out.data[c * self.rows + r] = self.data[r * self.cols + c];
            }
        }
        out
    }
    pub fn add(&self, rhs: &Self) -> MathResult<Self> {
        require(
            (self.rows, self.cols) == (rhs.rows, rhs.cols),
            "addition shape mismatch",
        )?;
        Self::new(
            self.rows,
            self.cols,
            self.data
                .iter()
                .zip(&rhs.data)
                .map(|(a, b)| a ^ b)
                .collect(),
        )
    }
    pub fn mul(&self, rhs: &Self) -> MathResult<Self> {
        require(self.cols == rhs.rows, "multiplication shape mismatch")?;
        let mut out = Self::zero(self.rows, rhs.cols)?;
        for r in 0..self.rows {
            for k in 0..self.cols {
                if self.data[r * self.cols + k] == 1 {
                    for c in 0..rhs.cols {
                        out.data[r * rhs.cols + c] ^= rhs.data[k * rhs.cols + c];
                    }
                }
            }
        }
        Ok(out)
    }
    /// Reduced row echelon form and ascending pivot columns.
    pub fn rref(&self) -> (Self, Vec<usize>) {
        let mut m = self.clone();
        let mut pivots = Vec::new();
        let mut row = 0;
        for col in 0..m.cols {
            let Some(pivot) = (row..m.rows).find(|&r| m.data[r * m.cols + col] == 1) else {
                continue;
            };
            for c in 0..m.cols {
                m.data.swap(row * m.cols + c, pivot * m.cols + c);
            }
            for r in 0..m.rows {
                if r != row && m.data[r * m.cols + col] == 1 {
                    for c in 0..m.cols {
                        m.data[r * m.cols + c] ^= m.data[row * m.cols + c];
                    }
                }
            }
            pivots.push(col);
            row += 1;
        }
        (m, pivots)
    }
    pub fn rank(&self) -> usize {
        self.rref().1.len()
    }
    /// Columns form a basis for the nullspace, with deterministic free-variable order.
    pub fn kernel(&self) -> Self {
        let (r, pivots) = self.rref();
        let free: Vec<_> = (0..self.cols).filter(|c| !pivots.contains(c)).collect();
        let mut out = Self::zero(self.cols, free.len()).unwrap();
        for (j, &f) in free.iter().enumerate() {
            out.data[f * free.len() + j] = 1;
            for (i, &p) in pivots.iter().enumerate() {
                out.data[p * free.len() + j] = r.data[i * self.cols + f];
            }
        }
        out
    }
    pub fn image(&self) -> Self {
        self.select_columns(&self.rref().1).unwrap()
    }
    pub fn select_columns(&self, columns: &[usize]) -> MathResult<Self> {
        require(
            columns.len() <= MAX_AXIS && columns.iter().all(|&c| c < self.cols),
            "invalid column selection",
        )?;
        let mut data = Vec::with_capacity(self.rows * columns.len());
        for r in 0..self.rows {
            for &c in columns {
                data.push(self.data[r * self.cols + c]);
            }
        }
        Self::new(self.rows, columns.len(), data)
    }
    pub fn hstack(&self, rhs: &Self) -> MathResult<Self> {
        require(self.rows == rhs.rows, "column concatenation shape mismatch")?;
        require(
            self.cols + rhs.cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        let mut data = Vec::new();
        for r in 0..self.rows {
            data.extend_from_slice(&self.data[r * self.cols..(r + 1) * self.cols]);
            data.extend_from_slice(&rhs.data[r * rhs.cols..(r + 1) * rhs.cols]);
        }
        Self::new(self.rows, self.cols + rhs.cols, data)
    }
    pub fn tensor(&self, rhs: &Self) -> MathResult<Self> {
        let rows = self
            .rows
            .checked_mul(rhs.rows)
            .ok_or_else(|| super::MathError("tensor shape overflow".into()))?;
        let cols = self
            .cols
            .checked_mul(rhs.cols)
            .ok_or_else(|| super::MathError("tensor shape overflow".into()))?;
        let mut out = Self::zero(rows, cols)?;
        for i in 0..self.rows {
            for j in 0..self.cols {
                for r in 0..rhs.rows {
                    for c in 0..rhs.cols {
                        out.data[(i * rhs.rows + r) * cols + j * rhs.cols + c] =
                            self.data[i * self.cols + j] & rhs.data[r * rhs.cols + c];
                    }
                }
            }
        }
        Ok(out)
    }
}
