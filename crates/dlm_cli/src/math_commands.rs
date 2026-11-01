//! F2 computation-witness commands, separate from Nat-kernel proof commands.
use dlm_core::math::topology::{ChainComplex, SimplicialComplex};
use dlm_core::math::witness::{
    certify_homology, check_homology, check_homology_against, decode_homology_certificate,
    encode_homology_certificate, WitnessBudget,
};
use std::{fs, io::Read};
fn read_text(path: &str, limit: usize) -> Result<String, String> {
    let mut bytes = Vec::new();
    fs::File::open(path)
        .map_err(|e| e.to_string())?
        .take((limit + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() > limit {
        return Err("UnknownWithinBudget: input text budget exhausted".into());
    }
    String::from_utf8(bytes).map_err(|e| e.to_string())
}
fn complex(path: &str) -> Result<ChainComplex, String> {
    SimplicialComplex::parse(&read_text(path, 65536)?)
        .and_then(|c| c.chains())
        .map_err(|e| e.to_string())
}
fn finish(result: Result<(), String>) {
    if let Err(e) = result {
        eprintln!("homology witness error: {e}");
        std::process::exit(1);
    }
}
pub fn export(input: &str, output: &str) {
    finish((|| {
        let chain = complex(input)?;
        let budget = WitnessBudget::default();
        let certificate = certify_homology(&chain, budget).map_err(|e| e.to_string())?;
        let text = encode_homology_certificate(&certificate, budget).map_err(|e| e.to_string())?;
        fs::write(output, text).map_err(|e| e.to_string())?;
        println!("exported F2 homology witness: {output}");
        println!("assurance: independent-runtime-witness; StaticProof: no");
        Ok(())
    })());
}
pub fn check(input: &str, expected: Option<&str>) {
    finish((|| {
        let budget = WitnessBudget::default();
        let text = read_text(input, budget.max_text_bytes)?;
        let certificate = decode_homology_certificate(&text, budget).map_err(|e| e.to_string())?;
        let checked = match expected {
            Some(path) => check_homology_against(&complex(path)?, &certificate, budget),
            None => check_homology(&certificate, budget),
        }
        .map_err(|e| e.to_string())?;
        println!("F2 homology witness: checked");
        println!(
            "claim: {}",
            if expected.is_some() {
                "matches supplied complex"
            } else {
                "embedded boundary matrices"
            }
        );
        println!("Betti numbers: {:?}", checked.betti);
        println!("scalar verification work: {}", checked.scalar_work);
        println!("assurance: independent-runtime-witness; StaticProof: no");
        Ok(())
    })());
}

/// Explicit coefficients; F2 witness transport remains unchanged.
pub fn homology_prime(modulus: &str, path: &str) {
    use dlm_core::math::prime::PrimeField;
    let result = (|| -> Result<(), String> {
        let p = modulus
            .parse::<u16>()
            .map_err(|_| "invalid prime modulus".to_string())?;
        let field = PrimeField::new(p).map_err(|e| e.to_string())?;
        let complex =
            SimplicialComplex::parse(&read_text(path, 65536)?).map_err(|e| e.to_string())?;
        let chains = complex.chains_over(field).map_err(|e| e.to_string())?;
        println!("coefficients: F{p}; assurance: runtime-checked; homology: ordinary");
        println!("chain dimensions: {:?}", chains.dimensions());
        println!("oriented boundary squared: zero; StaticProof: no");
        for n in 0..chains.dimensions().len() {
            let h = chains.homology(n).map_err(|e| e.to_string())?;
            let co = chains.cohomology(n).map_err(|e| e.to_string())?;
            println!(
                "H_{n}: dimension {}; H^{n}: dimension {}",
                h.betti, co.betti
            );
            for c in 0..h.representatives.cols() {
                let terms: Vec<_> = complex.simplices()[n]
                    .iter()
                    .enumerate()
                    .filter_map(|(r, s)| {
                        let a = h.representatives.entries()[r * h.representatives.cols() + c];
                        (a != 0).then(|| format!("{a}*{s:?}"))
                    })
                    .collect();
                println!("  cycle {c}: {}", terms.join(" + "));
            }
        }
        Ok(())
    })();
    if let Err(e) = result {
        eprintln!("homology error: {e}");
        std::process::exit(1);
    }
}
