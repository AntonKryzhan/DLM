//! Oriented, unaugmented finite chain complexes over a specified prime field.
//! Runtime computations, not StaticProof or independent certificate checking.
use super::prime::{Matrix, PrimeField, MAX_AXIS};
use super::topology::{SimplicialComplex, MAX_DEGREES};
use super::{require, MathError, MathResult};
#[derive(Clone, Debug)]
pub struct ChainComplex {
    field: PrimeField,
    dimensions: Vec<usize>,
    boundaries: Vec<Matrix>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Homology {
    pub degree: usize,
    pub betti: usize,
    pub representatives: Matrix,
}
impl ChainComplex {
    pub fn new(
        field: PrimeField,
        dimensions: Vec<usize>,
        boundaries: Vec<Matrix>,
    ) -> MathResult<Self> {
        require(
            dimensions.len() <= MAX_DEGREES,
            "chain degree budget exceeded",
        )?;
        require(
            dimensions.iter().all(|&d| d <= MAX_AXIS),
            "chain dimension budget exceeded",
        )?;
        require(
            dimensions.len() == boundaries.len(),
            "one boundary per degree required",
        )?;
        for (n, d) in boundaries.iter().enumerate() {
            require(d.field() == field, "coefficient field mismatch")?;
            require(
                d.cols() == dimensions[n] && d.rows() == if n == 0 { 0 } else { dimensions[n - 1] },
                "boundary shape mismatch",
            )?;
            if n > 0 {
                require(
                    boundaries[n - 1].mul(d)?.is_zero(),
                    "boundary squared is nonzero",
                )?;
            }
        }
        Ok(Self {
            field,
            dimensions,
            boundaries,
        })
    }
    pub fn field(&self) -> PrimeField {
        self.field
    }
    pub fn dimensions(&self) -> &[usize] {
        &self.dimensions
    }
    pub fn boundaries(&self) -> &[Matrix] {
        &self.boundaries
    }
    pub fn homology(&self, n: usize) -> MathResult<Homology> {
        require(n < self.dimensions.len(), "homology degree out of range")?;
        let cycles = self.boundaries[n].kernel();
        let image = if n + 1 < self.dimensions.len() {
            self.boundaries[n + 1].image()
        } else {
            Matrix::zero(self.field, self.dimensions[n], 0)?
        };
        let representatives = image.extend_basis(&cycles)?;
        Ok(Homology {
            degree: n,
            betti: representatives.cols(),
            representatives,
        })
    }
    pub fn cohomology(&self, n: usize) -> MathResult<Homology> {
        require(n < self.dimensions.len(), "cohomology degree out of range")?;
        let mut dimensions = self.dimensions.clone();
        dimensions.reverse();
        let mut boundaries = vec![Matrix::zero(self.field, 0, dimensions[0])?];
        for i in (1..self.boundaries.len()).rev() {
            boundaries.push(self.boundaries[i].transpose());
        }
        let dual = Self::new(self.field, dimensions, boundaries)?;
        let mut h = dual.homology(self.dimensions.len() - 1 - n)?;
        h.degree = n;
        Ok(h)
    }
    pub fn induced_ranks(&self, target: &Self, maps: &[Matrix]) -> MathResult<Vec<usize>> {
        require(self.field == target.field, "coefficient field mismatch")?;
        require(
            self.dimensions.len() == target.dimensions.len() && maps.len() == self.dimensions.len(),
            "chain map degree mismatch",
        )?;
        for (n, f) in maps.iter().enumerate() {
            require(f.field() == self.field, "coefficient field mismatch")?;
            require(
                (f.rows(), f.cols()) == (target.dimensions[n], self.dimensions[n]),
                "chain map shape mismatch",
            )?;
            if n > 0 {
                require(
                    target.boundaries[n].mul(f)? == maps[n - 1].mul(&self.boundaries[n])?,
                    "chain map does not commute with boundaries",
                )?;
            }
        }
        let mut ranks = Vec::new();
        for (n, f) in maps.iter().enumerate() {
            let mapped = f.mul(&self.homology(n)?.representatives)?;
            let image = if n + 1 < maps.len() {
                target.boundaries[n + 1].image()
            } else {
                Matrix::zero(self.field, target.dimensions[n], 0)?
            };
            ranks.push(image.extend_basis(&mapped)?.cols());
        }
        Ok(ranks)
    }
}
impl SimplicialComplex {
    /// Canonically sorted vertices orient each simplex. The j-th face has (-1)^j.
    pub fn chains_over(&self, field: PrimeField) -> MathResult<ChainComplex> {
        let simplices = self.simplices();
        let dimensions: Vec<_> = simplices.iter().map(Vec::len).collect();
        let mut boundaries = Vec::new();
        for (n, faces) in simplices.iter().enumerate() {
            let rows = if n == 0 { 0 } else { dimensions[n - 1] };
            let mut data = vec![0; rows * faces.len()];
            if n > 0 {
                for (c, s) in faces.iter().enumerate() {
                    for omit in 0..s.len() {
                        let mut face = s.clone();
                        face.remove(omit);
                        let r = simplices[n - 1]
                            .binary_search(&face)
                            .map_err(|_| MathError("missing face".into()))?;
                        data[r * faces.len() + c] = if omit % 2 == 0 {
                            1
                        } else {
                            field.modulus() - 1
                        };
                    }
                }
            }
            boundaries.push(Matrix::new(field, rows, faces.len(), data)?);
        }
        ChainComplex::new(field, dimensions, boundaries)
    }
}
