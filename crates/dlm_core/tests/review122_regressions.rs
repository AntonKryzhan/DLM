//! Boundary and independent-oracle regressions for 0.119–0.122.
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use dlm_core::{combinatorics as cb, groebner as gb, spectral};
use std::path::Path;
use std::process::Command;

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-review122-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let p = load_program_with_paths(&path, &[root.join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let interpreted = Runtime::new().run_program(&p).unwrap().output;
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap();
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let output = Command::new(exe).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let native: Vec<_> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect();
    assert_eq!(interpreted, native);
    std::fs::remove_dir_all(dir).unwrap();
    native
}

#[test]
fn malformed_certificate_lengths_are_rejected_without_panics() {
    for p in [
        vec![1, usize::MAX as u128],
        vec![1, (usize::MAX - 1) as u128],
        vec![u128::MAX],
        vec![1, 3, 0],
    ] {
        assert_eq!(cb::sat_check(&[0], &[1], 1, &p), None);
    }
}

#[test]
fn polynomial_outputs_stay_in_their_input_domain() {
    let limit = (1u128 << 31) - 1;
    assert_eq!(gb::mv_binary(&[1, limit], &[1, 1], 1, 7, 2), None);
    let f = [1, limit, 0];
    let g = [1, 1, 0, 6, 0, limit];
    assert!(gb::gb_reduce(&f, &g, &[6], 2, 7, 0).is_none());
    let out = gb::mv_binary(&[1, limit - 1], &[1, 1], 1, 7, 2).unwrap();
    assert_eq!(out, vec![1, limit]);
    assert_eq!(gb::mv_normalize(&out, 1, 7, 2), Some(out));
}

#[test]
fn polynomial_pair_selection_is_bounded() {
    // Coprime generators still consume work while selecting Buchberger pairs.
    let mut terms = Vec::new();
    let mut ends = Vec::new();
    for _ in 0..400 {
        terms.extend([1, 0]);
        ends.push(terms.len() as u128);
    }
    assert!(gb::gb_basis(&terms, &ends, 1, 7, 0).is_none());
}

#[test]
fn rips_preserves_small_and_large_distances() {
    assert!(cb::rips(&[0.0], 1, 0, -1.0).is_none());
    for d in [1e-200, 1e200] {
        let bars = cb::rips(&[0.0, d], 1, 0, d).unwrap();
        assert_eq!(bars.len(), 6);
        assert_eq!(&bars[..3], &[0.0, 0.0, d]);
        assert!(bars[5].is_infinite());
    }
    assert!(cb::rips(&[-f64::MAX, f64::MAX], 1, 0, f64::INFINITY).is_none());
    assert!(cb::rips(&[], u128::MAX, 0, 1.0).is_none());
}

#[test]
fn ilp_does_not_confuse_unbounded_relaxation_with_integer_feasibility() {
    // x = 1/2, y >= 0, maximize y. The relaxation is unbounded; no integer x exists.
    let a = [1.0, 0.0, -1.0, 0.0];
    let b = [0.5, -0.5];
    assert_eq!(spectral::ilp(&[0.0, 1.0], &a, &b, 2, 2, &[1, 0]).0, 1);
    assert_eq!(
        spectral::ilp(&[0.0, 1.0], &a, &[1.0, -1.0], 2, 2, &[1, 0]).0,
        2
    );
    assert_eq!(
        spectral::ilp(&[1.0], &[1.0], &[0.9999995], 1, 1, &[1]),
        (0, vec![0.0])
    );
}

#[test]
fn small_objective_ilps_match_integer_enumeration() {
    let mut seed = 31u64;
    let mut next = || {
        seed = seed
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        seed >> 33
    };
    for _ in 0..100 {
        let c = [
            (next() % 7 + 1) as f64 * 1e-7,
            (next() % 7 + 1) as f64 * 1e-7,
        ];
        let a = [
            (next() % 5 + 1) as f64,
            (next() % 5 + 1) as f64,
            (next() % 5 + 1) as f64,
            (next() % 5 + 1) as f64,
        ];
        let b = [(next() % 15 + 3) as f64, (next() % 15 + 3) as f64];
        let mut want = 0.0f64;
        for x in 0..20 {
            for y in 0..20 {
                let (x, y) = (x as f64, y as f64);
                if a[0] * x + a[1] * y <= b[0] && a[2] * x + a[3] * y <= b[1] {
                    want = want.max(c[0] * x + c[1] * y);
                }
            }
        }
        let (status, x) = spectral::ilp(&c, &a, &b, 2, 2, &[1, 1]);
        assert_eq!(status, 0);
        assert!(
            (c[0] * x[0] + c[1] * x[1] - want).abs() < 1e-12,
            "{c:?} {a:?} {b:?} {x:?} {want}"
        );
    }
}

#[test]
fn counting_handles_deep_formulas_within_work_limits() {
    // Many independent (a∨b) clauses exercise depth without a call-stack recursion.
    let pairs = 3000usize;
    let lits: Vec<_> = (0..2 * pairs).map(|v| 2 * v as u128).collect();
    let ends: Vec<_> = (1..=pairs).map(|v| 2 * v as u128).collect();
    assert!(cb::sat_count(&lits, &ends, (2 * pairs) as u128).is_none());
    assert_eq!(cb::sat_count(&[], &[], 127), Some(1u128 << 127));
    assert_eq!(cb::sat_count(&[], &[], 128), None);
}

#[test]
fn smith_shape_validation_and_independent_two_by_two_minors() {
    assert!(cb::smith(vec![], 1, 1).is_none());
    for a in -3i128..=3 {
        for b in -3i128..=3 {
            for c in -3i128..=3 {
                for d in -3i128..=3 {
                    let actual = cb::smith(vec![a, b, c, d], 2, 2).unwrap();
                    let gcd = |mut x: i128, mut y: i128| {
                        while y != 0 {
                            (x, y) = (y, x % y);
                        }
                        x.abs()
                    };
                    let first = gcd(gcd(a, b), gcd(c, d));
                    let det = (a * d - b * c).abs();
                    let want = if first == 0 {
                        vec![]
                    } else if det == 0 {
                        vec![first]
                    } else {
                        vec![first, det / first]
                    };
                    assert_eq!(actual, want, "{a} {b} {c} {d}");
                }
            }
        }
    }
}

#[test]
fn new_builtins_agree_at_boundaries() {
    let out = run_both(
        "builtins",
        r#"module test.review122_native
theory App {
    fn check(b: Bool) -> Text = if_then_else(b, "ok", "FAIL")
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn fv(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    print_line(check(is_none(sat_check(nv(list(0)), nv(list(1)), 1, nv(list(1, 18446744073709551615))))))
    print_line(check(is_none(mv_mul(nv(list(1, 2147483647)), nv(list(1, 1)), 1, 7))))
    print_line(check(is_none(tp_rips(fv(list(0.0)), 1, 0, -1.0))))
    print_line(check(is_none(tp_rips(fv_empty(), 340282366920938463463374607431768211455, 0, 1.0))))
    let lits = unwrap_or(nv_from_list(map(nv_to_list(unwrap_or(nv_range(0, 6000), nv_empty())), fun(v: Nat, mul(2, v)))), nv_empty())
    let ends = unwrap_or(nv_from_list(map(nv_to_list(unwrap_or(nv_range(1, 3000), nv_empty())), fun(v: Nat, mul(2, v)))), nv_empty())
    print_line(check(is_none(sat_count(lits, ends, 6000))))
    let polys = fold(nv_to_list(unwrap_or(nv_range(0, 400), nv_empty())), nv_empty(), fun(acc: NatVec, i: Nat, unwrap_or(nv_concat(acc, nv(list(1, 0))), acc)))
    let poly_ends = unwrap_or(nv_from_list(map(nv_to_list(unwrap_or(nv_range(1, 400), nv_empty())), fun(v: Nat, mul(2, v)))), nv_empty())
    print_line(check(is_none(gb_basis(polys, poly_ends, 1, 7, 0))))
    let bars = unwrap_or(tp_rips(fv(list(0.0, fl_lit("1e-200"))), 1, 0, fl_lit("1e-200")), fv_empty())
    print_line(check(and(nat_eq(fv_len(bars), 6), fl_eq(unwrap_or(fv_get(bars, 2), 0.0), fl_lit("1e-200")))))
    print_line(check(nat_eq(ilp_status(fv(list(0.0, 1.0)), fv(list(1.0, 0.0, -1.0, 0.0)), fv(list(0.5, -0.5)), 2, 2, nv(list(1, 0))), 1)))
    let x = unwrap_or(ilp_solve(fv(list(1.0)), fv(list(1.0)), fv(list(0.9999995)), 1, 1, nv(list(1))), fv_empty())
    print_line(check(fl_eq(unwrap_or(fv_get(x, 0), -1.0), 0.0)))
}
"#,
    );
    assert!(!out.is_empty() && out.iter().all(|s| s == "ok"), "{out:?}");
}

#[test]
fn libraries_reject_incompatible_descriptions_and_maps_preserve_keys() {
    let out = run_both(
        "libraries",
        r#"module test.review122_library
import std.maps
theory App extends StdMaps {
    fn check(b: Bool) -> Text = if_then_else(b, "ok", "FAIL")
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    let a = mv_var(1, 7, 0)
    let b = mv_var(1, 5, 0)
    print_line(check(is_none(groebner(list(b), 1, 7, lex()))))
    print_line(check(is_none(normal_form(a, Basis(list(b), lex())))))
    print_line(check(not(basis_equal(Basis(list(a), lex()), Basis(list(b), lex())))))
    print_line(check(not(satisfies(Cnf(1, nv(list(0)), nv_empty()), nv(list(1))))))
    print_line(check(not(satisfies(cnf(1), nv_empty()))))
    print_line(check(is_none(sudoku(unwrap_or(nv_new(81, 10), nv_empty())))))
    let bad = FinMap(1, 1, nv_empty())
    print_line(check(is_none(compose_maps(identity_map(1), bad))))
    print_line(check(not(is_iso(bad))))
    print_line(check(is_none(coequalizer(FinMap(1, 340282366920938463463374607431768211455, nv(list(0))), FinMap(1, 340282366920938463463374607431768211455, nv(list(0)))))))
    print_line(check(is_none(copairing(FinMap(340282366920938463463374607431768211455, 1, nv_empty()), bad))))
    print_line(check(is_none(pushout(FinMap(0, 340282366920938463463374607431768211455, nv_empty()), identity_map(0)))))
    print_line(check(is_none(pairing(FinMap(1, 340282366920938463463374607431768211455, nv(list(0))), FinMap(1, 2, nv(list(0)))))))
    fn make_key(i: Nat) -> Text = concat("ключ", nat_to_text(i))
    fn build(k: Nat, m: TextMap) -> TextMap decreasing k = if_then_else(nat_eq(k, 0), m, build(pred(k), unwrap_or(tm_add(m, make_key(k), 1), m)))
    let words = build(100, tm_count_words("a b\ta　b"))
    print_line(check(nat_eq(unwrap_or(tm_get(words, "a"), 0), 2)))
    print_line(check(nat_eq(unwrap_or(tm_get(words, "ключ37"), 0), 1)))
    let nuls = unwrap_or(tm_from(list("a\0b", "a", "a\0b"), nv(list(1, 2, 3))), tm_empty())
    print_line(check(and(nat_eq(tm_len(nuls), 2), nat_eq(unwrap_or(tm_get(nuls, "a\0b"), 0), 3))))
    print_line(check(equal(most_common(text_counts(list("b", "a", "c")), 3), list("a", "b", "c"))))
}
"#,
    );
    assert!(!out.is_empty() && out.iter().all(|s| s == "ok"), "{out:?}");
}
