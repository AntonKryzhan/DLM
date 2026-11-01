//! v0.120 — multivariate polynomials over F_p and reduced Gröbner bases
//! (`mv_*`, `gb_*`), `std.ideals` (ideal membership, elimination) and
//! `std.categories` (FinSet limits and colimits). Interpreter and native C
//! print identical lines; bases are checked against ideal identities.
use dlm_core::groebner as gb;
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
    let dir = std::env::temp_dir().join(format!("dlm-groebner-{}-{name}", std::process::id()));
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
    fn op(x: Option<NatVec>) -> Text = match x { none => "none", some(v) => print_symbolic(v) }
    let p = 32003
    print_line(op(gb_basis(nv(list(1, 2, 0, 6, 0, 1, 1, 1, 1, 6, 0, 0)), nv(list(6, 12)), 2, 7, 0)))
    print_line(op(gb_basis(nv(list(1, 2, 0, 6, 0, 1, 1, 1, 1, 6, 0, 0)), nv(list(6, 12)), 2, 7, 2)))
    print_line(op(gb_basis(nv(list(1, 2, 0, 0, 1, 0, 2, 0, 1, 0, 0, 2, 32002, 0, 0, 0, 1, 1, 0, 0, 32002, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 32002, 0, 0, 0)), nv(list(16, 28, 36)), 3, p, 0)))
    print_line(op(gb_basis(nv(list(1, 1, 0, 1, 0, 1, 32002, 0, 0)), nv(list(9)), 2, p, 1)))
    print_line(op(gb_basis(nv(list(1, 1, 0, 1, 0, 0)), nv(list(3, 6)), 2, 5, 0)))
    print_line(op(gb_basis(nv_empty(), nv_empty(), 2, 5, 0)))
    print_line(op(gb_reduce(nv(list(1, 3, 0)), nv(list(1, 1, 0, 6, 0, 2, 1, 0, 3, 6, 0, 0)), nv(list(6, 12)), 2, 7, 0)))
    print_line(op(mv_mul(nv(list(1, 1, 0, 1, 0, 1)), nv(list(1, 1, 0, 6, 0, 1)), 2, 7)))
    print_line(op(mv_add(nv(list(3, 1, 0)), nv(list(4, 1, 0, 1, 0, 0)), 2, 7)))
    print_line(op(mv_sub(nv(list(3, 1, 0)), nv(list(3, 1, 0)), 2, 7)))
    print_line(op(mv_normalize(nv(list(1, 0, 3, 1, 1, 1, 1, 2, 0)), 2, 7, 0)))
    print_line(op(mv_normalize(nv(list(1, 0, 3, 1, 1, 1, 1, 2, 0)), 2, 7, 2)))
    print_line(op(gb_basis(nv(list(1, 1)), nv(list(2)), 2, 7, 0)))
    print_line(op(gb_basis(nv(list(1, 0, 0)), nv(list(3)), 2, 8, 0)))
"#;

#[test]
fn groebner_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.groebner\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &[
        "nvec[2, 6, 6, 1, 1, 0, 6, 0, 2, 1, 0, 3, 6, 0, 0]",
        "nvec[2, 6, 6, 1, 1, 0, 6, 0, 2, 1, 0, 3, 6, 0, 0]",
        "nvec[3, 6, 6, 6, 1, 2, 0, 6, 0, 1, 1, 1, 1, 6, 0, 0, 1, 0, 2, 6, 1, 0]",
        "nvec[3, 6, 6, 6, 1, 2, 0, 6, 0, 1, 1, 1, 1, 6, 0, 0, 1, 0, 2, 6, 1, 0]",
        "nvec[3, 8, 4, 8, 1, 1, 0, 0, 16001, 0, 1, 0, 1, 0, 2, 0, 1, 0, 0, 1, 32002, 0, 0, 0]",
        "nvec[3, 8, 4, 8, 1, 1, 0, 0, 16001, 0, 1, 0, 1, 0, 2, 0, 1, 0, 0, 1, 32002, 0, 0, 0]",
        "nvec[1, 9, 1, 1, 0, 1, 0, 1, 32002, 0, 0]",
        "nvec[1, 9, 1, 1, 0, 1, 0, 1, 32002, 0, 0]",
        "nvec[1, 3, 1, 0, 0]",
        "nvec[1, 3, 1, 0, 0]",
        "nvec[0]",
        "nvec[0]",
        "nvec[1, 0, 0]",
        "nvec[1, 0, 0]",
        "nvec[1, 2, 0, 6, 0, 2]",
        "nvec[1, 2, 0, 6, 0, 2]",
        "nvec[1, 0, 0]",
        "nvec[1, 0, 0]",
        "nvec[]",
        "nvec[]",
        "nvec[1, 2, 0, 1, 1, 1, 1, 0, 3]",
        "nvec[1, 2, 0, 1, 1, 1, 1, 0, 3]",
        "nvec[1, 0, 3, 1, 2, 0, 1, 1, 1]",
        "nvec[1, 0, 3, 1, 2, 0, 1, 1, 1]",
        "none",
        "none",
    ];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn ob(x: Option<Bool>) -> Text = match x { none => "none", some(b) => if_then_else(b, "true", "false") }
    fn obs(x: Option<Basis>) -> Text = match x { none => "none", some(b) => basis_text(b) }
    fn om(x: Option<FinMap>) -> Text = match x { none => "none", some(m) => map_text(m) }
    fn u(x: Option<MPoly>) -> MPoly = unwrap_or(x, mv_zero(0, 2))
    fn fm(d: Nat, c: Nat, xs: List<Nat>) -> FinMap = unwrap_or(fin_map(d, c, xs), identity_map(0))
    let p = 32003
    let x = mv_var(3, p, 0)
    let y = mv_var(3, p, 1)
    let z = mv_var(3, p, 2)
    let one = mv_const(3, p, 1)
    let f1 = u(mv_minus(mv_sum(list(mv_power(x, 2), mv_power(y, 2), mv_power(z, 2)), 3, p), one))
    let f2 = u(mv_minus(mv_sum(list(mv_power(x, 2), mv_power(z, 2)), 3, p), y))
    let f3 = u(mv_minus(x, z))
    print_line(mv_text(f1))
    print_line(obs(groebner(list(f1, f2, f3), 3, p, lex())))
    print_line(obs(groebner(list(f1, f2, f3), 3, p, grevlex())))
    print_line(ob(ideal_contains(list(f1, f2, f3), u(mv_minus(mv_power(y, 2), mv_const(3, p, 1))), lex())))
    print_line(ob(ideal_contains(list(f1, f2, f3), u(mv_plus(u(mv_times(f1, x)), u(mv_times(f3, y)))), grevlex())))
    print_line(ob(ideal_contains(list(f1, f2, f3), y, lex())))
    print_line(match eliminate(list(f1, f2, f3), 3, p, 2) { none => "none", some(es) => join(map(es, mv_text), ", ") })
    print_line(if_then_else(is_unit_ideal(unwrap_or(groebner(list(x, u(mv_minus(x, one))), 3, p, lex()), Basis(empty_list(MPoly), 0))), "unit", "proper"))
    print_line(nat_to_text(mv_eval(f1, unwrap_or(nv_from_list(list(1, 0, 0)), nv_empty()))))
    let cx = mv_var(2, 7, 0)
    let cy = mv_var(2, 7, 1)
    print_line(obs(groebner(list(u(mv_minus(mv_power(cx, 2), cy)), u(mv_minus(u(mv_times(cx, cy)), mv_const(2, 7, 1)))), 2, 7, lex())))
    let f = fm(4, 3, list(0, 1, 1, 2))
    let g = fm(4, 3, list(0, 1, 2, 2))
    print_line(om(compose_maps(fm(3, 2, list(1, 0, 1)), f)))
    print_line(print_symbolic(image(f)))
    print_line(if_then_else(is_surjective(f), "epi", "not epi"))
    print_line(om(equalizer(f, g)))
    print_line(om(coequalizer(f, g)))
    print_line(match pullback(fm(2, 2, list(0, 1)), fm(3, 2, list(1, 1, 0))) { none => "none", some(s) => concat(map_text(field(s, left)), concat(" | ", map_text(field(s, right)))) })
    print_line(match pushout(fm(1, 2, list(0)), fm(1, 3, list(2))) { none => "none", some(c) => concat(map_text(field(c, left)), concat(" | ", map_text(field(c, right)))) })
    print_line(om(pairing(proj1(2, 3), proj2(2, 3))))
    print_line(om(copairing(inj1(2, 3), inj2(2, 3))))
    print_line(om(inverse_map(fm(3, 3, list(2, 0, 1)))))
    print_line(map_text(characteristic(inj2(2, 3))))
    print_line(om(compose_maps(mono_part(f), epi_part(f))))
    print_line(nat_to_text(hom_count(3, 4)))
"#;

#[test]
fn ideals_and_categories_agree_between_interpreter_and_native() {
    let out = run_both("library", &format!("module test.ideals\nimport std.categories\ntheory App extends StdCategories {{\n{LIBRARY}}}\n"));
    let want: &[&str] = &[
        "x1^2 + x2^2 + x3^2 + 32002",
        "x1 + 32002*x3, x2 + 32001*x3^2, x3^4 + 16002*x3^2 + 24002",
        "x2^2 + x2 + 32002, x3^2 + 16001*x2, x1 + 32002*x3",
        "false",
        "true",
        "false",
        "x3^4 + 16002*x3^2 + 24002",
        "unit",
        "0",
        "x1 + 6*x2^2, x2^3 + 6",
        "4 -> 2 [1,0,0,1]",
        "nvec[0, 1, 2]",
        "nvec[0, 1, 2]",
        "epi",
        "3 -> 4 [0,1,3]",
        "3 -> 2 [0,1,1]",
        "3 -> 2 [0,1,1] | 3 -> 3 [2,0,1]",
        "2 -> 4 [0,1] | 3 -> 4 [2,3,0]",
        "6 -> 6 [0,1,2,3,4,5]",
        "5 -> 5 [0,1,2,3,4]",
        "3 -> 3 [1,2,0]",
        "5 -> 2 [0,0,1,1,1]",
        "4 -> 3 [0,1,1,2]",
        "64",
    ];
    assert_eq!(out, want);
}

fn split(code: &[u128]) -> Vec<Vec<u128>> {
    let k = code[0] as usize;
    let mut off = 1 + k;
    let mut out = Vec::new();
    for i in 0..k {
        let len = code[1 + i] as usize;
        out.push(code[off..off + len].to_vec());
        off += len;
    }
    out
}

#[test]
fn reduced_bases_are_canonical_and_generate_the_ideal() {
    let p = 101u128;
    // Katsura-3 style system in 3 variables, plus a random combination
    let f1 = vec![1, 1, 0, 0, 2, 0, 1, 0, 2, 0, 0, 1, p - 1, 0, 0, 0];
    let f2 = vec![1, 2, 0, 0, 2, 0, 2, 0, 2, 0, 0, 2, p - 1, 1, 0, 0];
    let f3 = vec![2, 1, 1, 0, 2, 0, 1, 1, p - 1, 0, 1, 0];
    for order in 0..3u128 {
        let sys = |ps: &[&Vec<u128>]| {
            let mut v = Vec::new();
            let mut e = Vec::new();
            for f in ps {
                v.extend(f.iter());
                e.push(v.len() as u128);
            }
            (v, e)
        };
        let (v, e) = sys(&[&f1, &f2, &f3]);
        let b = gb::gb_basis(&v, &e, 3, p, order).unwrap();
        // independent of generator order and of adding ideal members
        let combo =
            gb::mv_binary(&gb::mv_binary(&f1, &f3, 3, p, 2).unwrap(), &f2, 3, p, 0).unwrap();
        let (v2, e2) = sys(&[&f3, &combo, &f1, &f2]);
        assert_eq!(
            gb::gb_basis(&v2, &e2, 3, p, order).unwrap(),
            b,
            "order {order}"
        );
        // every generator reduces to zero modulo the basis
        let polys = split(&b);
        let mut bv = Vec::new();
        let mut be = Vec::new();
        for g in &polys {
            bv.extend(g.iter());
            be.push(bv.len() as u128);
        }
        for f in [&f1, &f2, &f3, &combo] {
            assert!(gb::gb_reduce(f, &bv, &be, 3, p, order).unwrap().is_empty());
        }
        // basis elements are monic
        for g in &polys {
            assert_eq!(g[0], 1);
        }
    }
    // inconsistent system: x = 0, x = 1 → (1)
    let b = gb::gb_basis(&[1, 1, 1, 1, p - 1, 0], &[2, 6], 1, p, 0).unwrap();
    assert_eq!(b, vec![1, 2, 1, 0]);
    assert!(gb::gb_basis(&[1, 0], &[2], 1, 100, 0).is_none());
    assert!(gb::gb_basis(&[1, 0], &[2], 1, 101, 3).is_none());
}
