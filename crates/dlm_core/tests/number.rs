//! v0.118 — number theory (`nt_*`), polynomials over prime fields (`pf_*`),
//! permutation groups (`pm_*`, Schreier–Sims) and `std.number_theory`
//! (modular arithmetic, GF(p^k), RSA, primitive roots, classical groups).
//! Exact results: interpreter and native C print identical lines.
use dlm_core::number as nt;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-number-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let native: Vec<String> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const BUILTINS: &str = r#"    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn ob(x: Option<Bool>) -> Text = match x { none => "none", some(v) => if_then_else(v, "true", "false") }
    fn b(x: Bool) -> Text = if_then_else(x, "true", "false")
    print_line(nat_to_text(nt_gcd(462, 1071)))
    print_line(on(nt_lcm(4, 6)))
    print_line(on(nt_lcm(340282366920938463463374607431768211455, 2)))
    print_line(on(nt_mod_mul(340282366920938463463374607431768211455, 340282366920938463463374607431768211454, 340282366920938463463374607431768211453)))
    print_line(on(nt_mod_pow(2, 1000, 1000000007)))
    print_line(on(nt_mod_pow(3, 340282366920938463463374607431768211454, 340282366920938463463374607431768211297)))
    print_line(on(nt_mod_inv(3, 11)))
    print_line(on(nt_mod_inv(6, 9)))
    print_line(b(nt_is_prime(170141183460469231731687303715884105727)))
    print_line(b(nt_is_prime(561)))
    print_line(b(nt_is_prime(340282366920938463463374607431768211297)))
    print_line(on(nt_next_prime(1000000000000)))
    print_line(op(nt_factor(600851475143)))
    print_line(op(nt_factor(998244359987710471)))
    print_line(op(nt_factor(1298074218846574320324160893485707)))
    print_line(op(nt_factor(1)))
    print_line(op(nt_factor(0)))
    print_line(nat_to_text(nt_totient(36)))
    print_line(op(nt_divisors(360)))
    print_line(op(nt_primes(50)))
    print_line(on(nt_crt(nv(list(2, 3, 2)), nv(list(3, 5, 7)))))
    print_line(on(nt_crt(nv(list(1, 2)), nv(list(4, 6)))))
    print_line(on(nt_crt(nv(list(3, 5)), nv(list(4, 6)))))
    print_line(on(nt_sqrt_mod(10, 13)))
    print_line(on(nt_sqrt_mod(2, 1000000007)))
    print_line(on(nt_sqrt_mod(5, 1000000007)))
    print_line(nat_to_text(nt_isqrt(340282366920938463463374607431768211455)))
    print_line(op(pf_mul(nv(list(1, 1)), nv(list(1, 1)), 2)))
    print_line(op(pf_add(nv(list(1, 2, 3)), nv(list(4, 5)), 7)))
    print_line(op(pf_sub(nv(list(1, 2)), nv(list(1, 2)), 7)))
    print_line(op(pf_div(nv(list(6, 5, 1)), nv(list(2, 1)), 7)))
    print_line(op(pf_rem(nv(list(1, 0, 0, 1)), nv(list(1, 1, 1)), 3)))
    print_line(op(pf_gcd(nv(list(6, 5, 1)), nv(list(3, 4, 1)), 7)))
    print_line(op(pf_powmod(nv(list(0, 1)), 8, nv(list(1, 1, 0, 1)), 2)))
    print_line(on(pf_eval(nv(list(1, 2, 3)), 10, 1000000007)))
    print_line(ob(pf_irreducible(nv(list(1, 1, 1)), 2)))
    print_line(ob(pf_irreducible(nv(list(1, 0, 1)), 2)))
    print_line(ob(pf_irreducible(nv(list(1, 1, 0, 0, 1)), 2)))
    print_line(ob(pf_irreducible(nv(list(2, 0, 1)), 5)))
    print_line(ob(pf_irreducible(nv(list(1, 0, 1)), 4)))
    print_line(op(pm_compose(nv(list(1, 2, 0)), nv(list(0, 2, 1)))))
    print_line(op(pm_inverse(nv(list(1, 2, 0)))))
    print_line(op(pm_power(nv(list(1, 2, 3, 4, 0)), 7)))
    print_line(on(pm_order(nv(list(1, 0, 3, 4, 2)))))
    print_line(on(pm_sign(nv(list(1, 0, 3, 4, 2)))))
    print_line(op(pm_cycle_type(nv(list(1, 0, 3, 4, 2, 5)))))
    print_line(op(pm_compose(nv(list(1, 1)), nv(list(0, 1)))))
    print_line(op(pm_orbit(nv(list(1, 0, 2, 3, 0, 1, 3, 2)), 4, 0)))
    print_line(on(pm_group_order(nv(list(1, 2, 3, 4, 0, 1, 0, 2, 3, 4)), 5)))
    print_line(on(pm_group_order(nv(list(1, 2, 0, 3, 4, 0, 1, 3, 4, 2)), 5)))
    print_line(on(pm_group_order(nv(list(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19)), 20)))
    print_line(on(pm_group_order(nv(list(1, 2, 3, 0, 3, 2, 1, 0)), 4)))
    print_line(on(pm_group_order(nv_empty(), 3)))
"#;

#[test]
fn number_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.number\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &[
        "21",
        "12",
        "none",
        "2",
        "688423210",
        "215768107020904335039990585066521651668",
        "4",
        "none",
        "true",
        "false",
        "true",
        "1000000000039",
        "nvec[71, 839, 1471, 6857]",
        "nvec[71, 839, 1471, 6857]",
        "nvec[998244353, 1000000007]",
        "nvec[998244353, 1000000007]",
        "nvec[1073741827, 68719476767, 17592186044423]",
        "nvec[1073741827, 68719476767, 17592186044423]",
        "nvec[]",
        "nvec[]",
        "none",
        "12",
        "nvec[1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 18, 20, 24, 30, 36, 40, 45, 60, 72, 90, 120, 180, 360]",
        "nvec[1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 18, 20, 24, 30, 36, 40, 45, 60, 72, 90, 120, 180, 360]",
        "nvec[2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47]",
        "nvec[2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47]",
        "23",
        "none",
        "11",
        "6",
        "59713600",
        "none",
        "18446744073709551615",
        "nvec[1, 0, 1]",
        "nvec[1, 0, 1]",
        "nvec[5, 0, 3]",
        "nvec[5, 0, 3]",
        "nvec[]",
        "nvec[]",
        "nvec[3, 1]",
        "nvec[3, 1]",
        "nvec[2]",
        "nvec[2]",
        "nvec[3, 1]",
        "nvec[3, 1]",
        "nvec[0, 1]",
        "nvec[0, 1]",
        "321",
        "true",
        "false",
        "true",
        "true",
        "none",
        "nvec[1, 0, 2]",
        "nvec[1, 0, 2]",
        "nvec[2, 0, 1]",
        "nvec[2, 0, 1]",
        "nvec[2, 3, 4, 0, 1]",
        "nvec[2, 3, 4, 0, 1]",
        "6",
        "1",
        "nvec[3, 2, 1]",
        "nvec[3, 2, 1]",
        "none",
        "nvec[0, 1]",
        "nvec[0, 1]",
        "120",
        "60",
        "2432902008176640000",
        "8",
        "1",
    ];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn oz(x: Option<Zmod>) -> Text = match x { none => "none", some(z) => nat_to_text(field(z, value)) }
    fn og(x: Option<Gf>) -> Text = match x { none => "none", some(g) => print_symbolic(field(g, coeffs)) }
    let a = unwrap_or(zmod(7, 13), Zmod(0, 1))
    let b = unwrap_or(zmod(9, 13), Zmod(0, 1))
    print_line(oz(zm_add(a, b)))
    print_line(oz(zm_sub(a, b)))
    print_line(oz(zm_mul(a, b)))
    print_line(oz(zm_div(a, b)))
    print_line(nat_to_text(field(zm_pow(a, 12), value)))
    print_line(if_then_else(is_perfect_square(144), "sq", "no"))
    print_line(on(sigma(28)))
    print_line(nat_to_text(divisor_count(360)))
    print_line(nat_to_text(mobius(30)))
    print_line(nat_to_text(mobius(12)))
    print_line(print_symbolic(distinct_prime_factors(360)))
    print_line(on(primitive_root(1000000007)))
    print_line(on(primitive_root(23)))
    print_line(on(discrete_log(5, 8, 23, 100)))
    let k = unwrap_or(rsa_key(61, 53, 17), RsaKey(1, 1, 1))
    print_line(nat_to_text(field(k, d)))
    print_line(on(rsa_encrypt(k, 65)))
    print_line(on(rsa_decrypt(k, unwrap_or(rsa_encrypt(k, 65), 0))))
    print_line(op(irreducible(2, 8)))
    print_line(op(irreducible(3, 2)))
    let f = unwrap_or(irreducible(2, 8), nv_empty())
    let x = unwrap_or(gf_element(perm(list(0, 1, 1)), f, 2), Gf(nv_empty(), nv_empty(), 2))
    print_line(nat_to_text(field_size(x)))
    print_line(og(gf_inv(x)))
    print_line(og(match gf_inv(x) { none => none(Gf), some(i) => gf_mul(x, i) }))
    print_line(og(gf_pow(x, 255)))
    print_line(on(group_order(symmetric_group(8))))
    print_line(on(group_order(alternating_group(8))))
    print_line(on(group_order(dihedral_group(10))))
    print_line(on(group_order(cyclic_group(12))))
    print_line(if_then_else(is_transitive(dihedral_group(6)), "transitive", "no"))
    print_line(on(group_order(group(6, list(perm(list(1, 0, 2, 3, 4, 5)), perm(list(0, 1, 3, 2, 4, 5)))))))
    print_line(on(group_order(symmetric_group(30))))
    print_line(on(group_order(symmetric_group(40))))
"#;

#[test]
fn std_number_theory_agrees_between_interpreter_and_native() {
    let out = run_both("library", &format!("module test.number_lib\nimport std.number_theory\ntheory App extends StdNumberTheory {{\n{LIBRARY}}}\n"));
    let want: &[&str] = &[
        "3",
        "11",
        "11",
        "8",
        "1",
        "sq",
        "56",
        "24",
        "2",
        "0",
        "nvec[2, 3, 5]",
        "nvec[2, 3, 5]",
        "5",
        "5",
        "6",
        "2753",
        "2790",
        "65",
        "nvec[1, 1, 0, 1, 1, 0, 0, 0, 1]",
        "nvec[1, 1, 0, 1, 1, 0, 0, 0, 1]",
        "nvec[1, 0, 1]",
        "nvec[1, 0, 1]",
        "256",
        "nvec[1, 1, 0, 1, 1, 1, 1]",
        "nvec[1, 1, 0, 1, 1, 1, 1]",
        "nvec[1]",
        "nvec[1]",
        "nvec[1]",
        "nvec[1]",
        "40320",
        "20160",
        "20",
        "12",
        "transitive",
        "4",
        "265252859812191058636308480000000",
        "none",
    ];
    assert_eq!(out, want);
}

#[test]
fn host_kernels_match_brute_force() {
    let naive_prime =
        |n: u128| n >= 2 && (2..).take_while(|d: &u128| d * d <= n).all(|d| n % d != 0);
    for n in 0..5000u128 {
        assert_eq!(nt::is_prime(n), naive_prime(n), "{n}");
        if n > 0 {
            let f = nt::factor(n).unwrap();
            assert_eq!(f.iter().product::<u128>(), n);
            assert!(f.iter().all(|p| naive_prime(*p)));
            let phi = (1..=n).filter(|k| nt::gcd(*k, n) == 1).count() as u128;
            assert_eq!(nt::totient(n), phi, "{n}");
        }
    }
    for m in 1..60u128 {
        for a in 0..60u128 {
            let inv = (0..m).find(|x| a * x % m == 1 % m);
            assert_eq!(nt::mod_inv(a, m), inv, "{a} {m}");
        }
    }
    for p in [3u128, 5, 7, 11, 13, 101, 1009] {
        for a in 0..p {
            let r = nt::sqrt_mod(a, p);
            let any = (0..p).any(|x| x * x % p == a);
            assert_eq!(r.is_some(), any);
            if let Some(x) = r {
                assert_eq!(x * x % p, a);
            }
        }
    }
    let big = 340282366920938463463374607431768211297u128;
    assert!(nt::is_prime(big));
    assert_eq!(nt::powmod(7, big - 1, big), Some(1));
    // groups: |S_n| = n!, |A_n| = n!/2, |D_n| = 2n
    let shift = |n: usize| (1..n).chain([0]).map(|x| x as u128).collect::<Vec<_>>();
    let swap = |n: usize| {
        [1u128, 0]
            .into_iter()
            .chain((2..n).map(|x| x as u128))
            .collect::<Vec<_>>()
    };
    let mut f = 1u128;
    for n in 2..=12usize {
        f *= n as u128;
        let gens: Vec<u128> = shift(n).into_iter().chain(swap(n)).collect();
        assert_eq!(nt::pm_group_order(&gens, n as u128), Some(f), "S{n}");
        let refl: Vec<u128> = (0..n).rev().map(|x| x as u128).collect();
        let d: Vec<u128> = shift(n).into_iter().chain(refl).collect();
        assert_eq!(
            nt::pm_group_order(&d, n as u128),
            Some(if n == 2 { 2 } else { 2 * n as u128 }),
            "D{n}"
        );
    }
    // GF(2^8): x^8 + x^4 + x^3 + x + 1 is irreducible, x^255 = 1
    let aes = [1u128, 1, 0, 1, 1, 0, 0, 0, 1];
    assert_eq!(nt::pf_irreducible(&aes, 2), Some(true));
    assert_eq!(nt::pf_powmod(&[0, 1], 255, &aes, 2), Some(vec![1]));
    assert_eq!(nt::pf_irreducible(&[1, 0, 0, 0, 1], 2), Some(false));
}
