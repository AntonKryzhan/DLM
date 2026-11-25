use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, float, linalg, lower_program,
    math_compute as math, parse_module, spectral, CheckPolicy, Program, Runtime,
};
use std::process::Command;

#[test]
fn certificate_checks_positivity_residual_and_exact_coefficient_range() {
    let a = [1.0, 2.0, 2.0, 5.0];
    let positive = math::solve_certified(&a, &[5.0, 12.0], 2, 1e-10).unwrap();
    assert!(positive.cholesky);
    for (x, exact) in positive.values.iter().zip([1.0, 2.0]) {
        assert!((x - exact).abs() <= positive.error);
    }

    let nonsymmetric =
        math::solve_certified(&[3.0, 1.0, 2.0, 4.0], &[5.0, 10.0], 2, 1e-10).unwrap();
    assert!(!nonsymmetric.cholesky);
    assert_eq!(nonsymmetric.values, [1.0, 2.0]);
    assert!(math::solve_certified(&[1.0, 2.0, 2.0, 1.0], &[1.0, 2.0], 2, 1e-10).is_none());
    assert!(math::solve_certified(&[1.0, 1.0, 1.0, 1.0], &[1.0, 1.0], 2, 1e-10).is_none());
    assert!(math::solve_certified(&[3.0], &[1.0], 1, 0.0).is_none());
    assert_eq!(
        math::solve_certified(&[1.0], &[-1.0], 1, 0.0)
            .unwrap()
            .error,
        0.0
    );
    assert_eq!(
        math::convolve_integer(&[-6.0, 8.0, 2.0], &[3.0, -4.0]).unwrap(),
        [-18.0, 48.0, -26.0, -8.0],
    );
    assert!(math::convolve_integer(&[499_122_176.0], &[2.0]).is_none());
    assert!(math::convolve_integer(&[1.25], &[1.0]).is_none());
    assert!(math::convolve_integer(&[f64::NAN], &[1.0]).is_none());
}

#[test]
fn empty_invalid_and_strict_fallback_are_distinguished() {
    let empty = math::matmul_bounded(&[], &[], u128::MAX, 0, 0, 0.0).unwrap();
    assert!(empty.values.is_empty());
    assert!(!empty.vulkan);
    let zero = math::matmul_bounded(&[], &[], 2, 0, 3, 0.01).unwrap();
    assert_eq!(zero.values, [0.0; 6]);
    assert!(!zero.vulkan);
    assert!(math::matmul_bounded(&[1.0], &[1.0], 2, 1, 1, 1.0).is_none());
    assert!(math::matmul_bounded(&[f64::MAX], &[f64::MAX], 1, 1, 1, 1.0).is_none());
    assert!(math::fft_bounded(&[1.0, 0.0], false, -1.0).is_none());
    assert!(math::fft_bounded(&[1.0], false, 1.0).is_none());
    assert!(math::fft_bounded(&[f64::NAN, 1.0], false, 1.0).is_none());
    assert!(math::csr_bounded(&[0, 2, 1], &[0], &[1.0], &[1.0], 1.0).is_none());
    assert!(math::csr_bounded(&[0, 1], &[1], &[1.0], &[1.0], 1.0).is_none());
    let input = [3.0, 0.0, -2.0, 1.0, 0.0, 5.0];
    let y = math::fft_bounded(&input, true, 1.0).unwrap();
    assert_eq!(y.values, spectral::fft(&input, true).unwrap());
    assert_eq!(y.error, 0.0);
    assert!(!y.vulkan);
    let zero_tol = math::fft_bounded(&input[..4], false, 0.0).unwrap();
    assert_eq!(zero_tol.values, spectral::fft(&input[..4], false).unwrap());
    assert_eq!(zero_tol.error, 0.0);
    assert!(!zero_tol.vulkan);
}

#[test]
fn parallel_sparse_cpu_keeps_the_order_inside_each_row() {
    let mut row = vec![0];
    let mut columns = vec![];
    let mut values = vec![];
    let vector: Vec<_> = (0..256).map(|i| (f64::from(i) - 127.0) / 63.0).collect();
    let expected: Vec<_> = (0..1025)
        .map(|i| {
            let mut sum = 0.0;
            if i % 8 != 0 {
                for j in 0..(i * 17) % 1024 {
                    let column = (j * 33 + i) % vector.len();
                    let value = (j as f64 % 7.0 - 2.0) / 5.0;
                    columns.push(column as u128);
                    values.push(value);
                    sum += value * vector[column];
                }
            }
            row.push(values.len() as u128);
            f64::to_bits(sum)
        })
        .collect();
    assert!(values.len() > (1 << 18));
    let actual = linalg::csr_matvec(&row, &columns, &values, &vector).unwrap();
    assert_eq!(
        actual.into_iter().map(f64::to_bits).collect::<Vec<_>>(),
        expected
    );
}

fn checked_program() -> Program {
    Program::single(parse_module(
        r#"module test.compute
theory T {
    fn fv(x: List<Float>) -> FloatVec = unwrap_or(fv_from_list(x), fv_empty())
    fn nv(x: List<Nat>) -> NatVec = unwrap_or(nv_from_list(x), nv_empty())
    let a = fv(list(1.0, 2.0, 3.0, 4.0, 5.0, 6.0))
    let b = fv(list(7.0, 8.0, 9.0, 10.0, 11.0, 12.0))
    print_symbolic(fv_matmul_bounded(a, b, 2, 3, 2, 0.01))
    print_symbolic(fft_bounded(fv(list(1.0, 0.0, 2.0, 0.0, 3.0, 0.0, 4.0, 0.0)), true, 0.01))
    print_symbolic(fft_convolve_bounded(fv(list(1.0, 2.0)), fv(list(3.0, -4.0)), 0.01))
    print_symbolic(la_csr_matvec_bounded(nv(list(0, 2, 2, 3)), nv(list(0, 2, 1)), fv(list(3.0, 2.0, -4.0)), fv(list(1.0, 5.0, 7.0)), 0.01))
    print_symbolic(ntt_convolve_accel(nv(list(998244352, 2)), nv(list(3, 4))))
    print_symbolic(la_solve_certified(fv(list(1.0, 2.0, 2.0, 5.0)), fv(list(5.0, 12.0)), 2, 0.0000000001))
    print_symbolic(fft_convolve_integer(fv(list(-6.0, 8.0, 2.0)), fv(list(3.0, -4.0))))
    print_symbolic(la_solve_certified(fv(list(1.0, 2.0, 2.0, 1.0)), fv(list(1.0, 2.0)), 2, 0.01))
    print_symbolic(fv_matmul_bounded(a, b, 2, 0, 2, 0.0))
}
"#,
    ).unwrap())
}

#[test]
fn typed_compute_and_certificates_agree_with_native_c() {
    let program = checked_program();
    let ir = lower_program(&program, CheckPolicy::trusted_only()).unwrap();
    let reference = Runtime::new().run_program(&program).unwrap().output;
    let dir = std::env::temp_dir().join(format!("dlm-math-compute-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("compute.c"),
        &dir.join("compute"),
        &default_c_compiler(),
    )
    .unwrap();
    let result = Command::new(&exe).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        String::from_utf8(result.stdout)
            .unwrap()
            .lines()
            .collect::<Vec<_>>(),
        reference
    );
    assert!(reference[5].contains("true"));
    assert!(reference[6].contains("-26.0"));
    assert_eq!(&reference[7..], &["none", "none"]);
    std::fs::remove_dir_all(dir).ok();
}

fn compare(result: math::BoundedVector, strict: Vec<f64>, tolerance: f64) {
    assert!(result.vulkan, "the test requires actual Vulkan execution");
    assert!((0.0..=tolerance).contains(&result.error));
    assert_eq!(result.values.len(), strict.len());
    for (a, b) in result.values.into_iter().zip(strict) {
        assert!(
            (a - b).abs() <= result.error,
            "{a} vs {b}, bound {}",
            result.error
        );
    }
}

#[test]
#[ignore = "requires Vulkan with FP64 and Int64 and DLM_MATH_BACKEND=vulkan"]
fn vulkan_compute_backends_execute_with_checked_readback() {
    let (n, m, k) = (19, 53, 27);
    let a: Vec<_> = (0..n * m)
        .map(|i| (i as f64 % 53.0 - 20.25) / 37.0)
        .collect();
    let b: Vec<_> = (0..m * k)
        .map(|i| (i as f64 % 71.0 - 25.75) / 63.0)
        .collect();
    let strict = float::matmul(&a, &b, n as u128, m as u128, k as u128).unwrap();
    for tolerance in [1e-2, 1e-9] {
        let y = math::matmul_bounded(&a, &b, n as u128, m as u128, k as u128, tolerance).unwrap();
        compare(y, strict.clone(), tolerance);
    }
    let input: Vec<_> = (0..256)
        .map(|i| (f64::from(i % 41) - 15.0) / 37.0)
        .collect();
    for inverse in [false, true] {
        for tolerance in [0.1, 1e-9] {
            compare(
                math::fft_bounded(&input, inverse, tolerance).unwrap(),
                spectral::fft(&input, inverse).unwrap(),
                tolerance,
            );
        }
    }
    let (a, b) = (&input[..21], &input[21..56]);
    compare(
        math::convolve_bounded(a, b, 1e-6).unwrap(),
        spectral::convolve(a, b).unwrap(),
        1e-6,
    );
    let (row, column, value, x) = ([0, 2, 2, 3], [0, 2, 1], [3.0, 2.0, -4.0], [1.0, 5.0, 7.0]);
    compare(
        math::csr_bounded(&row, &column, &value, &x, 1e-5).unwrap(),
        linalg::csr_matvec(&row, &column, &value, &x).unwrap(),
        1e-5,
    );
    let a: Vec<_> = (0..257).map(|n| u128::MAX - n * 171).collect();
    let b: Vec<_> = (0..123).map(|n| n * n * 1_003_000_542).collect();
    let (actual, vulkan) = math::ntt_accel(&a, &b).unwrap();
    assert!(vulkan);
    assert_eq!(actual, spectral::ntt_convolve(&a, &b).unwrap());
}
