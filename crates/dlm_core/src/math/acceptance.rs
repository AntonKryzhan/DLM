//! Acceptance of the numeric section (numeric criterion N16): every criterion
//! N01..N16 names its evidence — test files, fixtures (each produced by an
//! independent generator in `tools/`), source modules and documents.
//! `tests/numeric_acceptance.rs` checks that the evidence exists and is
//! consistent, and runs the cross-family regression in the host, the
//! interpreter and native C (sanitizers and MinGW-w64/Wine when available).

pub struct Criterion {
    pub id: &'static str,
    pub title: &'static str,
    pub tests: &'static [&'static str],
    pub fixtures: &'static [&'static str],
    pub modules: &'static [&'static str],
    pub docs: &'static [&'static str],
}

const fn c(
    id: &'static str,
    title: &'static str,
    tests: &'static [&'static str],
    fixtures: &'static [&'static str],
    modules: &'static [&'static str],
    docs: &'static [&'static str],
) -> Criterion {
    Criterion {
        id,
        title,
        tests,
        fixtures,
        modules,
        docs,
    }
}

pub const CRITERIA: &[Criterion] = &[
    c(
        "N01",
        "machine Nat/Int",
        &["integers.rs", "machine_arithmetic.rs"],
        &[],
        &["nat"],
        &[],
    ),
    c(
        "N02",
        "canonical multiword integers",
        &["exact_numbers.rs", "source_exact.rs"],
        &["exact093.tsv", "source_exact094.tsv"],
        &["exact"],
        &["SOURCE_EXACT_ARITHMETIC.md"],
    ),
    c(
        "N03",
        "exact rationals",
        &["rational.rs", "exact_numbers.rs"],
        &["rational128.tsv", "exact093.tsv"],
        &["exact", "rational"],
        &[],
    ),
    c(
        "N04",
        "exact precision range",
        &["source_wide_exact.rs"],
        &["wide_exact097.tsv", "wide_decimal097.txt"],
        &["exact"],
        &["SOURCE_EXACT_PRECISION.md"],
    ),
    c(
        "N05",
        "scalar operations",
        &["exact_ops.rs", "source_exact_ops.rs"],
        &["exact_ops096.tsv"],
        &["exact"],
        &["EXACT_SCALAR_OPERATIONS.md"],
    ),
    c(
        "N06",
        "exact complex numbers over Q",
        &["exact_complex_q.rs", "complex_q.rs", "source_complex_q.rs"],
        &[
            "complex_q097.tsv",
            "complex_q098.tsv",
            "complex_q098_source.tsv",
        ],
        &["complex_q"],
        &["EXACT_COMPLEX_Q.md"],
    ),
    c(
        "N07",
        "rational intervals",
        &["interval_q.rs"],
        &["interval_q101.tsv"],
        &["interval_q"],
        &["EXACT_INTERVALS.md"],
    ),
    c(
        "N08",
        "computable reals",
        &["creal.rs"],
        &["creal102.tsv"],
        &["creal"],
        &["COMPUTABLE_REALS.md"],
    ),
    c(
        "N09",
        "computable complex numbers",
        &["ccomplex.rs"],
        &["ccomplex103.tsv"],
        &["ccomplex"],
        &["COMPUTABLE_COMPLEX.md"],
    ),
    c(
        "N10",
        "elementary functions",
        &["elementary.rs"],
        &["elementary104.tsv"],
        &["creal"],
        &["ELEMENTARY_FUNCTIONS.md"],
    ),
    c(
        "N11",
        "explicit conversions",
        &["convert.rs"],
        &["convert105.tsv"],
        &["convert"],
        &["NUMERIC_CONVERSIONS.md"],
    ),
    c(
        "N12",
        "portable numeric texts",
        &["convert.rs"],
        &["convert105.tsv"],
        &["convert"],
        &["NUMERIC_CONVERSIONS.md"],
    ),
    c(
        "N13",
        "error and budget contracts",
        &["numeric_contract.rs"],
        &[],
        &["numeric"],
        &["NUMERIC_CONTRACTS.md"],
    ),
    c(
        "N14",
        "dense storage and cost profile",
        &["dense_profile.rs", "ir_verify.rs"],
        &[],
        &["dense", "exact"],
        &["DENSE_NUMBERS.md"],
    ),
    c(
        "N15",
        "scalar witnesses",
        &["scalar_witness.rs"],
        &[],
        &["scalar_witness"],
        &["SCALAR_WITNESSES.md"],
    ),
    c(
        "N16",
        "acceptance of the numeric section",
        &["numeric_acceptance.rs"],
        &[],
        &["dense", "complex_q"],
        &["NUMERIC_ACCEPTANCE.md"],
    ),
];

/// Environment switches of the acceptance run: when set to `1`, a missing
/// sanitizer toolchain or MinGW-w64/Wine is a failure instead of a skip.
pub const REQUIRE_SANITIZERS: &str = "DLM_REQUIRE_SANITIZERS";
pub const REQUIRE_WINDOWS: &str = "DLM_REQUIRE_WINDOWS";
