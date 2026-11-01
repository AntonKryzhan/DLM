//! Bounded, exact computational mathematics. Results are runtime witnesses,
//! never `StaticProof`. No passports or proof objects occur in matrix entries.
pub mod algebra;
pub mod category;
pub mod f2;
pub mod topology;
pub mod rational;
pub mod exact;
pub mod complex_q;
pub mod complex;
pub mod interval_q;
pub mod creal;
pub mod elementary;
pub mod convert;
pub mod portable;
pub mod contract;
pub mod scalar_witness;
pub mod profile;
pub mod acceptance;
pub mod ccomplex;
pub mod rational_matrix;
pub mod rational_witness;
pub mod prime;
pub mod prime_topology;
pub mod witness;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MathError(pub String);
impl std::fmt::Display for MathError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for MathError {}
pub type MathResult<T> = Result<T, MathError>;
pub(crate) fn require(ok: bool, message: &str) -> MathResult<()> {
    if ok {
        Ok(())
    } else {
        Err(MathError(message.into()))
    }
}
