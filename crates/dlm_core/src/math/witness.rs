//! Independent bounded verification of finite F2 computation witnesses.
//! This checker uses scalar matrix identities and RREF shape checks only;
//! it never calls row reduction, rank search or a homology solver.
//! Acceptance is a runtime witness, not a Nat-kernel StaticProof.
use super::f2::Matrix;
use super::topology::{ChainComplex, MAX_DEGREES};
use super::MathError;
use std::fmt;
mod codec;
mod producer;
pub use codec::{decode_homology_certificate, encode_homology_certificate};
pub use producer::{certify_homology, certify_rank};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WitnessErrorKind {
    Invalid,
    UnknownWithinBudget,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WitnessError {
    pub kind: WitnessErrorKind,
    pub message: String,
}
impl fmt::Display for WitnessError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}: {}", self.kind, self.message)
    }
}
impl std::error::Error for WitnessError {}
impl From<MathError> for WitnessError {
    fn from(value: MathError) -> Self {
        invalid(value.0)
    }
}
pub type WitnessResult<T> = Result<T, WitnessError>;
fn invalid(message: impl Into<String>) -> WitnessError {
    WitnessError {
        kind: WitnessErrorKind::Invalid,
        message: message.into(),
    }
}
fn exhausted(message: impl Into<String>) -> WitnessError {
    WitnessError {
        kind: WitnessErrorKind::UnknownWithinBudget,
        message: message.into(),
    }
}
fn valid(ok: bool, message: &str) -> WitnessResult<()> {
    if ok {
        Ok(())
    } else {
        Err(invalid(message))
    }
}

#[derive(Clone, Copy, Debug)]
pub struct WitnessBudget {
    /// Conservative count of scalar multiply-add positions, including zeros.
    pub max_work: u64,
    pub max_text_bytes: usize,
    pub max_matrices: usize,
}
impl Default for WitnessBudget {
    fn default() -> Self {
        Self {
            max_work: 100_000_000,
            max_text_bytes: 16 << 20,
            max_matrices: 256,
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RankWitness {
    pub reduced: Matrix,
    pub left: Matrix,
    pub left_inverse: Matrix,
    pub pivots: Vec<usize>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HomologyWitness {
    pub degree: usize,
    pub representatives: Matrix,
    pub outgoing: RankWitness,
    pub incoming: RankWitness,
    pub quotient: RankWitness,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HomologyCertificate {
    /// Embedded claim: d_0, d_1, ... in their specified chain bases.
    pub boundaries: Vec<Matrix>,
    pub groups: Vec<HomologyWitness>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CheckedHomology {
    pub betti: Vec<usize>,
    pub scalar_work: u64,
}

struct Verifier {
    budget: WitnessBudget,
    work: u64,
    matrices: usize,
}
impl Verifier {
    fn new(budget: WitnessBudget) -> Self {
        Self {
            budget,
            work: 0,
            matrices: 0,
        }
    }
    fn matrix(&mut self, matrix: &Matrix) -> WitnessResult<()> {
        let _ = matrix; // Matrix's private storage has already checked its F2/shape invariant.
        if self.matrices >= self.budget.max_matrices {
            return Err(exhausted("witness matrix budget exhausted"));
        }
        self.matrices += 1;
        Ok(())
    }
    fn mul(&mut self, a: &Matrix, b: &Matrix) -> WitnessResult<Matrix> {
        valid(
            a.cols() == b.rows(),
            "witness matrix multiplication shape mismatch",
        )?;
        let cost = (a.rows() as u64) * (a.cols() as u64) * (b.cols() as u64);
        if cost > self.budget.max_work.saturating_sub(self.work) {
            return Err(exhausted("witness scalar work budget exhausted"));
        }
        self.work += cost;
        Ok(a.mul(b)?)
    }
    fn rank(&mut self, a: &Matrix, w: &RankWitness) -> WitnessResult<usize> {
        for matrix in [&w.reduced, &w.left, &w.left_inverse] {
            self.matrix(matrix)?;
        }
        let rows = a.rows();
        let cols = a.cols();
        valid(
            (w.reduced.rows(), w.reduced.cols()) == (rows, cols),
            "reduced matrix shape mismatch",
        )?;
        valid(
            (w.left.rows(), w.left.cols()) == (rows, rows)
                && (w.left_inverse.rows(), w.left_inverse.cols()) == (rows, rows),
            "rank transform must be square on rows",
        )?;
        valid(
            w.pivots.len() <= rows && w.pivots.len() <= cols,
            "too many rank pivots",
        )?;
        valid(
            w.pivots.iter().all(|&p| p < cols) && w.pivots.windows(2).all(|p| p[0] < p[1]),
            "invalid rank pivot order",
        )?;
        valid(
            self.mul(&w.left, a)? == w.reduced,
            "row transform does not produce claimed reduction",
        )?;
        let id = Matrix::identity(rows)?;
        valid(
            self.mul(&w.left, &w.left_inverse)? == id,
            "left inverse equation fails",
        )?;
        valid(
            self.mul(&w.left_inverse, &w.left)? == id,
            "right inverse equation fails",
        )?;
        let data = w.reduced.entries();
        for (r, &p) in w.pivots.iter().enumerate() {
            valid(
                data[r * cols + p] == 1 && data[r * cols..r * cols + p].iter().all(|&v| v == 0),
                "rank pivot is not the leading one",
            )?;
            for other in 0..rows {
                if other != r {
                    valid(
                        data[other * cols + p] == 0,
                        "rank pivot column is not reduced",
                    )?;
                }
            }
        }
        valid(
            data[w.pivots.len() * cols..].iter().all(|&v| v == 0),
            "nonzero row omitted from rank witness",
        )?;
        Ok(w.pivots.len())
    }
}

pub fn check_rank(
    a: &Matrix,
    witness: &RankWitness,
    budget: WitnessBudget,
) -> WitnessResult<usize> {
    Verifier::new(budget).rank(a, witness)
}
/// Verify the entire embedded claim. No external source is consulted.
pub fn check_homology(
    certificate: &HomologyCertificate,
    budget: WitnessBudget,
) -> WitnessResult<CheckedHomology> {
    let mut v = Verifier::new(budget);
    let ds = &certificate.boundaries;
    valid(
        ds.len() <= MAX_DEGREES && ds.len() == certificate.groups.len(),
        "homology degree count mismatch",
    )?;
    for (n, d) in ds.iter().enumerate() {
        v.matrix(d)?;
        valid(
            d.rows() == if n == 0 { 0 } else { ds[n - 1].cols() },
            "chain boundary shape mismatch",
        )?;
        if n > 0 {
            valid(
                v.mul(&ds[n - 1], d)?.is_zero(),
                "boundary squared is nonzero",
            )?;
        }
    }
    let mut betti = Vec::new();
    for (n, w) in certificate.groups.iter().enumerate() {
        valid(w.degree == n, "homology degree order mismatch")?;
        let down = &ds[n];
        let top = Matrix::zero(down.cols(), 0)?;
        let up = ds.get(n + 1).unwrap_or(&top);
        let outgoing = v.rank(down, &w.outgoing)?;
        let incoming = v.rank(up, &w.incoming)?;
        let h = down
            .cols()
            .checked_sub(outgoing + incoming)
            .ok_or_else(|| invalid("negative homology dimension"))?;
        v.matrix(&w.representatives)?;
        valid(
            (w.representatives.rows(), w.representatives.cols()) == (down.cols(), h),
            "homology representative count or shape mismatch",
        )?;
        valid(
            v.mul(down, &w.representatives)?.is_zero(),
            "homology representative is not a cycle",
        )?;
        let boundaries = up.select_columns(&w.incoming.pivots)?;
        let basis = boundaries.hstack(&w.representatives)?;
        valid(
            v.rank(&basis, &w.quotient)? == incoming + h,
            "representatives are dependent modulo boundaries",
        )?;
        betti.push(h);
    }
    Ok(CheckedHomology {
        betti,
        scalar_work: v.work,
    })
}
/// Bind a certificate to externally supplied boundaries, not just its embedded claim.
pub fn check_homology_against(
    expected: &ChainComplex,
    certificate: &HomologyCertificate,
    budget: WitnessBudget,
) -> WitnessResult<CheckedHomology> {
    valid(
        expected.boundaries() == certificate.boundaries,
        "certificate is for a different chain complex",
    )?;
    check_homology(certificate, budget)
}
