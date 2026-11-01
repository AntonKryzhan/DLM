//! Exact dense matrices over a validated prime field, including empty shapes.
use super::{require, MathResult};
pub const MAX_AXIS: usize = 256;
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct PrimeField {
    modulus: u8,
}
impl PrimeField {
    pub fn new(p: u16) -> MathResult<Self> {
        require(
            (2..=251).contains(&p) && (2..p).all(|d| !p.is_multiple_of(d)),
            "unsupported prime modulus (expected prime 2..251)",
        )?;
        Ok(Self { modulus: p as u8 })
    }
    pub fn modulus(self) -> u8 {
        self.modulus
    }
    pub fn element(self, a: u16) -> MathResult<u8> {
        require(a < self.modulus as u16, "field element out of range")?;
        Ok(a as u8)
    }
    pub fn reduce_integer(self, a: i128) -> u8 {
        a.rem_euclid(self.modulus as i128) as u8
    }
    fn plus(self, a: u8, b: u8) -> u8 {
        ((a as u16 + b as u16) % self.modulus as u16) as u8
    }
    fn minus(self, a: u8, b: u8) -> u8 {
        ((a as u16 + self.modulus as u16 - b as u16) % self.modulus as u16) as u8
    }
    fn times(self, a: u8, b: u8) -> u8 {
        ((a as u16 * b as u16) % self.modulus as u16) as u8
    }
    fn neg(self, a: u8) -> u8 {
        self.minus(0, a)
    }
    fn inv(self, a: u8) -> u8 {
        (1..self.modulus)
            .find(|&b| self.times(a, b) == 1)
            .expect("nonzero prime-field element")
    }
    fn pair(self, a: u8, b: u8) -> MathResult<()> {
        require(
            a < self.modulus && b < self.modulus,
            "field element out of range",
        )
    }
    pub fn add(self, a: u8, b: u8) -> MathResult<u8> {
        self.pair(a, b)?;
        Ok(self.plus(a, b))
    }
    pub fn subtract(self, a: u8, b: u8) -> MathResult<u8> {
        self.pair(a, b)?;
        Ok(self.minus(a, b))
    }
    pub fn multiply(self, a: u8, b: u8) -> MathResult<u8> {
        self.pair(a, b)?;
        Ok(self.times(a, b))
    }
    pub fn inverse(self, a: u8) -> MathResult<u8> {
        self.pair(a, 0)?;
        require(a != 0, "division by zero in prime field")?;
        Ok(self.inv(a))
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Matrix {
    field: PrimeField,
    rows: usize,
    cols: usize,
    data: Vec<u8>,
}
/// Private augmented reduction admits up to 512 columns; public matrices stay <=256.
fn reduce_data(
    f: PrimeField,
    rows: usize,
    cols: usize,
    mut data: Vec<u8>,
) -> (Vec<u8>, Vec<usize>) {
    let mut pivots = Vec::new();
    let mut row = 0;
    for col in 0..cols {
        let Some(pivot) = (row..rows).find(|&r| data[r * cols + col] != 0) else {
            continue;
        };
        for c in 0..cols {
            data.swap(row * cols + c, pivot * cols + c);
        }
        let inverse = f.inv(data[row * cols + col]);
        for c in 0..cols {
            data[row * cols + c] = f.times(data[row * cols + c], inverse);
        }
        for r in 0..rows {
            if r == row {
                continue;
            }
            let scale = data[r * cols + col];
            if scale == 0 {
                continue;
            }
            for c in 0..cols {
                data[r * cols + c] =
                    f.minus(data[r * cols + c], f.times(scale, data[row * cols + c]));
            }
        }
        pivots.push(col);
        row += 1;
    }
    (data, pivots)
}
impl Matrix {
    pub fn new(field: PrimeField, rows: usize, cols: usize, data: Vec<u8>) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        require(
            data.len() == rows * cols,
            "matrix storage does not match shape",
        )?;
        require(
            data.iter().all(|&x| x < field.modulus),
            "field element out of range",
        )?;
        Ok(Self {
            field,
            rows,
            cols,
            data,
        })
    }
    pub fn zero(field: PrimeField, rows: usize, cols: usize) -> MathResult<Self> {
        require(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        Self::new(field, rows, cols, vec![0; rows * cols])
    }
    pub fn identity(field: PrimeField, n: usize) -> MathResult<Self> {
        let mut m = Self::zero(field, n, n)?;
        for i in 0..n {
            m.data[i * n + i] = 1;
        }
        Ok(m)
    }
    pub fn field(&self) -> PrimeField {
        self.field
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
    fn same_field(&self, rhs: &Self) -> MathResult<()> {
        require(self.field == rhs.field, "coefficient field mismatch")
    }
    pub fn transpose(&self) -> Self {
        let mut out = Self::zero(self.field, self.cols, self.rows).unwrap();
        for r in 0..self.rows {
            for c in 0..self.cols {
                out.data[c * self.rows + r] = self.data[r * self.cols + c];
            }
        }
        out
    }
    pub fn add(&self, rhs: &Self) -> MathResult<Self> {
        self.same_field(rhs)?;
        require(
            (self.rows, self.cols) == (rhs.rows, rhs.cols),
            "addition shape mismatch",
        )?;
        Self::new(
            self.field,
            self.rows,
            self.cols,
            self.data
                .iter()
                .zip(&rhs.data)
                .map(|(&a, &b)| self.field.plus(a, b))
                .collect(),
        )
    }
    pub fn mul(&self, rhs: &Self) -> MathResult<Self> {
        self.same_field(rhs)?;
        require(self.cols == rhs.rows, "multiplication shape mismatch")?;
        let mut out = Self::zero(self.field, self.rows, rhs.cols)?;
        for r in 0..self.rows {
            for k in 0..self.cols {
                let a = self.data[r * self.cols + k];
                if a == 0 {
                    continue;
                }
                for c in 0..rhs.cols {
                    let i = r * rhs.cols + c;
                    out.data[i] = self
                        .field
                        .plus(out.data[i], self.field.times(a, rhs.data[k * rhs.cols + c]));
                }
            }
        }
        Ok(out)
    }
    pub fn rref(&self) -> (Self, Vec<usize>) {
        let (data, pivots) = reduce_data(self.field, self.rows, self.cols, self.data.clone());
        (
            Self {
                data,
                ..self.clone()
            },
            pivots,
        )
    }
    pub fn rank(&self) -> usize {
        self.rref().1.len()
    }
    pub fn kernel(&self) -> Self {
        let (r, pivots) = self.rref();
        let free: Vec<_> = (0..self.cols).filter(|c| !pivots.contains(c)).collect();
        let mut out = Self::zero(self.field, self.cols, free.len()).unwrap();
        for (j, &f) in free.iter().enumerate() {
            out.data[f * free.len() + j] = 1;
            for (i, &p) in pivots.iter().enumerate() {
                out.data[p * free.len() + j] = self.field.neg(r.data[i * self.cols + f]);
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
        Self::new(self.field, self.rows, columns.len(), data)
    }
    pub fn hstack(&self, rhs: &Self) -> MathResult<Self> {
        self.same_field(rhs)?;
        require(self.rows == rhs.rows, "column concatenation shape mismatch")?;
        require(
            self.cols + rhs.cols <= MAX_AXIS,
            "matrix axis budget exceeded",
        )?;
        Self::new(
            self.field,
            self.rows,
            self.cols + rhs.cols,
            self.augmented(rhs),
        )
    }
    fn augmented(&self, rhs: &Self) -> Vec<u8> {
        let mut data = Vec::with_capacity(self.rows * (self.cols + rhs.cols));
        for r in 0..self.rows {
            data.extend_from_slice(&self.data[r * self.cols..(r + 1) * self.cols]);
            data.extend_from_slice(&rhs.data[r * rhs.cols..(r + 1) * rhs.cols]);
        }
        data
    }
    /// Original candidate columns that extend the span of the base columns.
    pub fn extend_basis(&self, candidates: &Self) -> MathResult<Self> {
        self.same_field(candidates)?;
        require(
            self.rows == candidates.rows,
            "basis extension shape mismatch",
        )?;
        let (_, pivots) = reduce_data(
            self.field,
            self.rows,
            self.cols + candidates.cols,
            self.augmented(candidates),
        );
        let selected: Vec<_> = pivots
            .into_iter()
            .filter(|&c| c >= self.cols)
            .map(|c| c - self.cols)
            .collect();
        candidates.select_columns(&selected)
    }
    pub fn tensor(&self, rhs: &Self) -> MathResult<Self> {
        self.same_field(rhs)?;
        let rows = self.rows * rhs.rows;
        let cols = self.cols * rhs.cols;
        let mut out = Self::zero(self.field, rows, cols)?;
        for i in 0..self.rows {
            for j in 0..self.cols {
                for r in 0..rhs.rows {
                    for c in 0..rhs.cols {
                        out.data[(i * rhs.rows + r) * cols + j * rhs.cols + c] = self
                            .field
                            .times(self.data[i * self.cols + j], rhs.data[r * rhs.cols + c]);
                    }
                }
            }
        }
        Ok(out)
    }
}
