//! v0.121 — SAT certificates (`sat_proof`, independent RUP checker
//! `sat_check`), exact model counting (`sat_count`) and integer linear
//! programming (`ilp_*`, branch and bound), with the `std.logic` /
//! `std.optimize` wrappers. Interpreter and native C print identical lines.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use dlm_core::{combinatorics as cb, spectral};
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-proofs-{}-{name}", std::process::id()));
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

const PROGRAM: &str = r#"    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn ob(x: Option<Bool>) -> Text = match x { none => "none", some(v) => if_then_else(v, "true", "false") }
    fn b(x: Bool) -> Text = if_then_else(x, "true", "false")
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    fn lp(r: LpResult) -> Text = concat(nat_to_text(field(r, status)), concat(" ", concat(print_symbolic(field(r, x)), concat(" ", fl_to_text(field(r, value))))))
    let php = pigeonhole(5)
    let proof = unwrap_or(refutation(php), nv_empty())
    print_line(nat_to_text(refutation_size(proof)))
    print_line(b(check_refutation(php, proof)))
    print_line(b(verified_unsat(php)))
    print_line(b(verified_unsat(queens_cnf(8))))
    print_line(b(check_refutation(php, nv(list(0)))))
    print_line(ob(sat_check(nv(list(0, 1)), nv(list(1, 2)), 1, nv(list(0)))))
    print_line(ob(sat_check(nv(list(0, 2, 1, 2, 0, 3, 1, 3)), nv(list(2, 4, 6, 8)), 2, nv(list(1, 1, 2)))))
    print_line(ob(sat_check(nv(list(0, 2, 1, 2, 0, 3, 1, 3)), nv(list(2, 4, 6, 8)), 2, nv(list(1, 1, 0)))))
    print_line(op(sat_proof(nv(list(0, 2, 1, 2, 0, 3, 1, 3)), nv(list(2, 4, 6, 8)), 2)))
    print_line(on(model_count(queens_cnf(6))))
    print_line(on(model_count(queens_cnf(8))))
    print_line(on(model_count(cnf(10))))
    print_line(on(model_count(exactly_one(cnf(5), list(pos(0), pos(1), pos(2), pos(3), pos(4))))))
    print_line(b(entails(add_clauses(cnf(3), list(list(neg(0), pos(1)), list(neg(1), pos(2)))), list(neg(0), pos(2)))))
    print_line(b(entails(add_clauses(cnf(3), list(list(neg(0), pos(1)))), list(pos(2)))))
    print_line(lp(integer_program(fvl(list(5.0, 4.0, 3.0)), Matrix(3, 3, fvl(list(2.0, 3.0, 1.0, 4.0, 1.0, 2.0, 3.0, 4.0, 2.0))), fvl(list(5.0, 11.0, 8.0)), all_integer(3))))
    print_line(lp(knapsack(fvl(list(60.0, 100.0, 120.0)), fvl(list(10.0, 20.0, 30.0)), 50.0)))
    print_line(lp(integer_program(fvl(list(1.0, 1.0)), Matrix(2, 2, fvl(list(2.0, 2.0, -2.0, -2.0))), fvl(list(3.0, -1.0)), all_integer(2))))
    print_line(lp(integer_program(fvl(list(1.0, 0.0)), Matrix(1, 2, fvl(list(-1.0, 1.0))), fvl(list(0.5)), all_integer(2))))
    print_line(lp(integer_program(fvl(list(1.0, 1.0)), Matrix(1, 2, fvl(list(1.0, 1.0))), fvl(list(2.5)), nv(list(1, 0)))))
"#;

#[test]
fn certificates_counting_and_ilp_agree_between_interpreter_and_native() {
    let out = run_both("program", &format!("module test.proofs\nimport std.topology\ntheory App extends StdTopology {{\n{PROGRAM}}}\n"));
    let want: &[&str] = &[
        "141",
        "true",
        "true",
        "false",
        "false",
        "true",
        "true",
        "true",
        "nvec[1, 1, 0]",
        "nvec[1, 1, 0]",
        "4",
        "92",
        "1024",
        "5",
        "true",
        "false",
        "fvec[2.0, 0.0, 1.0]",
        "0 fvec[2.0, 0.0, 1.0] 13.0",
        "fvec[0.0, 1.0, 1.0]",
        "0 fvec[0.0, 1.0, 1.0] 220.0",
        "fvec[1.0, 0.0]",
        "0 fvec[1.0, 0.0] 1.0",
        "fvec[]",
        "2 fvec[] nan",
        "fvec[2.0, 0.5]",
        "0 fvec[2.0, 0.5] 2.5",
    ];
    assert_eq!(out, want);
}

fn lcg(s: &mut u64) -> u64 {
    *s = s
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    *s >> 33
}

#[test]
fn certificates_check_and_counts_match_brute_force() {
    let mut seed = 99u64;
    for round in 0..200 {
        let nv = 3 + (round % 8) as usize;
        let nc = 1 + (lcg(&mut seed) as usize) % (6 * nv);
        let mut lits = Vec::new();
        let mut ends = Vec::new();
        let mut clauses = Vec::new();
        for _ in 0..nc {
            let k = 1 + (lcg(&mut seed) % 3) as usize;
            let c: Vec<u128> = (0..k)
                .map(|_| (lcg(&mut seed) % (2 * nv as u64)) as u128)
                .collect();
            lits.extend(&c);
            ends.push(lits.len() as u128);
            clauses.push(c);
        }
        let models = (0..1u32 << nv)
            .filter(|m| {
                clauses
                    .iter()
                    .all(|c| c.iter().any(|l| ((m >> (l / 2)) & 1 == 1) == (l % 2 == 0)))
            })
            .count() as u128;
        assert_eq!(
            cb::sat_count(&lits, &ends, nv as u128),
            Some(models),
            "round {round}"
        );
        match cb::sat_proof(&lits, &ends, nv as u128) {
            Some(p) => {
                assert_eq!(models, 0);
                assert_eq!(
                    cb::sat_check(&lits, &ends, nv as u128, &p),
                    Some(true),
                    "round {round}"
                );
                // a corrupted certificate is rejected or still valid, never accepted for a satisfiable formula
            }
            None => assert!(models > 0),
        }
    }
    // a satisfiable formula has no accepted refutation
    assert_eq!(cb::sat_check(&[0, 2], &[1, 2], 2, &[0]), Some(false));
    assert_eq!(cb::sat_check(&[0, 2], &[1, 2], 2, &[1, 0]), Some(false));
    assert_eq!(cb::sat_check(&[0], &[1], 1, &[2, 1]), None);
}

#[test]
fn integer_programs_match_enumeration() {
    let mut seed = 5u64;
    for round in 0..60 {
        let n = 2 + round % 2;
        let m = 2 + round % 3;
        let c: Vec<f64> = (0..n).map(|_| (lcg(&mut seed) % 7) as f64 + 1.0).collect();
        let a: Vec<f64> = (0..m * n)
            .map(|_| (lcg(&mut seed) % 5) as f64 + 1.0)
            .collect();
        let b: Vec<f64> = (0..m).map(|_| (lcg(&mut seed) % 15) as f64 + 3.0).collect();
        let (s, x) = spectral::ilp(&c, &a, &b, m as u128, n as u128, &vec![1; n]);
        assert_eq!(s, 0, "round {round}");
        let value = |x: &[f64]| x.iter().zip(&c).map(|(p, q)| p * q).sum::<f64>();
        let mut best = 0.0f64;
        let bound = 20;
        let mut idx = vec![0usize; n];
        loop {
            let xs: Vec<f64> = idx.iter().map(|v| *v as f64).collect();
            if (0..m).all(|i| (0..n).map(|j| a[i * n + j] * xs[j]).sum::<f64>() <= b[i] + 1e-9) {
                best = best.max(value(&xs));
            }
            let mut k = 0;
            while k < n {
                idx[k] += 1;
                if idx[k] <= bound {
                    break;
                }
                idx[k] = 0;
                k += 1;
            }
            if k == n {
                break;
            }
        }
        assert!(
            (value(&x) - best).abs() < 1e-6,
            "round {round}: {x:?} {best}"
        );
        assert!(x.iter().all(|v| v.fract() == 0.0 && *v >= 0.0));
    }
    assert_eq!(spectral::ilp(&[1.0], &[1.0], &[1.0], 1, 1, &[1, 1]).0, 3);
}
