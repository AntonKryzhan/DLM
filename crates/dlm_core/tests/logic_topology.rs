//! v0.119 — CDCL SAT (`sat_*`), integral homology (`tp_homology`), Rips
//! persistence (`tp_rips`), `std.logic` (CNF, cardinality, Tseitin, n-queens,
//! sudoku, colouring) and `std.topology` (spheres, torus, RP², barcodes).
//! Interpreter and native C print identical lines.
use dlm_core::combinatorics as cb;
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
    let dir = std::env::temp_dir().join(format!("dlm-logic-{}-{name}", std::process::id()));
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
    fn hs(x: Option<List<HomologyGroup>>) -> Text = match x { none => "none", some(h) => join(map(h, group_text), " | ") }
    fn bs(x: Option<List<Bar>>) -> Text = match x { none => "none", some(b) => join(map(b, bar_text), "; ") }
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn fvl(xs: List<Float>) -> FloatVec = unwrap_or(fv_from_list(xs), fv_empty())
    print_line(op(sat_solve(nv(list(0, 2, 1, 2, 3, 4)), nv(list(2, 4, 6)), 3)))
    print_line(nat_to_text(sat_status(nv(list(0, 1)), nv(list(1, 2)), 1)))
    print_line(nat_to_text(sat_status(nv(list(0, 9)), nv(list(2)), 2)))
    print_line(nat_to_text(status(pigeonhole(6))))
    print_line(op(queens(8)))
    print_line(op(queens(3)))
    let puzzle = nv(list(5, 3, 0, 0, 7, 0, 0, 0, 0, 6, 0, 0, 1, 9, 5, 0, 0, 0, 0, 9, 8, 0, 0, 0, 0, 6, 0, 8, 0, 0, 0, 6, 0, 0, 0, 3, 4, 0, 0, 8, 0, 3, 0, 0, 1, 7, 0, 0, 0, 2, 0, 0, 0, 6, 0, 6, 0, 0, 0, 0, 2, 8, 0, 0, 0, 0, 4, 1, 9, 0, 0, 5, 0, 0, 0, 0, 8, 0, 0, 7, 9))
    print_line(op(sudoku(puzzle)))
    let pet = from_edges(10, list(0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9), list(1, 2, 3, 4, 0, 5, 6, 7, 8, 9, 7, 8, 9, 5, 6))
    print_line(op(colouring(pet, 3)))
    print_line(op(colouring(pet, 2)))
    let x = fresh(cnf(2))
    let g = gate_xor(field(x, cnf), pos(0), pos(1))
    print_line(op(sat_model(add_clause(add_clause(field(g, cnf), list(pos(field(g, var)))), list(pos(0))))))
    let f = add_clauses(cnf(3), list(list(pos(0), pos(1)), list(neg(0), pos(2)), list(neg(1), neg(2))))
    print_line(match sat_model(f) { none => "unsat", some(m) => if_then_else(satisfies(f, m), "model ok", "bad model") })
    print_line(hs(homology(circle(5))))
    print_line(hs(homology(sphere(2))))
    print_line(hs(homology(sphere(3))))
    print_line(hs(homology(torus())))
    print_line(hs(homology(projective_plane())))
    print_line(hs(homology(wedge_of_circles(3))))
    print_line(hs(homology(ball(4))))
    print_line(match euler_characteristic(torus()) { none => "none", some(e) => int_to_text(e) })
    print_line(match euler_characteristic(sphere(2)) { none => "none", some(e) => int_to_text(e) })
    print_line(if_then_else(has_torsion(projective_plane()), "torsion", "free"))
    let ring = fvl(list(1.0, 0.0, 0.5, 0.866, -0.5, 0.866, -1.0, 0.0, -0.5, -0.866, 0.5, -0.866))
    print_line(bs(rips_barcode(ring, 2, 1, 3.0)))
    let bars = unwrap_or(rips_barcode(ring, 2, 1, 3.0), empty_list(Bar))
    print_line(nat_to_text(betti_at(bars, 1, 1.2)))
    print_line(nat_to_text(significant(bars, 0, 0.5)))
    print_line(bs(rips_barcode(fvl(list(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)), 3, 2, 5.0)))
"#;

#[test]
fn logic_and_topology_agree_between_interpreter_and_native() {
    let out = run_both("program", &format!("module test.logic\nimport std.topology\ntheory App extends StdTopology {{\n{PROGRAM}}}\n"));
    let want: &[&str] = &[
        "nvec[0, 1, 1]",
        "nvec[0, 1, 1]",
        "1",
        "2",
        "1",
        "nvec[7, 1, 4, 2, 0, 6, 3, 5]",
        "nvec[7, 1, 4, 2, 0, 6, 3, 5]",
        "none",
        "nvec[5, 3, 4, 6, 7, 8, 9, 1, 2, 6, 7, 2, 1, 9, 5, 3, 4, 8, 1, 9, 8, 3, 4, 2, 5, 6, 7, 8, 5, 9, 7, 6, 1, 4, 2, 3, 4, 2, 6, 8, 5, 3, 7, 9, 1, 7, 1, 3, 9, 2, 4, 8, 5, 6, 9, 6, 1, 5, 3, 7, 2, 8, 4, 2, 8, 7, 4, 1, 9, 6, 3, 5, 3, 4, 5, 2, 8, 6, 1, 7, 9]",
        "nvec[5, 3, 4, 6, 7, 8, 9, 1, 2, 6, 7, 2, 1, 9, 5, 3, 4, 8, 1, 9, 8, 3, 4, 2, 5, 6, 7, 8, 5, 9, 7, 6, 1, 4, 2, 3, 4, 2, 6, 8, 5, 3, 7, 9, 1, 7, 1, 3, 9, 2, 4, 8, 5, 6, 9, 6, 1, 5, 3, 7, 2, 8, 4, 2, 8, 7, 4, 1, 9, 6, 3, 5, 3, 4, 5, 2, 8, 6, 1, 7, 9]",
        "nvec[2, 1, 2, 1, 0, 1, 2, 0, 0, 1]",
        "nvec[2, 1, 2, 1, 0, 1, 2, 0, 0, 1]",
        "none",
        "nvec[1, 0, 0, 1]",
        "nvec[1, 0, 0, 1]",
        "model ok",
        "Z | Z",
        "Z | 0 | Z",
        "Z | 0 | 0 | Z",
        "Z | Z^2 | Z",
        "Z | Z/2 | 0",
        "Z | Z^3",
        "Z | 0 | 0 | 0 | 0",
        "0",
        "2",
        "torsion",
        "H0 [0.0, 0.9999779997579946); H0 [0.0, 0.9999779997579946); H0 [0.0, 0.9999779997579946); H0 [0.0, 0.9999779997579946); H0 [0.0, 1.0); H0 [0.0, inf); H1 [1.0, 1.7320381058163818)",
        "1",
        "6",
        "H0 [0.0, 1.0); H0 [0.0, 1.0); H0 [0.0, 1.0); H0 [0.0, inf)",
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
fn sat_matches_brute_force_on_random_3cnf() {
    let mut seed = 7u64;
    for round in 0..300 {
        let nv = 3 + (round % 9) as usize;
        let nc = 1 + (lcg(&mut seed) as usize) % (5 * nv);
        let mut lits = Vec::new();
        let mut ends = Vec::new();
        let mut clauses = Vec::new();
        for _ in 0..nc {
            let c: Vec<u128> = (0..3)
                .map(|_| (lcg(&mut seed) % (2 * nv as u64)) as u128)
                .collect();
            lits.extend(&c);
            ends.push(lits.len() as u128);
            clauses.push(c);
        }
        let brute = (0..1u32 << nv).any(|m| {
            clauses
                .iter()
                .all(|c| c.iter().any(|l| ((m >> (l / 2)) & 1 == 1) == (l % 2 == 0)))
        });
        let (s, model) = cb::sat(&lits, &ends, nv as u128);
        assert_eq!(s == 0, brute, "round {round}");
        if s == 0 {
            assert!(clauses.iter().all(|c| c
                .iter()
                .any(|l| (model[*l as usize / 2] == 1) == (l % 2 == 0))));
        } else {
            assert_eq!(s, 1);
        }
    }
    assert_eq!(cb::sat(&[5], &[1], 2).0, 2);
    assert_eq!(cb::sat(&[0], &[2], 1).0, 2);
    assert_eq!(cb::sat(&[], &[0], 1).0, 1);
}

#[test]
fn homology_and_persistence_kernels() {
    let enc = |ss: &[&[u128]]| {
        let mut f = Vec::new();
        let mut e = Vec::new();
        for s in ss {
            f.extend_from_slice(s);
            e.push(f.len() as u128);
        }
        (f, e)
    };
    // Klein bottle (Z, Z + Z/2, 0) from a 9-vertex triangulation of the identified square
    let grid = [[0u128, 1, 2, 0], [3, 4, 5, 3], [6, 7, 8, 6], [0, 2, 1, 0]];
    let mut tris: Vec<Vec<u128>> = Vec::new();
    for i in 0..3 {
        for j in 0..3 {
            let (a, b, c, d) = (
                grid[i][j],
                grid[i][j + 1],
                grid[i + 1][j],
                grid[i + 1][j + 1],
            );
            tris.push(vec![a, b, d]);
            tris.push(vec![a, c, d]);
        }
    }
    let refs: Vec<&[u128]> = tris.iter().map(|t| t.as_slice()).collect();
    let (f, e) = enc(&refs);
    let h = cb::homology(&f, &e).unwrap();
    assert_eq!(h, vec![3, 1, 0, 1, 1, 2, 0, 0], "{h:?}");
    // Smith normal form invariants
    assert_eq!(
        cb::smith(vec![2, 4, 4, -6, 6, 12, 10, -4, -16], 3, 3),
        Some(vec![2, 6, 12])
    );
    assert_eq!(cb::smith(vec![0, 0, 0, 0], 2, 2), Some(vec![]));
    assert!(cb::homology(&[0, 1], &[1]).is_none());
    // two clusters: one long H0 bar besides the essential class
    let pts = [0.0, 0.0, 0.1, 0.0, 5.0, 0.0, 5.1, 0.0];
    let r = cb::rips(&pts, 2, 0, 10.0).unwrap();
    let bars: Vec<(f64, f64, f64)> = r.chunks(3).map(|c| (c[0], c[1], c[2])).collect();
    assert_eq!(bars.len(), 4);
    assert!(bars.iter().any(|b| (b.2 - 4.9).abs() < 1e-12));
    assert!(bars.iter().any(|b| b.2.is_infinite()));
    assert!(cb::rips(&pts, 3, 0, 1.0).is_none());
}
