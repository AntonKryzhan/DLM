use crate::{float, linalg, math_dispatch, spectral};
use std::f64::consts::TAU;
use std::ptr;

mod proof;
pub use proof::{solve_certified, CertifiedSolution};

#[derive(Debug)]
pub struct BoundedVector {
    pub values: Vec<f64>,
    /// Upper bound on the componentwise absolute difference from strict DLM.
    pub error: f64,
    /// True if the result was read back from Vulkan.
    pub vulkan: bool,
}

#[repr(u32)]
enum Operation {
    Matrix = 1,
    Fft,
    Convolution,
    Csr,
}

#[repr(C)]
struct FloatJob {
    kind: Operation,
    n: u32,
    inner: u32,
    columns: u32,
    inverse: u32,
    left: *const f64,
    right: *const f64,
    factors: *const f64,
    layout: *const u32,
    left_count: usize,
    right_count: usize,
    output_count: usize,
    tolerance: f64,
}

#[cfg(dlm_math_compute)]
extern "C" {
    fn dlm_math_should_try(work: u64) -> std::ffi::c_int;
    fn dlm_math_should_try_ntt(a: usize, b: usize) -> std::ffi::c_int;
    fn dlm_math_float_gpu(job: *const FloatJob, out: *mut f64, bound: *mut f64) -> std::ffi::c_int;
    fn dlm_math_ntt_gpu(
        a: *const u32,
        b: *const u32,
        left: usize,
        right: usize,
        out: *mut u32,
    ) -> std::ffi::c_int;
    fn dlm_math_csr_parallel(
        row: *const u128,
        columns: *const u128,
        values: *const f64,
        vector: *const f64,
        rows: usize,
        out: *mut f64,
    ) -> std::ffi::c_int;
}

pub const VULKAN_EXECUTOR_COMPILED: bool = cfg!(dlm_math_compute);

fn try_gpu(work: u64) -> bool {
    #[cfg(dlm_math_compute)]
    // SAFETY: this function only inspects the backend policy and work estimate.
    return unsafe { dlm_math_should_try(work) != 0 };
    #[cfg(not(dlm_math_compute))]
    {
        let _ = work;
        false
    }
}

fn eval_gpu(
    kind: Operation,
    [n, inner, columns]: [usize; 3],
    inverse: bool,
    [left, right]: [&[f64]; 2],
    factors: &[f64],
    layout: &[u32],
    output_count: usize,
    tolerance: f64,
) -> Option<BoundedVector> {
    let job = FloatJob {
        kind,
        n: n.try_into().ok()?,
        inner: inner.try_into().ok()?,
        columns: columns.try_into().ok()?,
        inverse: u32::from(inverse),
        left: left.as_ptr(),
        right: right.as_ptr(),
        factors: if factors.is_empty() {
            ptr::null()
        } else {
            factors.as_ptr()
        },
        layout: layout.as_ptr(),
        left_count: left.len(),
        right_count: right.len(),
        output_count,
        tolerance,
    };
    #[cfg(dlm_math_compute)]
    {
        let mut values = Vec::new();
        values.try_reserve_exact(output_count).ok()?;
        let mut error = 0.0;
        // SAFETY: the inputs stay live until the synchronous job completes.
        // The wrappers establish the shapes. Success initializes the output.
        if unsafe { dlm_math_float_gpu(&job, values.as_mut_ptr(), &mut error) } == 0 {
            return None;
        }
        unsafe { values.set_len(output_count) };
        Some(BoundedVector {
            values,
            error,
            vulkan: true,
        })
    }
    #[cfg(not(dlm_math_compute))]
    {
        let _ = job;
        None
    }
}

pub(super) fn valid_tolerance(tolerance: f64) -> bool {
    tolerance.is_finite() && tolerance >= 0.0
}

fn finite(x: &[f64]) -> bool {
    x.iter().all(|v| v.is_finite())
}

fn strict(values: Vec<f64>) -> Option<BoundedVector> {
    finite(&values).then_some(BoundedVector {
        values,
        error: 0.0,
        vulkan: false,
    })
}

pub fn matmul_bounded(
    a: &[f64],
    b: &[f64],
    n: u128,
    m: u128,
    k: u128,
    tolerance: f64,
) -> Option<BoundedVector> {
    let len = n.checked_mul(k)?;
    if n.checked_mul(m)? != a.len() as u128
        || m.checked_mul(k)? != b.len() as u128
        || len > float::VEC_MAX as u128
        || !valid_tolerance(tolerance)
        || !finite(a)
        || !finite(b)
    {
        return None;
    }
    if tolerance > 0.0 && len != 0 && m != 0 && try_gpu((len * m) as u64) {
        let dims = [n as usize, m as usize, k as usize];
        if let Some(result) = eval_gpu(
            Operation::Matrix,
            dims,
            false,
            [a, b],
            &[],
            &[],
            len as usize,
            tolerance,
        ) {
            return Some(result);
        }
    }
    strict(float::matmul(a, b, n, m, k)?)
}

fn append_factors(factors: &mut Vec<f64>, n: usize, inverse: bool) {
    let sign = if inverse { 1.0 } else { -1.0 };
    let mut step = 2;
    while step <= n {
        let angle = sign * TAU / (step as f64);
        for j in 0..step / 2 {
            let t = angle * (j as f64);
            factors.extend([t.cos(), t.sin()]);
        }
        step *= 2;
    }
    factors.extend([1.0 / n as f64, 0.0]);
}

pub fn fft_bounded(input: &[f64], inverse: bool, tolerance: f64) -> Option<BoundedVector> {
    if input.len() % 2 != 0
        || input.len() > float::VEC_MAX
        || !valid_tolerance(tolerance)
        || !finite(input)
    {
        return None;
    }
    let n = input.len() / 2;
    if tolerance > 0.0 && n > 1 && n.is_power_of_two() && try_gpu((n as u64) * n.ilog2() as u64) {
        let mut factors = Vec::with_capacity(input.len());
        append_factors(&mut factors, n, inverse);
        if let Some(result) = eval_gpu(
            Operation::Fft,
            [n, 0, 0],
            inverse,
            [input, &[]],
            &factors,
            &[],
            input.len(),
            tolerance,
        ) {
            return Some(result);
        }
    }
    strict(spectral::fft(input, inverse)?)
}

pub fn convolve_bounded(a: &[f64], b: &[f64], tolerance: f64) -> Option<BoundedVector> {
    if !valid_tolerance(tolerance) || !finite(a) || !finite(b) {
        return None;
    }
    let len = math_dispatch::convolution_len(a.len(), b.len())?;
    let n = math_dispatch::plan_real_convolution(a.len(), b.len())?.transform_points;
    if tolerance > 0.0 && n > 1 && try_gpu(3 * (n as u64) * n.ilog2() as u64) {
        let interleave = |v: &[f64]| v.iter().flat_map(|x| [*x, 0.0]).collect::<Vec<_>>();
        let mut factors = Vec::with_capacity(4 * n);
        append_factors(&mut factors, n, false);
        append_factors(&mut factors, n, true);
        if let Some(mut result) = eval_gpu(
            Operation::Convolution,
            [n, 0, 0],
            false,
            [&interleave(a), &interleave(b)],
            &factors,
            &[],
            2 * len,
            tolerance,
        ) {
            for i in 0..len {
                result.values[i] = result.values[2 * i];
            }
            result.values.truncate(len);
            return Some(result);
        }
    }
    strict(spectral::convolve(a, b)?)
}

pub fn csr_bounded(
    row: &[u128],
    columns: &[u128],
    values: &[f64],
    vector: &[f64],
    tolerance: f64,
) -> Option<BoundedVector> {
    if !valid_tolerance(tolerance) || !finite(values) || !finite(vector) {
        return None;
    }
    let n = linalg::csr(row, columns, values, vector.len())?;
    if tolerance > 0.0 && n > 0 && !values.is_empty() && try_gpu(values.len() as u64) {
        let layout: Option<Vec<_>> = row
            .iter()
            .chain(columns)
            .map(|&x| u32::try_from(x).ok())
            .collect();
        let width = row.windows(2).map(|w| w[1] - w[0]).max().unwrap_or(0) as usize;
        if let Some(result) = layout.and_then(|layout| {
            eval_gpu(
                Operation::Csr,
                [n, width, vector.len()],
                false,
                [values, vector],
                &[],
                &layout,
                n,
                tolerance,
            )
        }) {
            return Some(result);
        }
    }
    strict(csr_matvec_validated(row, columns, values, vector, n))
}

pub(crate) fn csr_matvec_validated(
    row: &[u128],
    columns: &[u128],
    values: &[f64],
    vector: &[f64],
    rows: usize,
) -> Vec<f64> {
    #[cfg(dlm_math_compute)]
    if rows >= math_dispatch::config::CSR_PARALLEL_ROWS
        && values.len() >= math_dispatch::config::CSR_PARALLEL_NNZ
        && finite(values)
        && finite(vector)
    {
        let mut out = vec![0.0; rows];
        // SAFETY: CSR extents were validated by the caller and every worker
        // writes a disjoint range, joining before the inputs leave this scope.
        let computed = unsafe {
            dlm_math_csr_parallel(
                row.as_ptr(),
                columns.as_ptr(),
                values.as_ptr(),
                vector.as_ptr(),
                rows,
                out.as_mut_ptr(),
            )
        };
        if computed != 0 && finite(&out) {
            return out;
        }
    }
    linalg::spmv(row, columns, values, vector, rows)
}

/// Exact convolution in the field of order 998244353. The bool records Vulkan execution.
pub fn ntt_accel(a: &[u128], b: &[u128]) -> Option<(Vec<u128>, bool)> {
    #[cfg(dlm_math_compute)]
    {
        use math_dispatch::NTT_MODULUS;
        // SAFETY: this admission check only receives the operand lengths.
        if unsafe { dlm_math_should_try_ntt(a.len(), b.len()) } != 0 {
            let len = math_dispatch::convolution_len(a.len(), b.len())?;
            let residue = |v: &[u128]| {
                v.iter()
                    .map(|x| (x % u128::from(NTT_MODULUS)) as u32)
                    .collect::<Vec<_>>()
            };
            let (left, right) = (residue(a), residue(b));
            let mut out = Vec::new();
            // SAFETY: the inputs are normalized residues; output has space for
            // left.len() + right.len() - 1 elements and is initialized on success.
            if out.try_reserve_exact(len).is_ok()
                && unsafe {
                    dlm_math_ntt_gpu(
                        left.as_ptr(),
                        right.as_ptr(),
                        left.len(),
                        right.len(),
                        out.as_mut_ptr(),
                    )
                } != 0
            {
                unsafe { out.set_len(len) };
                return Some((out.into_iter().map(u128::from).collect(), true));
            }
        }
    }
    Some((spectral::ntt_convolve(a, b)?, false))
}

/// Certify that conversion to and from the NTT field is injective over the
/// range of every signed coefficient of this convolution.
pub fn convolve_integer(a: &[f64], b: &[f64]) -> Option<Vec<f64>> {
    const P: i64 = math_dispatch::NTT_MODULUS as i64;
    const LIMIT: i64 = P / 2;
    if a.is_empty() || b.is_empty() {
        return Some(Vec::new());
    }
    math_dispatch::plan_modular_convolution(a.len(), b.len())?;
    let max = |input: &[f64]| {
        input.iter().try_fold(0i64, |bound, &x| {
            (x.abs() <= LIMIT as f64 && x == x.trunc()).then(|| bound.max((x as i64).abs()))
        })
    };
    let bound = a.len().min(b.len()) as u128 * max(a)? as u128 * max(b)? as u128;
    if bound > LIMIT as u128 {
        return None;
    }
    let encode = |v: &[f64]| {
        v.iter()
            .map(|x| (*x as i64).rem_euclid(P) as u128)
            .collect::<Vec<_>>()
    };
    // Uses the pure exact dispatcher and is suitable for ordinary evaluation.
    let coeffs = spectral::ntt_convolve(&encode(a), &encode(b))?;
    Some(
        coeffs
            .into_iter()
            .map(|x| {
                let x = x as i64;
                (if x > LIMIT { x - P } else { x }) as f64
            })
            .collect(),
    )
}
