//! Search/production side. The independent checker does not call this module.
use super::*;

#[derive(Clone, Copy)]
enum RowOp {
    Swap(usize, usize),
    Add(usize, usize),
}
fn apply(data: &mut [u8], cols: usize, op: RowOp) {
    match op {
        RowOp::Swap(a, b) => {
            for c in 0..cols {
                data.swap(a * cols + c, b * cols + c);
            }
        }
        RowOp::Add(to, from) => {
            for c in 0..cols {
                data[to * cols + c] ^= data[from * cols + c];
            }
        }
    }
}
pub fn certify_rank(a: &Matrix) -> WitnessResult<RankWitness> {
    let rows = a.rows();
    let cols = a.cols();
    let mut data = a.entries().to_vec();
    let mut pivots = Vec::new();
    let mut operations = Vec::new();
    let mut row = 0;
    for col in 0..cols {
        let Some(p) = (row..rows).find(|&r| data[r * cols + col] == 1) else {
            continue;
        };
        if p != row {
            let op = RowOp::Swap(row, p);
            apply(&mut data, cols, op);
            operations.push(op);
        }
        for r in 0..rows {
            if r != row && data[r * cols + col] == 1 {
                let op = RowOp::Add(r, row);
                apply(&mut data, cols, op);
                operations.push(op);
            }
        }
        pivots.push(col);
        row += 1;
    }
    let mut left = Matrix::identity(rows)?.entries().to_vec();
    let mut inverse = left.clone();
    for &op in &operations {
        apply(&mut left, rows, op);
    }
    // Every elementary row operation here is self-inverse over F2.
    for &op in operations.iter().rev() {
        apply(&mut inverse, rows, op);
    }
    Ok(RankWitness {
        reduced: Matrix::new(rows, cols, data)?,
        left: Matrix::new(rows, rows, left)?,
        left_inverse: Matrix::new(rows, rows, inverse)?,
        pivots,
    })
}
pub fn certify_homology(
    complex: &ChainComplex,
    budget: WitnessBudget,
) -> WitnessResult<HomologyCertificate> {
    let mut groups = Vec::new();
    for (n, down) in complex.boundaries().iter().enumerate() {
        let top = Matrix::zero(down.cols(), 0)?;
        let up = complex.boundaries().get(n + 1).unwrap_or(&top);
        let representatives = complex.homology(n)?.representatives;
        let outgoing = certify_rank(down)?;
        let incoming = certify_rank(up)?;
        let joined = up
            .select_columns(&incoming.pivots)?
            .hstack(&representatives)?;
        let quotient = certify_rank(&joined)?;
        groups.push(HomologyWitness {
            degree: n,
            representatives,
            outgoing,
            incoming,
            quotient,
        });
    }
    let certificate = HomologyCertificate {
        boundaries: complex.boundaries().to_vec(),
        groups,
    };
    check_homology_against(complex, &certificate, budget)?;
    Ok(certificate)
}
