//! Untrusted witness construction using the existing exact elimination engine.
use super::{RankCertificate, WitnessResult};
use crate::math::{exact::ExactBudget, rational_matrix::Matrix};
pub fn certify_rank(a: &Matrix, b: &mut ExactBudget) -> WitnessResult<RankCertificate> {
    let pivots = a.rref(b)?.1;
    let rank = pivots.len();
    let columns = a.select_columns(&pivots, b)?;
    let coordinates = columns.solve(a, b)?.particular;
    let id = Matrix::identity(rank, b)?;
    let left = columns
        .transpose(b)?
        .solve(&id, b)?
        .particular
        .transpose(b)?;
    let right = coordinates.solve(&id, b)?.particular;
    let kernel = a.kernel(b)?;
    let kernel_id = Matrix::identity(a.cols() - rank, b)?;
    let kernel_left = kernel
        .transpose(b)?
        .solve(&kernel_id, b)?
        .particular
        .transpose(b)?;
    Ok(RankCertificate {
        input: a.clone(),
        rank,
        columns,
        coordinates,
        left,
        right,
        kernel,
        kernel_left,
    })
}
