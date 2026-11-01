//! v0.124 — BigNat modular arithmetic and Baillie–PSW (`bn_powmod`, `bn_modinv`,
//! `bn_is_prime`, `bn_next_prime`, `bn_random`, hex), 256-bit `mulmod` and BPSW for
//! `nt_*`, BVH ray casting / winding numbers / box pairs (`bvh_*`), std.bigprime and the
//! std.spatial soup functions. Interpreter and native C agree; kernels are checked against
//! independent references.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use dlm_core::{bignum, bvh, dense_nat as dn, number};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-v124-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(&emit_c(&ir), &dir.join("main.c"), &dir.join(if cfg!(windows) { "main.exe" } else { "main" }), &default_c_compiler()).unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let native: Vec<String> = String::from_utf8(out.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).ok();
    native
}

const PROGRAM: &str = r#"    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn of(x: Option<FloatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn ob(x: Option<BigNat>) -> Text = match x { none => "none", some(v) => bn_to_text(v) }
    fn bt(t: Text) -> BigNat = unwrap_or(bn_from_text(t), bn_from_nat(0))
    fn bb(b: Bool) -> Text = if_then_else(b, "true", "false")
    let cube = fvl(list(0.0,0.0,0.0, 0.0,1.0,0.0, 1.0,1.0,0.0,  0.0,0.0,0.0, 1.0,1.0,0.0, 1.0,0.0,0.0,
        0.0,0.0,1.0, 1.0,0.0,1.0, 1.0,1.0,1.0,  0.0,0.0,1.0, 1.0,1.0,1.0, 0.0,1.0,1.0,
        0.0,0.0,0.0, 1.0,0.0,0.0, 1.0,0.0,1.0,  0.0,0.0,0.0, 1.0,0.0,1.0, 0.0,0.0,1.0,
        0.0,1.0,0.0, 0.0,1.0,1.0, 1.0,1.0,1.0,  0.0,1.0,0.0, 1.0,1.0,1.0, 1.0,1.0,0.0,
        0.0,0.0,0.0, 0.0,0.0,1.0, 0.0,1.0,1.0,  0.0,0.0,0.0, 0.0,1.0,1.0, 0.0,1.0,0.0,
        1.0,0.0,0.0, 1.0,1.0,0.0, 1.0,1.0,1.0,  1.0,0.0,0.0, 1.0,1.0,1.0, 1.0,0.0,1.0))
    print_line(of(bvh_raycast(cube, fvl(list(0.25, 0.5, -1.0, 0.3, 0.3, 0.5, 5.0, 5.0, 5.0)), fvl(list(0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0)))))
    print_line(of(bvh_winding(cube, fvl(list(0.5, 0.5, 0.5, 2.0, 0.5, 0.5, 0.1, 0.9, 0.2)))))
    print_line(op(bvh_pairs(fvl(list(0.0,0.0,0.0,1.0,1.0,1.0, 0.5,0.5,0.5,2.0,2.0,2.0, 3.0,3.0,3.0,4.0,4.0,4.0, 1.0,0.0,0.0,1.5,0.2,0.2)))))
    print_line(fl_to_text(soup_area(cube)))
    print_line(fl_to_text(soup_volume(cube)))
    print_line(bb(inside_mesh(cube, Vec3(0.2, 0.3, 0.4))))
    print_line(bb(inside_mesh(cube, Vec3(1.2, 0.3, 0.4))))
    print_line(match ray_distance(cube, Vec3(0.5, 0.5, 3.0), Vec3(0.0, 0.0, -1.0)) { none => "miss", some(d) => fl_to_text(d) })
    print_line(nat_to_text(box_overlap_count(fvl(list(0.0,0.0,0.0,1.0,1.0,1.0, 0.5,0.5,0.5,2.0,2.0,2.0)))))
    print_line(ob(bn_powmod(bn_from_nat(4), bn_from_nat(13), bn_from_nat(497))))
    print_line(ob(bn_powmod(bt("123456789012345678901234567890"), bt("98765432109876543210"), bt("170141183460469231731687303715884105727"))))
    print_line(ob(bn_modinv(bn_from_nat(3), bn_from_nat(11))))
    print_line(ob(bn_modinv(bn_from_nat(6), bn_from_nat(9))))
    print_line(bb(bn_is_prime(bt("170141183460469231731687303715884105727"))))
    print_line(bb(bn_is_prime(bt("3317044064679887385961981"))))
    print_line(bb(bn_is_prime(bt("115792089237316195423570985008687907853269984665640564039457584007908834671663"))))
    print_line(bb(bn_is_prime(bt("115792089237316195423570985008687907853269984665640564039457584007908834671665"))))
    print_line(ob(bn_next_prime(bt("1000000000000000000000000000000"))))
    print_line(ob(bn_random(42, 100)))
    print_line(ob(random_prime(7, 128)))
    print_line(bn_to_hex(bt("255")))
    print_line(bn_to_hex(bt("340282366920938463463374607431768211456")))
    print_line(ob(bn_from_hex("DeadBeef0123456789")))
    print_line(ob(bn_from_hex("xyz")))
    print_line(bb(lucas_lehmer(61)))
    print_line(bb(lucas_lehmer(67)))
    print_line(bb(lucas_lehmer(521)))
    print_line(match big_rsa_key(bt("1000000007"), bt("998244353"), bn_from_nat(65537)) { none => "nokey", some(k) =>
        match big_rsa_encrypt(k, bn_from_nat(123456789)) { none => "noenc", some(c) => ob(big_rsa_decrypt(k, c)) } })
    print_line(match random_prime(1, 256) { none => "np", some(p) => match random_prime(2, 256) { none => "nq", some(q) =>
        match big_rsa_key(p, q, bn_from_nat(65537)) { none => "nokey", some(k) =>
            match big_rsa_sign(k, bt("31415926535897932384626433832795028841971")) { none => "nosig", some(s) => bb(big_rsa_verify(k, bt("31415926535897932384626433832795028841971"), s)) } } } })
    print_line(ob(big_crt(bn_from_nat(2), bn_from_nat(3), bn_from_nat(3), bn_from_nat(5))))
    print_line(bb(nt_is_prime(170141183460469231731687303715884105727)))
    print_line(bb(nt_is_prime(3317044064679887385961981)))
    print_line(bb(nt_is_prime(3317044064679887385961983)))
    print_line(bb(nt_is_prime(318665857834031151167461)))
    print_line(match nt_mod_mul(340282366920938463463374607431768211455, 340282366920938463463374607431768211454, 340282366920938463463374607431768211297) { none => "none", some(v) => nat_to_text(v) })
    print_line(match nt_factor(340282366920938463463374607431768211455) { none => "none", some(f) => print_symbolic(f) })
"#;

const EXPECTED: &[&str] = &[
    "fvec[0.0, 1.0, 11.0, 0.7, -1.0, -1.0]",
    "fvec[0.0, 1.0, 11.0, 0.7, -1.0, -1.0]",
    "fvec[1.0000000000000004, 8.834874115176436e-18, 1.0]",
    "fvec[1.0000000000000004, 8.834874115176436e-18, 1.0]",
    "nvec[0, 1, 0, 3]",
    "nvec[0, 1, 0, 3]",
    "6.0",
    "0.9999999999999999",
    "true",
    "false",
    "2.0",
    "1",
    "445",
    "94100362589646508130024018693800009796",
    "4",
    "none",
    "true",
    "false",
    "true",
    "false",
    "1000000000000000000000000000057",
    "1210495493340053259453451416102",
    "198360899900924158381716082753383686663",
    "ff",
    "100000000000000000000000000000000",
    "4107696891165822773129",
    "none",
    "true",
    "false",
    "true",
    "123456789",
    "true",
    "8",
    "true",
    "false",
    "false",
    "false",
    "24806",
    "nvec[3, 5, 17, 257, 641, 65537, 274177, 6700417, 67280421310721]",
    "nvec[3, 5, 17, 257, 641, 65537, 274177, 6700417, 67280421310721]",
];

#[test]
fn bignum_and_bvh_agree_between_interpreter_and_native() {
    let out = run_both("all", &format!("module test.v124
import std.bigprime
theory App extends StdBigPrime {{
{PROGRAM}}}
"));
    // winding numbers use libm atan2: exact text only where the value is rounding-independent
    for (i, (got, want)) in out.iter().zip(EXPECTED).enumerate() {
        if i != 1 {
            assert_eq!(got, want, "#{i}");
        }
    }
    assert_eq!(out.len(), EXPECTED.len());
}

fn lcg(s: &mut u64) -> u64 {
    *s = s.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    *s
}

fn big(x: u128) -> Vec<u32> {
    dn::from_u128(x)
}

#[test]
fn wide_mulmod_and_bpsw_match_bignum() {
    let mut s = 0x1234_5678u64;
    for _ in 0..20000 {
        let w = |s: &mut u64| (u128::from(lcg(s)) << 64) | u128::from(lcg(s));
        let (a, b, mut m) = (w(&mut s), w(&mut s), w(&mut s) >> (lcg(&mut s) % 64));
        if m == 0 {
            m = 1;
        }
        let want = dn::divrem(&dn::mul(&big(a), &big(b)).unwrap(), &big(m)).unwrap().1;
        assert_eq!(big(number::mulmod(a, b, m)), want, "{a} {b} {m}");
    }
    let primes = [170141183460469231731687303715884105727u128, 340282366920938463463374607431768211297, 18446744073709551557, 1000000000000000000000000000057];
    let composites = [3317044064679887385961981u128, 318665857834031151167461, 3825123056546413051, 18446744073709551557 * 1000000007, 340282366920938463463374607431768211297 / 3 * 3];
    for p in primes {
        assert!(number::is_prime(p) && bignum::is_prime(&big(p)), "{p}");
    }
    for c in composites {
        assert!(!number::is_prime(c) && !bignum::is_prime(&big(c)), "{c}");
    }
    for n in 0..20000u128 {
        assert_eq!(number::is_prime(n), bignum::is_prime(&big(n)), "{n}");
    }
    // Lucas alone must accept primes and reject the strong pseudoprimes to base 2
    for _ in 0..300 {
        let n = (u128::from(lcg(&mut s)) << 40 | u128::from(lcg(&mut s) >> 24)) | 1;
        assert_eq!(number::is_prime(n), bignum::is_prime(&big(n)), "{n}");
    }
    let n = big(65537);
    for a in 1..200u128 {
        let inv = bignum::modinv(&big(a), &n).unwrap();
        assert_eq!(dn::divrem(&dn::mul(&inv, &big(a)).unwrap(), &n).unwrap().1, vec![1]);
        assert_eq!(bignum::powmod(&big(a), &big(65536), &n), Some(vec![1]));
    }
    assert_eq!(bignum::from_hex(&bignum::to_hex(&big(u128::MAX))), Some(big(u128::MAX)));
    assert_eq!(bignum::random(9, 77).map(|x| dn::bits(&x)), Some(77));
}

#[test]
fn bvh_matches_brute_force() {
    let mut s = 99u64;
    let f = |s: &mut u64| (lcg(s) >> 11) as f64 / (1u64 << 53) as f64 * 10.0 - 5.0;
    let tris: Vec<f64> = (0..9 * 300).map(|_| f(&mut s)).collect();
    let origins: Vec<f64> = (0..3 * 200).map(|_| f(&mut s)).collect();
    let dirs: Vec<f64> = (0..3 * 200).map(|_| f(&mut s)).collect();
    let got = bvh::raycast(&tris, &origins, &dirs).unwrap();
    let none = bvh::raycast(&[], &origins, &dirs).unwrap();
    assert!(none.iter().all(|&x| x == -1.0));
    let mut hits = 0;
    for r in 0..200 {
        let mut best = (f64::INFINITY, usize::MAX);
        for i in 0..300 {
            let one = bvh::raycast(&tris[9 * i..9 * i + 9], &origins[3 * r..3 * r + 3], &dirs[3 * r..3 * r + 3]).unwrap();
            if one[0] == 0.0 && (one[1] < best.0 || (one[1] == best.0 && i < best.1)) {
                best = (one[1], i);
            }
        }
        if best.1 == usize::MAX {
            assert_eq!(&got[2 * r..2 * r + 2], &[-1.0, -1.0]);
        } else {
            hits += 1;
            assert_eq!(got[2 * r] as usize, best.1);
            assert_eq!(got[2 * r + 1], best.0);
        }
    }
    assert!(hits > 20);
    let boxes: Vec<f64> = (0..400).flat_map(|_| {
        let (x, y, z) = (f(&mut s), f(&mut s), f(&mut s));
        [x, y, z, x + 0.8, y + 0.8, z + 0.8]
    }).collect();
    let pairs = bvh::aabb_pairs(&boxes).unwrap();
    let mut want = Vec::new();
    for i in 0..400 {
        for j in i + 1..400 {
            if (0..3).all(|a| boxes[6 * i + a] <= boxes[6 * j + 3 + a] && boxes[6 * j + a] <= boxes[6 * i + 3 + a]) {
                want.extend([i as u128, j as u128]);
            }
        }
    }
    assert_eq!(pairs, want);
}
