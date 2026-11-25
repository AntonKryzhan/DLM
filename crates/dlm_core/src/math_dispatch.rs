//! Execution plans for the dense numerical builtins shared by the interpreter and C backend.
//! CPU implementations preserve each output's accumulation order and use separate multiply/add.

use crate::backend_capability::BackendCapability;
use crate::ordinary::OrdinaryType;
use crate::prelude_lowering::PreludeLoweringTarget;
use std::fmt;

pub(crate) mod config;
pub mod vulkan;

pub const NTT_MODULUS: u32 = config::NTT_MODULUS as u32;
pub const NTT_MAX: usize = config::NTT_MAX;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MathOperation {
    Matmul,
    Fourier,
    RealConvolution,
    ModularConvolution,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NumericalContract {
    OrderedBinary64,
    ExactModular,
}

impl fmt::Display for NumericalContract {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::OrderedBinary64 => f.write_str("strict binary64"),
            Self::ExactModular => write!(f, "exact modulo {NTT_MODULUS}"),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Algorithm {
    ScalarMatmul,
    BlockedMatmul,
    Avx2Matmul,
    Radix2Fft,
    TableDft,
    FftConvolution,
    DirectModularConvolution,
    NttConvolution,
}

#[derive(Clone, Copy, Debug)]
pub struct Registration {
    pub algorithm: Algorithm,
    pub operation: MathOperation,
    pub name: &'static str,
    pub target: PreludeLoweringTarget,
    pub capabilities: &'static [BackendCapability],
    pub numerical_contract: NumericalContract,
    pub requirements: &'static str,
    pub complexity: &'static str,
}

use Algorithm::*;
use BackendCapability::{Deterministic, Pure, ValuePreserving, Vectorizable};
use MathOperation::*;
use NumericalContract::{ExactModular, OrderedBinary64};
use PreludeLoweringTarget::{NativeScalar, NativeVector};

const SCALAR: &[BackendCapability] = &[Deterministic, Pure, ValuePreserving];
const VECTOR: &[BackendCapability] = &[Deterministic, Pure, ValuePreserving, Vectorizable];

pub const ALGORITHMS: &[Registration] = &[
    Registration {
        algorithm: ScalarMatmul,
        operation: Matmul,
        name: "scalar-matmul",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: OrderedBinary64,
        requirements: "row-major dense matrices",
        complexity: "rows * inner * columns",
    },
    Registration {
        algorithm: BlockedMatmul,
        operation: Matmul,
        name: "blocked-matmul",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: OrderedBinary64,
        requirements: "finite row-major dense matrices",
        complexity: "rows * inner * columns",
    },
    Registration {
        algorithm: Avx2Matmul,
        operation: Matmul,
        name: "avx2-matmul",
        target: NativeVector,
        capabilities: VECTOR,
        numerical_contract: OrderedBinary64,
        requirements: "CPU with AVX2, finite row-major dense matrices",
        complexity: "rows * inner * columns",
    },
    Registration {
        algorithm: Radix2Fft,
        operation: Fourier,
        name: "radix2-fft",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: OrderedBinary64,
        requirements: "empty or power-of-two interleaved complex vector",
        complexity: "n log2(n)",
    },
    Registration {
        algorithm: TableDft,
        operation: Fourier,
        name: "table-dft",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: OrderedBinary64,
        requirements: "non-power-of-two interleaved complex vector within DFT limit",
        complexity: "n²",
    },
    Registration {
        algorithm: FftConvolution,
        operation: RealConvolution,
        name: "fft-convolution",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: OrderedBinary64,
        requirements: "real vectors with power-of-two padding",
        complexity: "n log2(n) for padded length n",
    },
    Registration {
        algorithm: DirectModularConvolution,
        operation: ModularConvolution,
        name: "direct-modular-convolution",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: ExactModular,
        requirements: "Nat coefficients reduced modulo the registered prime",
        complexity: "left * right",
    },
    Registration {
        algorithm: NttConvolution,
        operation: ModularConvolution,
        name: "radix2-ntt-convolution",
        target: NativeScalar,
        capabilities: SCALAR,
        numerical_contract: ExactModular,
        requirements: "Nat coefficients and padding within the prime's radix-2 order",
        complexity: "n log2(n) for padded length n",
    },
];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MathPlan {
    pub algorithm: Algorithm,
    pub work_units: u128,
    /// Points in the transform, including padding when needed.
    pub transform_points: usize,
}

impl MathPlan {
    pub fn registration(self) -> &'static Registration {
        ALGORITHMS
            .iter()
            .find(|r| r.algorithm == self.algorithm)
            .expect("registered kernel")
    }
}

impl fmt::Display for MathPlan {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let r = self.registration();
        write!(f, "algorithm: {}\ntarget: {}\nnumerical contract: {}\nrequirements: {}\ncomplexity: {}\nwork units: {}",
            r.name, r.target, r.numerical_contract, r.requirements, r.complexity, self.work_units)?;
        if self.transform_points != 0 {
            write!(f, "\ntransform points: {}", self.transform_points)?;
        }
        Ok(())
    }
}

pub fn host_avx2() -> bool {
    #[cfg(target_arch = "x86_64")]
    {
        std::is_x86_feature_detected!("avx2")
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        false
    }
}

pub fn plan_matmul(rows: usize, inner: usize, columns: usize, avx2: bool) -> MathPlan {
    let work_units = (rows as u128)
        .saturating_mul(inner as u128)
        .saturating_mul(columns as u128);
    let wide_enough = columns >= config::MATMUL_MIN_COLUMNS && inner >= config::MATMUL_MIN_INNER;
    let algorithm = if wide_enough && avx2 && work_units >= config::MATMUL_AVX2_WORK as u128 {
        Avx2Matmul
    } else if wide_enough && work_units >= config::MATMUL_BLOCK_WORK as u128 {
        BlockedMatmul
    } else {
        ScalarMatmul
    };
    MathPlan {
        algorithm,
        work_units,
        transform_points: 0,
    }
}

pub fn plan_fourier(points: usize) -> Option<MathPlan> {
    let (algorithm, work_units) = if points == 0 || points.is_power_of_two() {
        (Radix2Fft, butterflies(points))
    } else if points <= config::DFT_MAX {
        (TableDft, (points as u128).pow(2))
    } else {
        return None;
    };
    Some(MathPlan {
        algorithm,
        work_units,
        transform_points: points,
    })
}

pub fn plan_real_convolution(left: usize, right: usize) -> Option<MathPlan> {
    let len = convolution_len(left, right)?;
    let points = padded_points(len, crate::float::VEC_MAX / 2)?;
    Some(MathPlan {
        algorithm: FftConvolution,
        work_units: 3 * butterflies(points) + points as u128,
        transform_points: points,
    })
}

pub fn plan_modular_convolution(left: usize, right: usize) -> Option<MathPlan> {
    let len = convolution_len(left, right)?;
    let points = padded_points(len, NTT_MAX)?;
    let work = (left as u128) * (right as u128);
    let transform_work = 3 * butterflies(points) + points as u128;
    if work <= transform_work * config::NTT_DIRECT_FACTOR as u128 {
        return Some(MathPlan {
            algorithm: DirectModularConvolution,
            work_units: work,
            transform_points: 0,
        });
    }
    Some(MathPlan {
        algorithm: NttConvolution,
        work_units: transform_work,
        transform_points: points,
    })
}

pub(crate) fn convolution_len(left: usize, right: usize) -> Option<usize> {
    if left == 0 || right == 0 {
        return Some(0);
    }
    left.checked_add(right - 1)
}

fn padded_points(len: usize, limit: usize) -> Option<usize> {
    if len == 0 {
        return Some(0);
    }
    len.checked_next_power_of_two().filter(|n| *n <= limit)
}

fn butterflies(points: usize) -> u128 {
    (points / 2) as u128 * points.checked_ilog2().unwrap_or(0) as u128
}

/// Shape-dependent kernels have the same typed call boundary for all implementations.
pub(crate) fn builtin_signature(name: &str) -> Option<(Vec<OrdinaryType>, OrdinaryType)> {
    use OrdinaryType::{FloatVec, Nat, NatVec, Option};
    let (parameters, elements) = match name {
        "fv_matmul" => (vec![FloatVec, FloatVec, Nat, Nat, Nat], FloatVec),
        "fft_forward" | "fft_inverse" => (vec![FloatVec], FloatVec),
        "fft_convolve" => (vec![FloatVec, FloatVec], FloatVec),
        "fft2_forward" | "fft2_inverse" => (vec![FloatVec, Nat, Nat], FloatVec),
        "ntt_convolve" => (vec![NatVec, NatVec], NatVec),
        _ => return None,
    };
    Some((parameters, Option(Box::new(elements))))
}
