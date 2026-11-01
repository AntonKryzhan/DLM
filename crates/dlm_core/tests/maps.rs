//! v0.122 — native ordered maps `NatMap` / `TextMap` (`km_*`, `tm_*`) and
//! `std.maps`. Interpreter and native C print identical lines; the host
//! operations agree with `BTreeMap`.
use dlm_core::maps;
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    parse_module, CheckPolicy, Checker, Program, Runtime,
};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn run_both(name: &str, source: &str) -> Vec<String> {
    let dir = std::env::temp_dir().join(format!("dlm-maps-{}-{name}", std::process::id()));
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

const BUILTINS: &str = r#"    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn km(x: Option<NatMap>) -> NatMap = unwrap_or(x, km_empty())
    fn tmo(x: Option<TextMap>) -> TextMap = unwrap_or(x, tm_empty())
    fn nv(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    fn ovf() -> Option<Nat> = match km_add(km(km_insert(km_empty(), 1, 340282366920938463463374607431768211455)), 1, 1) { none => none(Nat), some(m) => km_get(m, 1) }
    let a = km(km_insert(km(km_insert(km(km_insert(km_empty(), 5, 50)), 1, 10)), 9, 90))
    print_line(print_symbolic(a))
    print_line(on(km_get(a, 5)))
    print_line(on(km_get(a, 6)))
    print_line(print_symbolic(km_remove(a, 1)))
    print_line(print_symbolic(km_remove(a, 2)))
    print_line(if_then_else(km_contains(a, 9), "has 9", "no 9"))
    print_line(nat_to_text(km_len(a)))
    print_line(print_symbolic(km_keys(a)))
    print_line(print_symbolic(km_values(a)))
    print_line(print_symbolic(km(km_from(nv(list(3, 1, 3, 2)), nv(list(30, 10, 33, 20))))))
    print_line(print_symbolic(km(km_add(km(km_add(a, 5, 1)), 7, 2))))
    print_line(on(ovf()))
    print_line(on(km_min(a)))
    print_line(on(km_max(a)))
    print_line(on(km_floor(a, 6)))
    print_line(on(km_floor(a, 0)))
    print_line(on(km_ceil(a, 6)))
    print_line(on(km_ceil(a, 10)))
    print_line(print_symbolic(km(km_merge(a, km(km_insert(km_empty(), 5, 500))))))
    print_line(print_symbolic(km_range(a, 2, 9)))
    print_line(if_then_else(equal(a, km(km_from(nv(list(9, 5, 1)), nv(list(90, 50, 10))))), "equal", "different"))
    let t = tm_count_words("the cat and the hat and the bat")
    print_line(print_symbolic(t))
    print_line(on(tm_get(t, "the")))
    print_line(join(tm_keys(t), ","))
    print_line(print_symbolic(tm_values(t)))
    print_line(print_symbolic(tm_remove(t, "cat")))
    print_line(print_symbolic(tmo(tm_insert(t, "ant", 7))))
    print_line(print_symbolic(tmo(tm_from(list("b", "a", "b"), nv(list(1, 2, 3))))))
    print_line(print_symbolic(tmo(tm_merge(t, tmo(tm_add(tm_empty(), "the", 100))))))
    print_line(if_then_else(tm_contains(t, "hat"), "hat", "-"))
    print_line(nat_to_text(tm_len(tm_count_words("  ünïcode ünïcode  x  "))))
"#;

#[test]
fn map_builtins_agree_between_interpreter_and_native() {
    let out = run_both(
        "builtins",
        &format!("module test.maps\ntheory App {{\n{BUILTINS}}}\n"),
    );
    let want: &[&str] = &[
        "nmap{1: 10, 5: 50, 9: 90}",
        "nmap{1: 10, 5: 50, 9: 90}",
        "50",
        "none",
        "nmap{5: 50, 9: 90}",
        "nmap{5: 50, 9: 90}",
        "nmap{1: 10, 5: 50, 9: 90}",
        "nmap{1: 10, 5: 50, 9: 90}",
        "has 9",
        "3",
        "nvec[1, 5, 9]",
        "nvec[1, 5, 9]",
        "nvec[10, 50, 90]",
        "nvec[10, 50, 90]",
        "nmap{1: 10, 2: 20, 3: 33}",
        "nmap{1: 10, 2: 20, 3: 33}",
        "nmap{1: 10, 5: 51, 7: 2, 9: 90}",
        "nmap{1: 10, 5: 51, 7: 2, 9: 90}",
        "none",
        "1",
        "9",
        "5",
        "none",
        "9",
        "none",
        "nmap{1: 10, 5: 500, 9: 90}",
        "nmap{1: 10, 5: 500, 9: 90}",
        "nmap{5: 50}",
        "nmap{5: 50}",
        "equal",
        "tmap{\"and\": 2, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 3}",
        "tmap{\"and\": 2, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 3}",
        "3",
        "and,bat,cat,hat,the",
        "nvec[2, 1, 1, 1, 3]",
        "nvec[2, 1, 1, 1, 3]",
        "tmap{\"and\": 2, \"bat\": 1, \"hat\": 1, \"the\": 3}",
        "tmap{\"and\": 2, \"bat\": 1, \"hat\": 1, \"the\": 3}",
        "tmap{\"and\": 2, \"ant\": 7, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 3}",
        "tmap{\"and\": 2, \"ant\": 7, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 3}",
        "tmap{\"a\": 2, \"b\": 3}",
        "tmap{\"a\": 2, \"b\": 3}",
        "tmap{\"and\": 2, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 100}",
        "tmap{\"and\": 2, \"bat\": 1, \"cat\": 1, \"hat\": 1, \"the\": 100}",
        "hat",
        "2",
    ];
    assert_eq!(out, want);
}

const LIBRARY: &str = r#"    fn on(x: Option<Nat>) -> Text = match x { none => "none", some(v) => nat_to_text(v) }
    fn nvl(xs: List<Nat>) -> NatVec = unwrap_or(nv_from_list(xs), nv_empty())
    let c = counts(nvl(list(4, 1, 4, 2, 4, 1)))
    print_line(print_symbolic(c))
    print_line(print_symbolic(km_map_values(c, fun(v: Nat, mul(v, 10)))))
    print_line(print_symbolic(km_filter(c, fun(k: Nat, v: Nat, v > 1))))
    print_line(nat_to_text(km_fold(c, 0, fun(acc: Nat, k: Nat, v: Nat, acc + mul(k, v)))))
    print_line(on(km_sum_values(c)))
    print_line(print_symbolic(km_invert(c)))
    print_line(print_symbolic(unwrap_or(km_add_all(c, c), km_empty())))
    print_line(on(km_argmax(c)))
    print_line(print_symbolic(first_positions(nvl(list(7, 3, 7, 5, 3)))))
    let words = tm_count_words("to be or not to be that is the question to")
    print_line(join(most_common(words, 3), " "))
    print_line(print_symbolic(text_counts(list("x", "y", "x"))))
    print_line(print_symbolic(unwrap_or(tm_add_all(text_counts(list("x")), text_counts(list("x", "z"))), tm_empty())))
"#;

#[test]
fn std_maps_agrees_between_interpreter_and_native() {
    let out = run_both(
        "library",
        &format!(
            "module test.maps_lib\nimport std.maps\ntheory App extends StdMaps {{\n{LIBRARY}}}\n"
        ),
    );
    let want: &[&str] = &[
        "nmap{1: 2, 2: 1, 4: 3}",
        "nmap{1: 2, 2: 1, 4: 3}",
        "nmap{1: 20, 2: 10, 4: 30}",
        "nmap{1: 20, 2: 10, 4: 30}",
        "nmap{1: 2, 4: 3}",
        "nmap{1: 2, 4: 3}",
        "16",
        "6",
        "nmap{1: 2, 2: 1, 3: 4}",
        "nmap{1: 2, 2: 1, 3: 4}",
        "nmap{1: 4, 2: 2, 4: 6}",
        "nmap{1: 4, 2: 2, 4: 6}",
        "4",
        "nmap{3: 1, 5: 3, 7: 0}",
        "nmap{3: 1, 5: 3, 7: 0}",
        "to be is",
        "tmap{\"x\": 2, \"y\": 1}",
        "tmap{\"x\": 2, \"y\": 1}",
        "tmap{\"x\": 2, \"z\": 1}",
        "tmap{\"x\": 2, \"z\": 1}",
    ];
    assert_eq!(out, want);
}

#[test]
fn host_maps_match_btreemap() {
    let mut s = 1u64;
    let mut m: Vec<(u128, u128)> = Vec::new();
    let mut r: BTreeMap<u128, u128> = BTreeMap::new();
    for _ in 0..5000 {
        s = s
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        let k = ((s >> 33) % 200) as u128;
        let v = (s >> 13) as u128 % 1000;
        match (s >> 60) % 3 {
            0 => {
                m = maps::insert(&m, k, v).unwrap();
                r.insert(k, v);
            }
            1 => {
                m = maps::remove(&m, &k);
                r.remove(&k);
            }
            _ => {
                m = maps::add(&m, k, v).unwrap();
                *r.entry(k).or_insert(0) += v;
            }
        }
        assert_eq!(maps::get(&m, &k), r.get(&k).copied());
        assert_eq!(maps::floor(&m, &k), r.range(..=k).next_back().map(|e| *e.0));
        assert_eq!(maps::ceil(&m, &k), r.range(k..).next().map(|e| *e.0));
    }
    assert_eq!(m, r.into_iter().collect::<Vec<_>>());
    assert_eq!(
        maps::from_pairs(vec![2u128, 1, 2], &[5, 6, 7]),
        Some(vec![(1, 6), (2, 7)])
    );
    assert_eq!(maps::add(&[(1u128, u128::MAX)], 1, 1), None);
}

#[test]
fn maps_are_typed() {
    let check = |body: &str| {
        let src = format!("module test.maps\ntheory App {{\n{body}\n}}\n");
        let p = Program::single(parse_module(&src).unwrap());
        Checker::with_policy(CheckPolicy::trusted_only())
            .check_program(&p)
            .diagnostics
            .len()
    };
    assert!(check("fn f(m: NatMap) -> Option<Nat> = km_get(m, \"a\")") > 0);
    assert!(check("fn f(m: TextMap) -> Option<Nat> = tm_get(m, 1)") > 0);
    assert!(check("fn f(m: NatMap) -> Nat = tm_len(m)") > 0);
    assert_eq!(
        check("fn f(m: TextMap) -> Bool = equal(m, tm_remove(m, \"x\"))"),
        0
    );
    assert_eq!(
        check("fn f(m: NatMap) -> Nat = unwrap_or(km_get(m, 1), 0) + km_len(m)"),
        0
    );
}
