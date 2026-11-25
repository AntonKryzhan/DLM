use std::fmt::Write;
use std::path::PathBuf;

#[path = "src/math_dispatch/config.rs"]
mod config;

fn main() {
    println!("cargo:rerun-if-changed=src/math_dispatch/config.rs");
    let mut header = String::new();
    for (name, value) in [
        ("MATMUL_BLOCK_WORK", config::MATMUL_BLOCK_WORK),
        ("MATMUL_AVX2_WORK", config::MATMUL_AVX2_WORK),
        ("MATMUL_MIN_COLUMNS", config::MATMUL_MIN_COLUMNS),
        ("MATMUL_MIN_INNER", config::MATMUL_MIN_INNER),
        ("MATMUL_BLOCK_COLUMNS", config::MATMUL_BLOCK_COLUMNS),
        ("FFT_TWIDDLE_BLOCK", config::FFT_TWIDDLE_BLOCK),
        ("DFT_MAX", config::DFT_MAX),
        ("NTT_MODULUS", config::NTT_MODULUS),
        ("NTT_ROOT", config::NTT_ROOT),
        ("NTT_MAX", config::NTT_MAX),
        ("NTT_DIRECT_FACTOR", config::NTT_DIRECT_FACTOR),
    ] {
        writeln!(header, "#define DLM_MATH_{name} {value}u").unwrap();
    }
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    std::fs::write(out.join("math_dispatch.h"), header)
        .expect("write mathematical kernel configuration");
}
