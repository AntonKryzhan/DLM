//! Standalone exact Q rank/kernel witnesses; separate from kernel proof terms.
use dlm_core::math::{
    exact::ExactBudget,
    rational_matrix::{Matrix, MAX_TEXT_BYTES},
    rational_witness::{
        certify_rank, check_rank, check_rank_against, decode_rank_certificate,
        encode_rank_certificate, MAX_CERT_TEXT_BYTES,
    },
};
use std::io::Read;
fn budget() -> ExactBudget {
    ExactBudget::new(4096, 100_000_000).unwrap()
}
fn read(path: &str, limit: usize) -> Result<String, String> {
    let mut bytes = Vec::new();
    std::fs::File::open(path)
        .map_err(|e| e.to_string())?
        .take((limit + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() > limit {
        return Err("UnknownWithinBudget: Q input text limit exhausted".into());
    }
    String::from_utf8(bytes).map_err(|e| e.to_string())
}
fn finish(result: Result<String, String>) {
    match result {
        Ok(text) => print!("{text}"),
        Err(e) => {
            eprintln!("Q witness error: {e}");
            std::process::exit(1);
        }
    }
}
pub fn export(input: &str, output: &str) {
    finish((|| {
        let mut b = budget();
        let a = Matrix::parse(&read(input, MAX_TEXT_BYTES)?, &mut b).map_err(|e| e.to_string())?;
        let certificate = certify_rank(&a, &mut b).map_err(|e| e.to_string())?;
        let checked = check_rank(&certificate, &mut b).map_err(|e| e.to_string())?;
        let text = encode_rank_certificate(&certificate, &mut b).map_err(|e| e.to_string())?;
        std::fs::write(output, text).map_err(|e| e.to_string())?;
        Ok(format!("exported Q rank/kernel witness: {output}\nrank: {}; nullity: {}\nassurance: independent-runtime-witness; StaticProof: no\n",checked.rank,checked.nullity))
    })());
}
pub fn check(input: &str, expected: Option<&str>) {
    finish((|| {
        let mut b = budget();
        let certificate = decode_rank_certificate(&read(input, MAX_CERT_TEXT_BYTES)?, &mut b)
            .map_err(|e| e.to_string())?;
        let checked = match expected {
            Some(path) => {
                let a = Matrix::parse(&read(path, MAX_TEXT_BYTES)?, &mut b)
                    .map_err(|e| e.to_string())?;
                check_rank_against(&a, &certificate, &mut b)
            }
            None => check_rank(&certificate, &mut b),
        }
        .map_err(|e| e.to_string())?;
        Ok(format!("Q rank/kernel witness: checked\nclaim: {}\nrank: {}; nullity: {}\nverification work: {}\nassurance: independent-runtime-witness; StaticProof: no\n",if expected.is_some(){"matches supplied matrix"}else{"embedded matrix"},checked.rank,checked.nullity,checked.verification_work))
    })());
}
