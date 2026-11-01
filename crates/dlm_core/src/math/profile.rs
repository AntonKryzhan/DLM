//! Fixed numeric workloads and their verified cost profile (numeric criterion N14).
//!
//! Every workload is a deterministic computation on exact naturals of up to
//! 16384 bits. Its profile is reproducible: host work units (`ExactBudget`),
//! the result size in 32-bit limbs (memory) and the decimal result. The same
//! workloads run in DLM through `std.exact` (list storage, dense kernel) and
//! `std.dense` (`BigNat` storage); `tests/dense_profile.rs` compares values,
//! work and memory with the committed thresholds in [`THRESHOLDS`].
use super::exact::{ExactBudget, Natural};
use super::MathResult;

pub const PROFILE_BITS: usize = 16384;

/// `(name, description)`.
pub const WORKLOADS: &[(&str, &str)] = &[
    ("mul", "3^2000 * 7^1200"),
    ("divrem", "(3^4000 + 1) div and mod 7^700"),
    ("gcd", "gcd(6^1500 * 5, 6^1200 * 35)"),
    ("sqrt", "floor(sqrt(2 * 10^2000))"),
    ("text", "decimal text of 3^5000"),
    ("parse", "decimal text of 7^3000 parsed back"),
    ("pow", "3^10000"),
];

/// `(name, max host work, max result limbs, max source work in std.dense units)`.
pub const THRESHOLDS: &[(&str, u64, usize, u64)] = &[
    ("mul", 2_000_000, 210, 40_000),
    ("divrem", 3_000_000, 400, 80_000),
    ("gcd", 6_000_000, 200, 60_000),
    ("sqrt", 20_000_000, 110, 80_000),
    ("text", 10_000_000, 250, 100_000),
    ("parse", 10_000_000, 270, 120_000),
    ("pow", 10_000_000, 500, 200_000),
];

fn n(v: u128) -> Natural {
    Natural::from_u128(v)
}
fn pow(base: u128, e: u128, b: &mut ExactBudget) -> MathResult<Natural> {
    n(base).checked_pow(e, b)
}

/// Result text (decimal; `q r` for divrem) of a workload.
pub fn run(name: &str, b: &mut ExactBudget) -> MathResult<String> {
    Ok(match name {
        "mul" => pow(3, 2000, b)?
            .checked_mul(&pow(7, 1200, b)?, b)?
            .to_string(),
        "divrem" => {
            let (q, r) = pow(3, 4000, b)?
                .checked_add(&n(1), b)?
                .div_rem(&pow(7, 700, b)?, b)?;
            format!("{q} {r}")
        }
        "gcd" => {
            let a = pow(6, 1500, b)?.checked_mul(&n(5), b)?;
            let c = pow(6, 1200, b)?.checked_mul(&n(35), b)?;
            a.checked_gcd(&c, b)?.to_string()
        }
        "sqrt" => pow(10, 2000, b)?
            .checked_mul(&n(2), b)?
            .checked_sqrt_rem(b)?
            .0
            .to_string(),
        "text" => pow(3, 5000, b)?.to_string(),
        "parse" => {
            let t = pow(7, 3000, b)?.to_string();
            Natural::parse(&t, b)?.to_string()
        }
        "pow" => pow(3, 10000, b)?.to_string(),
        other => return Err(super::MathError(format!("unknown workload `{other}`"))),
    })
}

/// Measured profile of one workload.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProfileRow {
    pub name: &'static str,
    pub work: u64,
    pub result_limbs: usize,
    pub result: String,
}

pub fn limbs_of_decimal(t: &str) -> usize {
    t.split(' ')
        .map(|p| crate::dense_nat::from_decimal(p).map_or(0, |x| x.len()))
        .max()
        .unwrap_or(0)
}

pub fn measure() -> MathResult<Vec<ProfileRow>> {
    WORKLOADS
        .iter()
        .map(|(name, _)| {
            let mut b = ExactBudget::new(PROFILE_BITS, u64::MAX)?;
            let result = run(name, &mut b)?;
            Ok(ProfileRow {
                name,
                work: b.work_used(),
                result_limbs: limbs_of_decimal(&result),
                result,
            })
        })
        .collect()
}
