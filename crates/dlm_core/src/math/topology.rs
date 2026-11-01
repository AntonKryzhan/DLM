//! Finite unaugmented chain complexes over F2 and simplicial homology.
use super::f2::{Matrix, MAX_AXIS};
use super::{require, MathError, MathResult};
use std::collections::BTreeSet;
pub const MAX_DEGREES: usize = 16;
#[derive(Clone, Debug)]
pub struct ChainComplex {
    dimensions: Vec<usize>,
    boundaries: Vec<Matrix>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Homology {
    pub degree: usize,
    pub betti: usize,
    /// Columns represent a basis modulo boundaries in the original chain basis.
    pub representatives: Matrix,
}
impl ChainComplex {
    /// boundaries[n] is d_n: C_n -> C_(n-1); d_0 has zero rows.
    pub fn new(dimensions: Vec<usize>, boundaries: Vec<Matrix>) -> MathResult<Self> {
        require(
            dimensions.len() <= MAX_DEGREES,
            "chain degree budget exceeded",
        )?;
        require(
            dimensions.len() == boundaries.len(),
            "one boundary per degree required",
        )?;
        for (n, d) in boundaries.iter().enumerate() {
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
            dimensions,
            boundaries,
        })
    }
    pub fn dimensions(&self) -> &[usize] {
        &self.dimensions
    }
    pub fn boundaries(&self) -> &[Matrix] {
        &self.boundaries
    }
    pub fn homology(&self, n: usize) -> MathResult<Homology> {
        require(n < self.dimensions.len(), "homology degree out of range")?;
        let kernel = self.boundaries[n].kernel();
        let mut span = if n + 1 < self.dimensions.len() {
            self.boundaries[n + 1].image()
        } else {
            Matrix::zero(self.dimensions[n], 0)?
        };
        let mut rank = span.cols();
        let mut chosen = Vec::new();
        for j in 0..kernel.cols() {
            if rank == self.dimensions[n] {
                break;
            }
            let candidate = span.hstack(&kernel.select_columns(&[j])?)?;
            let next = candidate.rank();
            if next > rank {
                span = candidate;
                rank = next;
                chosen.push(j);
            }
        }
        Ok(Homology {
            degree: n,
            betti: chosen.len(),
            representatives: kernel.select_columns(&chosen)?,
        })
    }
    /// Total tensor product. Over F2 the Koszul sign is always +1.
    pub fn tensor(&self, rhs: &Self) -> MathResult<Self> {
        if self.dimensions.is_empty() || rhs.dimensions.is_empty() {
            return Self::new(vec![], vec![]);
        }
        let count = self.dimensions.len() + rhs.dimensions.len() - 1;
        require(count <= MAX_DEGREES, "tensor degree budget exceeded")?;
        let mut summands = vec![Vec::new(); count];
        let mut dimensions = vec![0usize; count];
        for (i, &a) in self.dimensions.iter().enumerate() {
            for (j, &b) in rhs.dimensions.iter().enumerate() {
                let n = i + j;
                let offset = dimensions[n];
                dimensions[n] += a * b;
                require(
                    dimensions[n] <= MAX_AXIS,
                    "tensor chain dimension budget exceeded",
                )?;
                summands[n].push((i, j, offset));
            }
        }
        let mut boundaries = Vec::new();
        for n in 0..count {
            let rows = if n == 0 { 0 } else { dimensions[n - 1] };
            let cols = dimensions[n];
            let mut data = vec![0; rows * cols];
            for &(i, j, offset) in &summands[n] {
                for side in 0..2 {
                    if (side == 0 && i == 0) || (side == 1 && j == 0) {
                        continue;
                    }
                    let (ti, tj, block) = if side == 0 {
                        (
                            i - 1,
                            j,
                            self.boundaries[i].tensor(&Matrix::identity(rhs.dimensions[j])?)?,
                        )
                    } else {
                        (
                            i,
                            j - 1,
                            Matrix::identity(self.dimensions[i])?.tensor(&rhs.boundaries[j])?,
                        )
                    };
                    let row_offset = summands[n - 1]
                        .iter()
                        .find(|&&(a, b, _)| a == ti && b == tj)
                        .unwrap()
                        .2;
                    for r in 0..block.rows() {
                        for c in 0..block.cols() {
                            data[(row_offset + r) * cols + offset + c] ^=
                                block.entries()[r * block.cols() + c];
                        }
                    }
                }
            }
            boundaries.push(Matrix::new(rows, cols, data)?);
        }
        Self::new(dimensions, boundaries)
    }
    /// H^n = ker(d_(n+1)^T) / im(d_n^T).
    pub fn cohomology(&self, n: usize) -> MathResult<Homology> {
        require(n < self.dimensions.len(), "cohomology degree out of range")?;
        let mut dimensions = self.dimensions.clone();
        dimensions.reverse();
        let mut boundaries = vec![Matrix::zero(0, dimensions[0])?];
        for i in (1..self.boundaries.len()).rev() {
            boundaries.push(self.boundaries[i].transpose());
        }
        let dual = Self::new(dimensions, boundaries)?;
        let mut h = dual.homology(self.dimensions.len() - 1 - n)?;
        h.degree = n;
        Ok(h)
    }
    /// Validate a degree-preserving chain map, then compute ranks on homology.
    pub fn induced_ranks(&self, target: &Self, maps: &[Matrix]) -> MathResult<Vec<usize>> {
        require(
            self.dimensions.len() == target.dimensions.len() && maps.len() == self.dimensions.len(),
            "chain map degree mismatch",
        )?;
        for (n, f) in maps.iter().enumerate() {
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
            let cycles = f.mul(&self.homology(n)?.representatives)?;
            let image = if n + 1 < maps.len() {
                target.boundaries[n + 1].image()
            } else {
                Matrix::zero(target.dimensions[n], 0)?
            };
            // Avoid concatenating dependent columns past the bounded ambient dimension.
            let mut span = image.clone();
            let mut rank = image.cols();
            for j in 0..cycles.cols() {
                if rank == span.rows() {
                    break;
                }
                let next = span.hstack(&cycles.select_columns(&[j])?)?;
                let r = next.rank();
                if r > rank {
                    span = next;
                    rank = r;
                }
            }
            ranks.push(rank - image.cols());
        }
        Ok(ranks)
    }
}
#[derive(Clone, Debug)]
pub struct SimplicialComplex {
    simplices: Vec<Vec<Vec<u32>>>,
}
impl SimplicialComplex {
    /// Maximal faces or any generating faces; closure is inserted automatically.
    /// Repeated vertices are errors; repeated faces are harmless.
    pub fn from_facets(facets: &[Vec<u32>]) -> MathResult<Self> {
        require(facets.len() <= MAX_AXIS, "facet budget exceeded")?;
        let mut all = BTreeSet::new();
        for facet in facets {
            require(
                !facet.is_empty() && facet.len() <= MAX_DEGREES,
                "invalid facet dimension",
            )?;
            let mut sorted = facet.clone();
            sorted.sort_unstable();
            sorted.dedup();
            require(sorted.len() == facet.len(), "repeated vertex in simplex")?;
            for mask in 1usize..(1usize << sorted.len()) {
                let face: Vec<_> = sorted
                    .iter()
                    .enumerate()
                    .filter(|(i, _)| mask & (1 << i) != 0)
                    .map(|(_, v)| *v)
                    .collect();
                all.insert(face);
                require(all.len() <= MAX_AXIS, "simplex budget exceeded")?;
            }
        }
        let degrees = all.iter().map(Vec::len).max().unwrap_or(0);
        let mut simplices = vec![Vec::new(); degrees];
        for s in all {
            simplices[s.len() - 1].push(s);
        }
        Ok(Self { simplices })
    }
    pub fn simplices(&self) -> &[Vec<Vec<u32>>] {
        &self.simplices
    }
    pub fn chains(&self) -> MathResult<ChainComplex> {
        let dimensions: Vec<_> = self.simplices.iter().map(Vec::len).collect();
        let mut boundaries = Vec::new();
        for (n, faces) in self.simplices.iter().enumerate() {
            let rows = if n == 0 { 0 } else { dimensions[n - 1] };
            let mut data = vec![0; rows * faces.len()];
            if n > 0 {
                for (c, s) in faces.iter().enumerate() {
                    for omit in 0..s.len() {
                        let mut face = s.clone();
                        face.remove(omit);
                        let r = self.simplices[n - 1]
                            .binary_search(&face)
                            .map_err(|_| MathError("missing face".into()))?;
                        data[r * faces.len() + c] ^= 1;
                    }
                }
            }
            boundaries.push(Matrix::new(rows, faces.len(), data)?);
        }
        ChainComplex::new(dimensions, boundaries)
    }
    /// Strict text format: one generating simplex per line, u32 vertex labels.
    pub fn parse(text: &str) -> MathResult<Self> {
        require(text.len() <= 65536, "complex input byte budget exceeded")?;
        let mut facets = Vec::new();
        for line in text.lines() {
            let line = line.split('#').next().unwrap().trim();
            if line.is_empty() {
                continue;
            }
            let values = line
                .split_whitespace()
                .map(|v| {
                    v.parse::<u32>()
                        .map_err(|_| MathError("invalid vertex label".into()))
                })
                .collect::<MathResult<Vec<_>>>()?;
            facets.push(values);
            require(facets.len() <= MAX_AXIS, "facet budget exceeded")?;
        }
        Self::from_facets(&facets)
    }
}
