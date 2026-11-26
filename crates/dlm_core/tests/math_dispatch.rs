use dlm_core::{
    build_c_executable_with_flags, default_c_compiler, emit_c, float, lower_program, math_dispatch,
    parse_module, spectral, CheckPolicy, Runtime,
};
use std::process::Command;

use math_dispatch::Algorithm;

#[test]
fn plans_follow_dimension_and_isa_boundaries() {
    let plan = math_dispatch::plan_matmul(2, 2, 2, true);
    assert_eq!(plan.algorithm, Algorithm::ScalarMatmul);
    assert_eq!(plan.work_units, 8);
    assert_eq!(
        math_dispatch::plan_matmul(1024, 1, 16, true).algorithm,
        Algorithm::ScalarMatmul
    );
    assert_eq!(
        math_dispatch::plan_matmul(64, 128, 49, false).algorithm,
        Algorithm::BlockedMatmul
    );
    assert_eq!(
        math_dispatch::plan_matmul(64, 128, 49, true).algorithm,
        Algorithm::Avx2Matmul
    );
    assert_eq!(
        math_dispatch::plan_fourier(128).unwrap().algorithm,
        Algorithm::Radix2Fft
    );
    assert_eq!(
        math_dispatch::plan_fourier(127).unwrap().algorithm,
        Algorithm::TableDft
    );
    assert!(math_dispatch::plan_fourier(spectral::DFT_MAX + 1).is_none());
    assert_eq!(
        math_dispatch::plan_real_convolution(33, 17)
            .unwrap()
            .transform_points,
        64
    );
    assert!(math_dispatch::plan_real_convolution(usize::MAX, 2).is_none());
    assert!(math_dispatch::plan_modular_convolution(math_dispatch::NTT_MAX, 2).is_none());
}

fn reference_matmul(a: &[f64], b: &[f64], n: usize, m: usize, k: usize) -> Vec<f64> {
    let mut out = vec![0.0; n * k];
    for i in 0..n {
        for j in 0..k {
            let mut s = 0.0;
            for l in 0..m {
                s += a[i * m + l] * b[l * k + j];
            }
            out[i * k + j] = s;
        }
    }
    out
}

#[test]
fn matrix_dispatch_preserves_per_cell_binary64_accumulation() {
    let (n, m, k) = (19, 53, 27);
    let mut a: Vec<_> = (0..n * m)
        .map(|i| (i as f64 % 73.0 - 20.25) / 37.0)
        .collect();
    let mut b: Vec<_> = (0..m * k)
        .map(|i| (i as f64 % 91.0 - 25.75) / 63.0)
        .collect();
    a[7] = -0.0;
    b[5] = f64::from_bits(1);
    for (a0, b0) in [
        (a[0], b[0]),
        (f64::MAX, f64::MAX),
        (f64::NAN, f64::INFINITY),
    ] {
        a[0] = a0;
        b[0] = b0;
        let expected = reference_matmul(&a, &b, n, m, k);
        let actual = float::matmul(&a, &b, n as u128, m as u128, k as u128).unwrap();
        for (x, y) in actual.into_iter().zip(expected) {
            assert!(
                x.to_bits() == y.to_bits() || (x.is_nan() && y.is_nan()),
                "{x:?} versus {y:?}"
            );
        }
    }
    assert_eq!(float::matmul(&[], &[], 3, 0, 5), Some(vec![0.0; 15]));
    assert_eq!(float::matmul(&[], &[], u128::MAX, 0, 0), Some(vec![]));
    assert_eq!(float::matmul(&a, &b, u128::MAX, 2, 1), None);
}

fn modular_oracle(a: &[u128], b: &[u128]) -> Vec<u128> {
    let p = u128::from(math_dispatch::NTT_MODULUS);
    let mut result = vec![0; a.len() + b.len() - 1];
    for (i, &x) in a.iter().enumerate() {
        for (j, &y) in b.iter().enumerate() {
            result[i + j] = (result[i + j] + (x % p) * (y % p)) % p;
        }
    }
    result
}

#[test]
fn modular_convolution_is_exact_on_both_routes() {
    assert_eq!(spectral::ntt_convolve(&[], &[1]), Some(Vec::new()));
    assert_eq!(spectral::ntt_convolve(&[0], &[u128::MAX]), Some(vec![0]));
    for (left, right, algorithm) in [
        (11, 13, Algorithm::DirectModularConvolution),
        (64, 64, Algorithm::DirectModularConvolution),
        (67, 63, Algorithm::DirectModularConvolution),
        (4, 8192, Algorithm::DirectModularConvolution),
        (256, 256, Algorithm::NttConvolution),
        (257, 199, Algorithm::NttConvolution),
    ] {
        let p = u128::from(math_dispatch::NTT_MODULUS);
        let a: Vec<_> = (0..left)
            .map(|n| u128::MAX - n as u128 * p + n as u128 % 2)
            .collect();
        let b: Vec<_> = (0..right)
            .map(|n| (n as u128 * 17 + 3) * p - n as u128 * 19)
            .collect();
        assert_eq!(
            math_dispatch::plan_modular_convolution(left, right)
                .unwrap()
                .algorithm,
            algorithm
        );
        assert_eq!(
            spectral::ntt_convolve(&a, &b).unwrap(),
            modular_oracle(&a, &b)
        );
    }
}

#[test]
fn typed_calls_agree_in_interpreter_and_c_cpu_profiles() {
    let program = dlm_core::Program::single(
        parse_module(
            r#"module test.dispatch
theory T {
    fn fv(n: Nat, x: Float) -> FloatVec = unwrap_or(fv_new(n, x), fv_empty())
    fn nv(n: Nat, x: Nat) -> NatVec = unwrap_or(nv_new(n, x), nv_empty())
    let a = unwrap_or(fv_set(fv(901, 0.27), 57, -1.73), fv_empty())
    let b = unwrap_or(fv_set(fv(986, -1.02), 19, 253.13), fv_empty())
    let c = unwrap_or(fv_matmul(a, b, 53, 17, 58), fv_empty())
    let x = unwrap_or(fv_set(fv(256, -0.13), 117, 3.892), fv_empty())
    let y = unwrap_or(fv_set(fv(130, 0.37), 25, -5.89), fv_empty())
    let p = unwrap_or(ntt_convolve(nv(256, 998244352), nv(256, 998244352)), nv_empty())
    print_line(fl_to_text(fv_sum(c)))
    print_line(nat_to_text(fv_len(c)))
    print_symbolic(unwrap_or(fft_forward(x), fv_empty()))
    print_symbolic(unwrap_or(fft_inverse(y), fv_empty()))
    print_line(nat_to_text(nv_len(p)))
    print_line(nat_to_text(unwrap_or(nv_get(p, 255), 0)))
    print_symbolic(unwrap_or(ntt_convolve(nv(2, 998244352), nv(2, 3)), nv_empty()))
}
"#,
        )
        .unwrap(),
    );
    let ir = lower_program(&program, CheckPolicy::trusted_only()).unwrap();
    assert!(ir.render().contains("ntt_convolve("));
    let reference = Runtime::new().run_program(&program).unwrap().output;
    assert_eq!(
        &reference[4..],
        &["511", "256", "nvec[998244350, 998244347, 998244350]"]
    );
    assert_eq!(reference[1], "3074");

    let dir = std::env::temp_dir().join(format!("dlm-math-dispatch-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let c_source = emit_c(&ir);
    for (profile, flags) in [
        ("auto", &[][..]),
        ("scalar", &["-DDN_CPU_PROFILE=1"][..]),
        ("sse2", &["-DDN_CPU_PROFILE=2"][..]),
    ] {
        let exe = build_c_executable_with_flags(
            &c_source,
            &dir.join(format!("{profile}.c")),
            &dir.join(profile),
            &default_c_compiler(),
            flags,
        )
        .unwrap();
        let result = Command::new(exe).output().unwrap();
        assert!(
            result.status.success(),
            "{profile}: {}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(
            String::from_utf8(result.stdout)
                .unwrap()
                .lines()
                .collect::<Vec<_>>(),
            reference
        );
    }
    std::fs::remove_dir_all(dir).ok();
}
