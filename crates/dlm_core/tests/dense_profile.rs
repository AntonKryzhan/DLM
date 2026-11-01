//! v0.107 — dense numeric storage and the verified cost profile (N14): the
//! `BigNat` kernel against u128 and Python-independent identities, fixed
//! workloads in the host, in `std.exact` (dense kernel under list storage)
//! and in `std.dense` (`BigNat` storage), interpreter and native C, with
//! reproducible work and memory under the committed thresholds.
use dlm_core::dense_nat as dn;
use dlm_core::math::profile::{self, THRESHOLDS, WORKLOADS};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;

// Whole-workload reservation totals, including operand construction and decimal formatting.
// These golden totals detect silently discarded predecessor budgets.
const SOURCE_WORK: &[(&str, u64)] = &[
    ("mul", 19584),
    ("divrem", 31234),
    ("gcd", 22770),
    ("sqrt", 62459),
    ("text", 85679),
    ("parse", 98657),
    ("pow", 94988),
];

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

#[test]
fn host_profile_is_reproducible_and_within_thresholds() {
    let first = profile::measure().unwrap();
    assert_eq!(first, profile::measure().unwrap());
    assert_eq!(first.len(), WORKLOADS.len());
    for row in &first {
        let (_, max_work, max_limbs, _) = THRESHOLDS.iter().find(|t| t.0 == row.name).unwrap();
        assert!(
            row.work > 0 && row.work <= *max_work,
            "{}: work {}",
            row.name,
            row.work
        );
        assert!(
            row.result_limbs <= *max_limbs,
            "{}: limbs {}",
            row.name,
            row.result_limbs
        );
    }
    // independent identities through the dense kernel
    let get = |n: &str| first.iter().find(|r| r.name == n).unwrap().result.clone();
    let p = |t: &str| dn::from_decimal(t).unwrap();
    let pow = |base: u128, e: u32| {
        (0..e).fold(vec![1u32], |acc, _| {
            dn::mul(&acc, &dn::from_u128(base)).unwrap()
        })
    };
    assert_eq!(
        p(&get("mul")),
        dn::mul(&pow(3, 2000), &pow(7, 1200)).unwrap()
    );
    let dr = get("divrem");
    let (q, r) = dr.split_once(' ').unwrap();
    assert_eq!(
        dn::add(&dn::mul(&p(q), &pow(7, 700)).unwrap(), &p(r)).unwrap(),
        dn::add(&pow(3, 4000), &[1]).unwrap()
    );
    assert_eq!(dn::cmp(&p(r), &pow(7, 700)), std::cmp::Ordering::Less);
    assert_eq!(p(&get("gcd")), dn::mul(&pow(6, 1200), &[5]).unwrap());
    let s = p(&get("sqrt"));
    let target = dn::mul(&pow(10, 2000), &[2]).unwrap();
    assert!(dn::cmp(&dn::mul(&s, &s).unwrap(), &target).is_le());
    let s1 = dn::add(&s, &[1]).unwrap();
    assert!(dn::cmp(&dn::mul(&s1, &s1).unwrap(), &target).is_gt());
    assert_eq!(get("text"), dn::to_decimal(&pow(3, 5000)));
    assert_eq!(get("parse"), dn::to_decimal(&pow(7, 3000)));
    assert_eq!(get("pow"), dn::to_decimal(&pow(3, 10000)));
    assert_eq!(get("pow").len(), 4772);
}

const SOURCE: &str = r#"module test.dense_profile
import std.dense
theory App extends StdDense {
 let big = ExactBudget(16384, 18446744073709551615)
 fn z(r: Result<DIntCalc, Text>) -> DInt = match r { err(e) => DInt(false, bn_from_nat(0)), ok(v) => field(v, value) }
 fn zb(r: Result<DIntCalc, Text>) -> ExactBudget = match r { err(e) => big, ok(v) => field(v, budget) }
 fn zt(r: Result<DIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match dz_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn i(r: Result<BigIntCalc, Text>) -> BigInt = match r { err(e) => BigInt(false, empty_list(Nat)), ok(v) => field(v, value) }
 fn it(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => match big_text(field(v, budget), field(v, value)) { err(e) => e, ok(t) => field(t, value) } }
 fn dp(b: ExactBudget, base: Nat, e: Nat) -> Result<DIntCalc, Text> = dz_pow(b, DInt(false, bn_from_nat(base)), e)
 fn lp(base: Nat, e: Nat) -> BigInt = i(big_pow(big, BigInt(false, list(base)), e))
 fn dn(v: Nat) -> DInt = DInt(false, bn_from_nat(v))
 fn ln(v: Nat) -> BigInt = BigInt(false, list(v))
 fn dense(name: Text) -> Result<DIntCalc, Text> =
    if_then_else(text_eq(name, "mul"), match dp(big, 3, 2000) { err(e) => err(e, DIntCalc), ok(a) =>
        match dp(field(a, budget), 7, 1200) { err(e) => err(e, DIntCalc), ok(c) => dz_mul(field(c, budget), field(a, value), field(c, value)) } },
    if_then_else(text_eq(name, "gcd"), match dp(big, 6, 1500) { err(e) => err(e, DIntCalc), ok(a) =>
        match dz_mul(field(a, budget), field(a, value), dn(5)) { err(e) => err(e, DIntCalc), ok(x) =>
        match dp(field(x, budget), 6, 1200) { err(e) => err(e, DIntCalc), ok(c) =>
        match dz_mul(field(c, budget), field(c, value), dn(35)) { err(e) => err(e, DIntCalc), ok(y) => dz_gcd(field(y, budget), field(x, value), field(y, value)) } } } },
    if_then_else(text_eq(name, "sqrt"), match dp(big, 10, 2000) { err(e) => err(e, DIntCalc), ok(a) =>
        match dz_mul(field(a, budget), field(a, value), dn(2)) { err(e) => err(e, DIntCalc), ok(x) => dz_sqrt(field(x, budget), field(x, value)) } },
    if_then_else(text_eq(name, "pow"), dp(big, 3, 10000),
    if_then_else(text_eq(name, "text"), match dp(big, 3, 5000) { err(e) => err(e, DIntCalc), ok(a) =>
        match dz_text(field(a, budget), field(a, value)) { err(e) => err(e, DIntCalc), ok(t) => ok(DIntCalc(field(a, value), field(t, budget)), Text) } },
    if_then_else(text_eq(name, "parse"), match dp(big, 7, 3000) { err(e) => err(e, DIntCalc), ok(a) =>
        match dz_text(field(a, budget), field(a, value)) { err(e) => err(e, DIntCalc), ok(t) => dz_parse(field(t, budget), field(t, value)) } }, err("no workload", DIntCalc)))))))
 fn list_path(name: Text) -> Result<BigIntCalc, Text> =
    if_then_else(text_eq(name, "mul"), big_mul(big, lp(3, 2000), lp(7, 1200)),
    if_then_else(text_eq(name, "gcd"), big_gcd(big, i(big_mul(big, lp(6, 1500), ln(5))), i(big_mul(big, lp(6, 1200), ln(35)))),
    if_then_else(text_eq(name, "sqrt"), match big_sqrt_rem(big, i(big_mul(big, i(big_pow(big, ln(10), 2000)), ln(2)))) { err(e) => err(e, BigIntCalc), ok(s) => ok(BigIntCalc(field(s, root), field(s, budget)), Text) },
    if_then_else(text_eq(name, "pow"), big_pow(big, ln(3), 10000),
    if_then_else(text_eq(name, "text"), big_pow(big, ln(3), 5000),
    if_then_else(text_eq(name, "parse"), big_parse(big, it(big_pow(big, ln(7), 3000))), err("no workload", BigIntCalc)))))))
 fn divrem_dense() -> Text = match dp(big, 3, 4000) { err(e) => e, ok(a) =>
    match dz_add(field(a, budget), field(a, value), dn(1)) { err(e) => e, ok(x) =>
    match dp(field(x, budget), 7, 700) { err(e) => e, ok(c) =>
    match dz_div_rem(field(c, budget), field(x, value), field(c, value)) { err(e) => e, ok(d) =>
    concat(zt(ok(DIntCalc(field(d, quotient), big), Text)), concat(" ", concat(zt(ok(DIntCalc(field(d, remainder), big), Text)), concat("|", concat(nat_to_text(monus(field(big, work), field(field(d, budget), work))), concat("|", nat_to_text(dz_limbs(field(d, quotient))))))))) } } } }
 fn divrem_list() -> Text = match big_div_rem(big, i(big_add(big, lp(3, 4000), ln(1))), lp(7, 700)) { err(e) => e, ok(d) =>
    concat(it(ok(BigIntCalc(field(d, quotient), big), Text)), concat(" ", it(ok(BigIntCalc(field(d, remainder), big), Text)))) }
 fn row(name: Text) -> Text = if_then_else(text_eq(name, "divrem"), concat(divrem_dense(), concat("|", divrem_list())),
    let d = dense(name) in
    concat(zt(d), concat("|", concat(nat_to_text(monus(field(big, work), field(zb(d), work))), concat("|", concat(nat_to_text(dz_limbs(z(d))), concat("|", it(list_path(name)))))))))
"#;

#[test]
fn source_dense_and_list_paths_match_the_host_profile() {
    let host = profile::measure().unwrap();
    let mut source = SOURCE.to_string();
    for (name, _) in WORKLOADS {
        source += &format!(" print_line(row(\"{name}\"))\n");
    }
    source += "}\n";
    let dir = std::env::temp_dir().join(format!("dlm-dense-profile-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, &source).unwrap();
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
    let output = Command::new(&exe)
        .env("DLM_DENSE_TRACE", "1")
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native: Vec<String> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    assert_eq!(native.len(), host.len());
    for (line, row) in native.iter().zip(&host) {
        let parts: Vec<&str> = line.split('|').collect();
        assert_eq!(parts.len(), 4, "{}: {line}", row.name);
        let (value, work, limbs, listed) = (
            parts[0],
            parts[1].parse::<u64>().unwrap(),
            parts[2].parse::<usize>().unwrap(),
            parts[3],
        );
        assert_eq!(value, row.result, "{}", row.name);
        assert_eq!(listed, row.result, "{}: list path", row.name);
        let (_, _, max_limbs, max_source) = THRESHOLDS.iter().find(|t| t.0 == row.name).unwrap();
        assert!(
            work > 0 && work <= *max_source,
            "{}: dense work {work}",
            row.name
        );
        assert_eq!(
            work,
            SOURCE_WORK
                .iter()
                .find(|(name, _)| *name == row.name)
                .unwrap()
                .1,
            "{}: full workload work",
            row.name
        );
        assert!(limbs <= *max_limbs, "{}: limbs {limbs}", row.name);
        assert_eq!(limbs, row.result_limbs, "{}: result layout", row.name);
    }
    // native memory: every dense value of these workloads fits 2 * 16384 bits
    let trace = String::from_utf8(output.stderr).unwrap();
    let largest: u64 = trace
        .split("largest ")
        .nth(1)
        .unwrap()
        .split(' ')
        .next()
        .unwrap()
        .parse()
        .unwrap();
    assert!(largest > 0 && largest <= 1025, "{trace}");
    std::fs::remove_dir_all(dir).unwrap();
}

fn check_cases(name: &str, source: &str, expected: &[String]) {
    let dir = std::env::temp_dir().join(format!("dlm-dense-{name}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(Runtime::new().run_program(&p).unwrap().output, expected);
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native: Vec<String> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(native, expected);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn dense_conversions_validate_budgets_shapes_and_zero_exponents() {
    let mut source = String::from(
        r#"module test.dense_contract
import std.dense
theory App extends StdDense {
 let b = ExactBudget(256, 1000000000000000)
 let bad = ExactBudget(0, 0)
 let z = DInt(false, bn_from_nat(0))
 let nz = DInt(true, bn_from_nat(0))
 let one = DInt(false, bn_from_nat(1))
 let two = DInt(false, bn_from_nat(2))
 fn t(r: Result<DIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => concat(if_then_else(field(field(v, value), negative), "-", ""), bn_to_text(field(field(v, value), magnitude))) }
 fn c(r: Result<DCompare, Text>) -> Text = match r { err(e) => e, ok(v) => int_to_text(field(v, order)) }
 fn bt(r: Result<BigIntCalc, Text>) -> Text = match r { err(e) => e, ok(v) => concat(if_then_else(field(field(v, value), negative), "-", ""), concat(bn_to_text(unwrap_or(bn_from_words(field(field(v, value), words)), bn_from_nat(0))), concat("|", nat_to_text(field(field(v, budget), work))))) }
"#,
    );
    let cases = [
        ("t(dz_from_int(bad, int(0)))", "invalid exact source budget"),
        ("t(dz_pow(bad, z, 0))", "invalid exact source budget"),
        (
            "t(dz_from_int(ExactBudget(16385, 1), int(1)))",
            "invalid exact source budget",
        ),
        (
            "t(dz_sqrt(bad, DInt(true, bn_from_nat(1))))",
            "invalid exact source budget",
        ),
        (
            "t(dz_pow(ExactBudget(1, 100), two, 0))",
            "UnknownWithinBudget: source precision exceeded",
        ),
        ("t(dz_pow(b, two, 0))", "1"),
        ("c(dz_cmp(b, nz, z))", "invalid canonical big integer"),
        ("t(dz_add(b, nz, z))", "invalid canonical big integer"),
        ("t(dz_sub(b, z, nz))", "invalid canonical big integer"),
        ("t(dz_pow(b, nz, 0))", "invalid canonical big integer"),
        ("t(dz_sqrt(b, nz))", "invalid canonical big integer"),
        ("bt(dz_to_big(b, nz))", "invalid canonical big integer"),
        (
            "bt(dz_to_big(ExactBudget(256, 2), DInt(false, bn_from_nat(7))))",
            "7|0",
        ),
        (
            "bt(dz_to_big(ExactBudget(256, 2), DInt(true, bn_from_nat(7))))",
            "-7|0",
        ),
        (
            "bt(dz_to_big(ExactBudget(256, 1), one))",
            "UnknownWithinBudget: source work reservation exhausted",
        ),
        (
            "bt(dz_to_big(ExactBudget(1, 0), two))",
            "UnknownWithinBudget: source precision exceeded",
        ),
        (
            "t(dz_from_big(bad, BigInt(false, list(65536))))",
            "invalid exact source budget",
        ),
        (
            "t(dz_from_big(b, BigInt(true, empty_list(Nat))))",
            "invalid canonical big integer",
        ),
        (
            "t(dz_from_big(b, BigInt(false, list(1, 0))))",
            "invalid canonical big integer",
        ),
        (
            "t(dz_from_big(b, BigInt(false, list(65536))))",
            "invalid big integer words",
        ),
    ];
    for (call, _) in cases {
        source += &format!(" print_line({call})\n");
    }
    source += "}\n";
    check_cases(
        "contract",
        &source,
        &cases.iter().map(|(_, e)| e.to_string()).collect::<Vec<_>>(),
    );
}

#[test]
fn dense_builtins_match_the_independent_exact_kernel_and_size_boundaries() {
    use dlm_core::math::exact::{ExactBudget, Natural};
    let mut seed = 107u64;
    let mut next = || {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        seed as u32
    };
    let mut cases = vec![(vec![], vec![]), (vec![1], vec![]), (vec![], vec![1])];
    for i in 0..64 {
        let n = 1 + i % 48;
        let m = 1 + i % n;
        let mut a: Vec<u32> = (0..n).map(|_| next()).collect();
        let mut b: Vec<u32> = (0..m).map(|_| next()).collect();
        if i % 3 == 0 {
            a.fill(u32::MAX);
        }
        if i % 4 == 0 {
            b.fill(u32::MAX);
        }
        if i % 5 == 0 {
            *b.last_mut().unwrap() = 1;
        }
        cases.push((dn::trim(a), dn::trim(b)));
    }
    let mut source = String::from(
        r#"module test.dense_oracle
theory App {
 fn p(t: Text) -> BigNat = unwrap_or(bn_from_text(t), bn_from_nat(0))
 fn o(x: Option<BigNat>) -> Text = match x { none => "none", some(v) => bn_to_text(v) }
 fn row(a: BigNat, b: BigNat) -> Text = join(list(o(bn_add(a, b)), bn_to_text(bn_monus(a, b)), o(bn_mul(a, b)), o(bn_div(a, b)), o(bn_mod(a, b)), bn_to_text(bn_gcd(a, b)), bn_to_text(bn_sqrt(a)), int_to_text(bn_cmp(a, b)), o(bn_shl(a, 33)), bn_to_text(bn_shr(a, 33)), bn_to_text(unwrap_or(bn_from_words(bn_to_words(a)), bn_from_nat(0))), match bn_to_nat(a) { none => "none", some(v) => nat_to_text(v) }, nat_to_text(bn_bits(a)), nat_to_text(bn_limbs(a))), "|")
"#,
    );
    let mut expected = Vec::new();
    let budget = &mut ExactBudget::new(16384, u64::MAX / 4).unwrap();
    for (x, y) in cases {
        // Construct the reference from words by exact arithmetic, without dense parsing/formatting.
        let reference = |words: &[u32], budget: &mut ExactBudget| {
            words.iter().rev().fold(Natural::zero(), |acc, &word| {
                acc.checked_mul(&Natural::from_u128(1u128 << 32), budget)
                    .unwrap()
                    .checked_add(&Natural::from_u128(u128::from(word)), budget)
                    .unwrap()
            })
        };
        let (a, b) = (reference(&x, budget), reference(&y, budget));
        assert_eq!(dn::to_decimal(&x), a.to_string());
        assert_eq!(dn::to_decimal(&y), b.to_string());
        let (q, r) = if y.is_empty() {
            ("none".into(), "none".into())
        } else {
            let (q, r) = a.div_rem(&b, budget).unwrap();
            (q.to_string(), r.to_string())
        };
        let scale = Natural::from_u128(1u128 << 33);
        let values = [
            a.checked_add(&b, budget).unwrap().to_string(),
            if a < b {
                "0".into()
            } else {
                a.checked_sub(&b, budget).unwrap().to_string()
            },
            a.checked_mul(&b, budget).unwrap().to_string(),
            q,
            r,
            a.checked_gcd(&b, budget).unwrap().to_string(),
            a.checked_sqrt_rem(budget).unwrap().0.to_string(),
            (a.cmp(&b) as i8).to_string(),
            a.checked_mul(&scale, budget).unwrap().to_string(),
            a.div_rem(&scale, budget).unwrap().0.to_string(),
            a.to_string(),
            a.to_u128().map_or_else(|| "none".into(), |v| v.to_string()),
            a.bit_len().to_string(),
            a.bit_len().div_ceil(32).to_string(),
        ];
        let (quot, rem) = if y.is_empty() {
            ("none".into(), "none".into())
        } else {
            let (quot, rem) = dn::divrem(&x, &y).unwrap();
            (dn::to_decimal(&quot), dn::to_decimal(&rem))
        };
        assert_eq!((quot, rem), (values[3].clone(), values[4].clone()));
        source += &format!(" print_line(row(p(\"{a}\"), p(\"{b}\")))\n");
        expected.push(values.join("|"));
    }
    source += r#" let one = bn_from_nat(1)
 let high = unwrap_or(bn_shl(one, 1048575), bn_from_nat(0))
 let maximum = unwrap_or(bn_add(high, bn_monus(high, one)), bn_from_nat(0))
 print_line(nat_to_text(bn_bits(maximum)))
 print_line(if_then_else(is_some(bn_add(maximum, one)), "some", "none"))
 print_line(if_then_else(is_some(bn_mul(high, bn_from_nat(2))), "some", "none"))
 print_line(if_then_else(is_some(bn_shl(one, 340282366920938463463374607431768211455)), "some", "none"))
 print_line(o(bn_shl(bn_from_nat(0), 340282366920938463463374607431768211455)))
 print_line(bn_to_text(bn_shr(maximum, 340282366920938463463374607431768211455)))
 print_line(if_then_else(is_some(bn_from_words(list(65536))), "some", "none"))
}
"#;
    expected.extend(["1048576", "none", "none", "none", "0", "0", "none"].map(str::to_string));
    check_cases("oracle", &source, &expected);
}
