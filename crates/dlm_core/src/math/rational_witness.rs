//! Algorithm-independent exact Q rank/kernel witnesses. No elimination here.
//! Checking uses canonical scalar arithmetic and matrix identities, not StaticProof.
use super::exact::ExactBudget;
use super::rational_matrix::{Matrix, MAX_AXIS};
use super::MathError;
mod codec;
mod producer;
pub use codec::{decode_rank_certificate, encode_rank_certificate, MAX_CERT_TEXT_BYTES};
pub use producer::certify_rank;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ErrorKind {
    Invalid,
    UnknownWithinBudget,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub kind: ErrorKind,
    pub message: String,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}: {}", self.kind, self.message)
    }
}
impl std::error::Error for Error {}
impl From<MathError> for Error {
    fn from(e: MathError) -> Self {
        let kind = if e.0.starts_with("UnknownWithinBudget:") {
            ErrorKind::UnknownWithinBudget
        } else {
            ErrorKind::Invalid
        };
        Self {
            kind,
            message: e
                .0
                .strip_prefix("UnknownWithinBudget:")
                .unwrap_or(&e.0)
                .trim()
                .to_string(),
        }
    }
}
pub type WitnessResult<T> = Result<T, Error>;
fn invalid(message: impl Into<String>) -> Error {
    Error {
        kind: ErrorKind::Invalid,
        message: message.into(),
    }
}
fn exhausted(message: impl Into<String>) -> Error {
    Error {
        kind: ErrorKind::UnknownWithinBudget,
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
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RankCertificate {
    pub input: Matrix,
    pub rank: usize,
    /// A=C*R, giving rank(A)<=rank.
    pub columns: Matrix,
    pub coordinates: Matrix,
    /// L*A*U=I_rank, giving the matching lower bound.
    pub left: Matrix,
    pub right: Matrix,
    /// A*K=0 and V*K=I_(n-rank), giving a full kernel basis.
    pub kernel: Matrix,
    pub kernel_left: Matrix,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CheckedRank {
    pub rank: usize,
    pub nullity: usize,
    pub kernel: Matrix,
    pub verification_work: u64,
}
fn shape(a: &Matrix, rows: usize, cols: usize, message: &str) -> WitnessResult<()> {
    valid((a.rows(), a.cols()) == (rows, cols), message)
}
fn matrix(a: &Matrix, b: &mut ExactBudget) -> WitnessResult<()> {
    for q in a.entries() {
        q.check_budget(b)?;
    }
    b.charge(a.entries().len() + 1)?;
    Ok(())
}
/// This verifier intentionally does not call rref, rank search, solve or inverse.
pub fn check_rank(c: &RankCertificate, b: &mut ExactBudget) -> WitnessResult<CheckedRank> {
    let start = b.work_used();
    let m = c.input.rows();
    let n = c.input.cols();
    let r = c.rank;
    valid(r <= m.min(n) && r <= MAX_AXIS, "invalid claimed Q rank")?;
    for a in [
        &c.input,
        &c.columns,
        &c.coordinates,
        &c.left,
        &c.right,
        &c.kernel,
        &c.kernel_left,
    ] {
        matrix(a, b)?;
    }
    shape(&c.columns, m, r, "Q rank columns shape mismatch")?;
    shape(&c.coordinates, r, n, "Q rank coordinates shape mismatch")?;
    shape(&c.left, r, m, "Q rank lower-bound left shape mismatch")?;
    shape(&c.right, n, r, "Q rank lower-bound right shape mismatch")?;
    shape(&c.kernel, n, n - r, "Q kernel shape mismatch")?;
    shape(
        &c.kernel_left,
        n - r,
        n,
        "Q kernel left inverse shape mismatch",
    )?;
    valid(
        c.columns.mul(&c.coordinates, b)? == c.input,
        "Q rank factorization fails",
    )?;
    valid(
        c.left.mul(&c.input, b)?.mul(&c.right, b)? == Matrix::identity(r, b)?,
        "Q rank lower bound fails",
    )?;
    valid(
        c.input.mul(&c.kernel, b)?.is_zero(),
        "Q kernel contains a noncycle",
    )?;
    valid(
        c.kernel_left.mul(&c.kernel, b)? == Matrix::identity(n - r, b)?,
        "Q kernel independence fails",
    )?;
    Ok(CheckedRank {
        rank: r,
        nullity: n - r,
        kernel: c.kernel.clone(),
        verification_work: b.work_used() - start,
    })
}
pub fn check_rank_against(
    expected: &Matrix,
    c: &RankCertificate,
    b: &mut ExactBudget,
) -> WitnessResult<CheckedRank> {
    valid(
        expected == &c.input,
        "Q certificate does not match supplied matrix",
    )?;
    check_rank(c, b)
}
/// The rank/kernel witness plus A*P=B certifies the complete affine solution set.
pub fn check_solution(
    c: &RankCertificate,
    rhs: &Matrix,
    particular: &Matrix,
    b: &mut ExactBudget,
) -> WitnessResult<CheckedRank> {
    let start = b.work_used();
    let mut checked = check_rank(c, b)?;
    shape(
        rhs,
        c.input.rows(),
        rhs.cols(),
        "Q solution RHS shape mismatch",
    )?;
    shape(
        particular,
        c.input.cols(),
        rhs.cols(),
        "Q particular solution shape mismatch",
    )?;
    matrix(rhs, b)?;
    matrix(particular, b)?;
    valid(
        c.input.mul(particular, b)? == *rhs,
        "Q particular solution residual is nonzero",
    )?;
    checked.verification_work = b.work_used() - start;
    Ok(checked)
}
pub fn check_inverse(a: &Matrix, inverse: &Matrix, b: &mut ExactBudget) -> WitnessResult<()> {
    valid(
        a.rows() == a.cols(),
        "Q inverse claim requires square input",
    )?;
    shape(inverse, a.rows(), a.cols(), "Q inverse shape mismatch")?;
    matrix(a, b)?;
    matrix(inverse, b)?;
    let id = Matrix::identity(a.rows(), b)?;
    valid(
        a.mul(inverse, b)? == id && inverse.mul(a, b)? == id,
        "Q inverse identities fail",
    )
}
