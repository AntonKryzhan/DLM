//! Exact rational computations with explicit resource limits and runtime assurance.
use dlm_core::math::{
    exact::ExactBudget,
    rational_matrix::{Matrix, MAX_TEXT_BYTES},
};
use std::io::Read;
fn read_matrix(path: &str, budget: &mut ExactBudget) -> Result<Matrix, String> {
    let mut bytes = Vec::new();
    std::fs::File::open(path)
        .map_err(|e| e.to_string())?
        .take((MAX_TEXT_BYTES + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() > MAX_TEXT_BYTES {
        return Err("rational matrix text budget exceeded".into());
    }
    let text = std::str::from_utf8(&bytes).map_err(|e| e.to_string())?;
    Matrix::parse(text, budget).map_err(|e| e.to_string())
}
fn compute(args: &[String]) -> Result<String, String> {
    let mut bits = 4096;
    let mut work = 50_000_000;
    let mut inverse = false;
    let mut rhs = None;
    let mut path = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--bits" | "--work" | "--solve" => {
                let option = args[i].as_str();
                i += 1;
                let value = args
                    .get(i)
                    .ok_or_else(|| format!("{option} requires a value"))?;
                match option {
                    "--bits" => bits = value.parse::<usize>().map_err(|_| "invalid precision")?,
                    "--work" => work = value.parse::<u64>().map_err(|_| "invalid work budget")?,
                    _ => rhs = Some(value.as_str()),
                };
            }
            "--inverse" => inverse = true,
            arg if arg.starts_with('-') => return Err(format!("unknown matrix-q option {arg}")),
            file => {
                if path.replace(file).is_some() {
                    return Err("matrix-q requires one matrix path".into());
                }
            }
        }
        i += 1;
    }
    let path = path.ok_or("matrix-q requires a matrix path")?;
    let mut budget = ExactBudget::new(bits, work).map_err(|e| e.to_string())?;
    let a = read_matrix(path, &mut budget)?;
    let (reduced, pivots) = a.rref(&mut budget).map_err(|e| e.to_string())?;
    let kernel = a.kernel(&mut budget).map_err(|e| e.to_string())?;
    let mut output=format!("coefficients: Q; precision: {bits} bits\nassurance: runtime-checked; StaticProof: no\nshape: {} x {}; rank: {}\nrref:\n{reduced}kernel:\n{kernel}",a.rows(),a.cols(),pivots.len());
    if a.rows() == a.cols() {
        let det = a.determinant(&mut budget).map_err(|e| e.to_string())?;
        output += &format!("determinant: {det}\n");
    }
    if inverse {
        let inv = a.inverse(&mut budget).map_err(|e| e.to_string())?;
        output += &format!("inverse:\n{inv}");
    }
    if let Some(path) = rhs {
        let b = read_matrix(path, &mut budget)?;
        let solution = a.solve(&b, &mut budget).map_err(|e| e.to_string())?;
        output += &format!(
            "particular solution:\n{}homogeneous kernel:\n{}",
            solution.particular, solution.kernel
        );
    }
    output += &format!("work used: {}\n", budget.work_used());
    Ok(output)
}
pub fn run(args: &[String]) {
    match compute(args) {
        Ok(text) => print!("{text}"),
        Err(error) => {
            eprintln!("rational matrix error: {error}");
            std::process::exit(1);
        }
    }
}
