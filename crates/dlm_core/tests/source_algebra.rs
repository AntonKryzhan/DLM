//! Source libraries checked against host algebra and native execution.
use dlm_core::math::algebra::{FiniteGroup, FiniteRing};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program,
    CheckPolicy, Checker, Runtime,
};
use std::path::{Path, PathBuf};
use std::process::Command;
fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}
fn check(source: &str, expected: &[String], name: &str) {
    let dir =
        std::env::temp_dir().join(format!("dlm-source-algebra-{}-{name}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.dlm");
    std::fs::write(&path, source).unwrap();
    let p = load_program_with_paths(&path, &[repo().join("lib")]).unwrap();
    let report = Checker::with_policy(CheckPolicy::trusted_only()).check_program(&p);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(
        Runtime::new().run_program(&p).unwrap().output,
        expected,
        "interpreter: {name}"
    );
    let ir = lower_program(&p, CheckPolicy::trusted_only()).unwrap_or_else(|e| panic!("{e:?}"));
    let exe = build_c_executable(
        &emit_c(&ir),
        &dir.join("main.c"),
        &dir.join("main"),
        &default_c_compiler(),
    )
    .unwrap();
    let out = Command::new(exe).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        String::from_utf8(out.stdout).unwrap().trim_end(),
        expected.join("\n"),
        "C: {name}"
    );
    std::fs::remove_dir_all(dir).unwrap();
}
fn list(xs: &[usize]) -> String {
    if xs.is_empty() {
        "empty_list(Nat)".into()
    } else {
        format!(
            "list({})",
            xs.iter()
                .map(usize::to_string)
                .collect::<Vec<_>>()
                .join(", ")
        )
    }
}
fn group(g: &FiniteGroup) -> String {
    format!(
        "FiniteGroup({}, {}, {})",
        g.order(),
        g.identity(),
        list(g.table())
    )
}
fn ring(r: &FiniteRing) -> String {
    format!(
        "FiniteRing({}, {}, {}, {}, {})",
        r.order(),
        r.zero(),
        r.one(),
        list(r.addition_table()),
        list(r.multiplication_table())
    )
}
fn lines(xs: &[&str]) -> Vec<String> {
    xs.iter().map(|s| s.to_string()).collect()
}
#[test]
fn all_order_two_tables_match_host_for_groups_and_rings() {
    let mut source=String::from("module test.small_algebra\nimport std.finite_algebra\ntheory App extends StdFiniteAlgebra {\n");
    let mut expected = Vec::new();
    for code in 0..16 {
        let table = (0..4).map(|i| (code >> i) & 1).collect::<Vec<_>>();
        for unit in 0..2 {
            source += &format!(
                "    print_symbolic(group_valid(FiniteGroup(2, {unit}, {})))\n",
                list(&table)
            );
            expected.push(FiniteGroup::new(2, unit, table.clone()).is_ok().to_string());
        }
        source += &format!(
            "    print_symbolic(ring_valid(FiniteRing(2, 0, 1, list(0, 1, 1, 0), {})))\n",
            list(&table)
        );
        expected.push(
            FiniteRing::new(2, 0, 1, vec![0, 1, 1, 0], table)
                .is_ok()
                .to_string(),
        );
    }
    source += "}\n";
    check(&source, &expected, "tables");
}
#[test]
fn quotients_kernels_and_first_isomorphism_match_host_and_c() {
    let ps = [
        [0, 1, 2],
        [0, 2, 1],
        [1, 0, 2],
        [1, 2, 0],
        [2, 0, 1],
        [2, 1, 0],
    ];
    let mut table = Vec::new();
    for a in ps {
        for b in ps {
            let p = [a[b[0]], a[b[1]], a[b[2]]];
            table.push(ps.iter().position(|&v| v == p).unwrap());
        }
    }
    let s3 = FiniteGroup::new(6, 0, table).unwrap();
    let c4 = FiniteGroup::cyclic(4).unwrap();
    let c2 = FiniteGroup::cyclic(2).unwrap();
    let source = format!(
        r#"module test.quotients
import std.finite_algebra
theory App extends StdFiniteAlgebra {{
    fn projection(q: Result<FiniteQuotient, Text>) -> List<Nat> = match q {{ ok(v) => field(v, projection), err(_) => empty_list(Nat) }}
    fn induced(q: Result<FiniteGroupIsomorphism, Text>) -> List<Nat> = match q {{ ok(v) => field(v, induced), err(_) => empty_list(Nat) }}
    let g = {}
    let c4 = {}
    let c2 = {}
    print_symbolic(group_abelian(g))
    print_symbolic(group_normal(g, list(0, 3, 4)))
    print_symbolic(group_normal(g, list(0, 1)))
    print_symbolic(projection(group_quotient(g, list(4, 0, 3))))
    print_symbolic(is_err(group_quotient(g, list(0, 1))))
    print_symbolic(group_kernel(c4, c2, list(0, 1, 0, 1)))
    print_symbolic(group_image(c4, c2, list(0, 1, 0, 1)))
    print_symbolic(induced(group_first_isomorphism(c4, c2, list(0, 1, 0, 1))))
    print_symbolic(is_ok(group_first_isomorphism(g, c2, list(0, 1, 1, 0, 0, 1))))
    print_symbolic(group_subgroup(c4, list(2, 0)))
    print_symbolic(is_err(group_subgroup(g, list(0, 0))))
}}
"#,
        group(&s3),
        group(&c4),
        group(&c2)
    );
    check(&source,&lines(&["false","true","false","[0, 1, 1, 0, 0, 1]","true","ok([0, 2])","ok([0, 1])","[0, 1]","true","ok(FiniteSubgroup{group: FiniteGroup{order: 2, unit: 0, operation: [0, 1, 1, 0]}, embedding: [0, 2]})","true"]),"quotients");
}
#[test]
fn extension_field_and_module_linearity_have_distinct_laws() {
    let f4 = FiniteRing::new(
        4,
        0,
        1,
        (0..16).map(|i| (i / 4) ^ (i % 4)).collect(),
        vec![0, 0, 0, 0, 0, 1, 2, 3, 0, 2, 3, 1, 0, 3, 1, 2],
    )
    .unwrap();
    let mut ut_times = Vec::new();
    for a in 0..8 {
        for b in 0..8 {
            let diagonal0 = (a & 1) * (b & 1);
            let upper = ((a & 1) * ((b >> 1) & 1)) ^ (((a >> 1) & 1) * ((b >> 2) & 1));
            let diagonal1 = ((a >> 2) & 1) * ((b >> 2) & 1);
            ut_times.push(diagonal0 | (upper << 1) | (diagonal1 << 2));
        }
    }
    let ut = FiniteRing::new(
        8,
        0,
        5,
        (0..64).map(|i| (i / 8) ^ (i % 8)).collect(),
        ut_times,
    )
    .unwrap();
    let source = format!(
        r#"module test.modules
import std.finite_algebra
theory App extends StdFiniteAlgebra {{
    let f4 = {}
    let f2 = {}
    let z4 = {}
    let frobenius = list(0, 1, 3, 2)
    let v4 = FiniteGroup(4, 0, list(0, 1, 2, 3, 1, 0, 3, 2, 2, 3, 0, 1, 3, 2, 1, 0))
    let m = FiniteModule(4, 0, field(v4, operation), list(0, 0, 0, 0, 0, 1, 2, 3))
    let fallback = FiniteModule(0, 0, empty_list(Nat), empty_list(Nat))
    let reg4 = unwrap_or(module_regular(f4), fallback)
    let reg2 = unwrap_or(module_regular(f2), fallback)
    print_symbolic(ring_is_field(f4))
    print_symbolic(ring_is_field(z4))
    print_symbolic(field_inverse(f4, 2))
    print_symbolic(ring_homomorphism(f4, f4, frobenius))
    print_symbolic(group_homomorphism(v4, v4, frobenius))
    print_symbolic(module_homomorphism(f4, reg4, reg4, frobenius))
    print_symbolic(module_homomorphism(f2, m, reg2, list(0, 1, 0, 1)))
    print_symbolic(module_kernel(f2, m, reg2, list(0, 1, 0, 1)))
    print_symbolic(module_image(f2, m, reg2, list(0, 1, 0, 1)))
    print_symbolic(ring_kernel(z4, f2, list(0, 1, 0, 1)))
    print_symbolic(ring_image(z4, f2, list(0, 1, 0, 1)))
    print_symbolic(ring_homomorphism(z4, f2, list(0, 0, 0, 0)))
    print_symbolic(is_err(module_check(f2, FiniteModule(4, 0, field(v4, operation), list(0)))))
    let upper_triangular = {}
    print_symbolic(ring_valid(upper_triangular))
    print_symbolic(ring_is_field(upper_triangular))
    print_symbolic(ring_mul(upper_triangular, 1, 2))
    print_symbolic(ring_mul(upper_triangular, 2, 1))
    let zero_ring = FiniteRing(1, 0, 0, list(0), list(0))
    print_symbolic(ring_valid(zero_ring))
    print_symbolic(ring_is_field(zero_ring))
    print_symbolic(is_ok(module_check(zero_ring, FiniteModule(1, 0, list(0), list(0)))))
    print_symbolic(is_err(module_check(zero_ring, FiniteModule(2, 0, list(0, 1, 1, 0), list(0, 0)))))
}}
"#,
        ring(&f4),
        ring(&FiniteRing::residues(2).unwrap()),
        ring(&FiniteRing::residues(4).unwrap()),
        ring(&ut)
    );
    check(
        &source,
        &lines(&[
            "true",
            "false",
            "ok(3)",
            "true",
            "true",
            "false",
            "true",
            "ok([0, 2])",
            "ok([0, 1])",
            "ok([0, 2])",
            "ok([0, 1])",
            "false",
            "true",
            "true",
            "false",
            "ok(2)",
            "ok(0)",
            "true",
            "false",
            "true",
            "true",
        ]),
        "modules",
    );
}
#[test]
fn source_primality_and_prime_arithmetic_match_integer_calculation() {
    let mut source =
        String::from("module test.prime\nimport std.prime\ntheory App extends StdPrime {\n");
    let mut expected = Vec::new();
    for p in 0..=260 {
        source += &format!("    print_symbolic(fp_prime_test({p}))\n");
        expected.push(if p > 251 {
            "err(\"primality test bound exceeded\")".into()
        } else {
            format!("ok({})", p >= 2 && (2..p).all(|d| p % d != 0))
        });
    }
    for p in [2usize, 3, 5, 7] {
        for a in 0..p {
            for b in 0..p {
                source+=&format!("    print_symbolic(fp_add({p}, {a}, {b}))\n    print_symbolic(fp_mul({p}, {a}, {b}))\n    print_symbolic(fp_sub({p}, {a}, {b}))\n");
                expected.extend([
                    format!("ok({})", (a + b) % p),
                    format!("ok({})", (a * b) % p),
                    format!("ok({})", (a + p - b) % p),
                ]);
            }
        }
        for a in 0..p {
            source += &format!("    print_symbolic(fp_neg({p}, {a}))\n");
            expected.push(format!("ok({})", (p - a) % p));
        }
        for a in 1..p {
            let inv = (1..p).find(|b| a * b % p == 1).unwrap();
            source += &format!("    print_symbolic(fp_inv({p}, {a}))\n");
            expected.push(format!("ok({inv})"));
            source += &format!("    print_symbolic(fp_div({p}, 1, {a}))\n");
            expected.push(format!("ok({inv})"));
        }
    }
    source +=
        "    print_symbolic(fp_mul(251, 250, 250))\n    print_symbolic(fp_pow(7, 2, 1024))\n}\n";
    expected.extend(["ok(1)".into(), "ok(2)".into()]);
    check(&source, &expected, "prime");
}
#[test]
fn raw_records_bad_maps_and_huge_naturals_cannot_bypass_source_checks() {
    let source = r#"module test.errors
import std.finite_algebra
theory App extends StdFiniteAlgebra {
    let g = FiniteGroup(2, 0, list(0, 1, 1, 0))
    let badg = FiniteGroup(2, 0, list(0, 1, 1, 999))
    let r = FiniteRing(2, 0, 1, list(0, 1, 1, 0), list(0, 0, 0, 1))
    print_symbolic(is_err(group_check(badg)))
    print_symbolic(is_err(group_mul(g, 340282366920938463463374607431768211455, 0)))
    print_symbolic(is_err(group_cyclic(340282366920938463463374607431768211455)))
    print_symbolic(is_err(group_inverse(g, 2)))
    print_symbolic(is_err(group_kernel(g, g, list(0, 999))))
    print_symbolic(is_err(group_first_isomorphism(g, g, list(0, 999))))
    print_symbolic(is_err(ring_check(FiniteRing(340282366920938463463374607431768211455, 0, 0, empty_list(Nat), empty_list(Nat)))))
    print_symbolic(is_err(ring_check(FiniteRing(2, 0, 2, list(0, 1, 1, 0), list(0, 0, 0, 1)))))
    print_symbolic(is_err(ring_add(r, 340282366920938463463374607431768211455, 1)))
    print_symbolic(is_err(field_inverse(r, 0)))
    print_symbolic(is_err(module_scale(r, FiniteModule(2, 0, list(0, 1, 1, 0), list(0, 0, 0, 2)), 1, 1)))
    print_symbolic(is_err(fp_add(7, 340282366920938463463374607431768211455, 1)))
    print_symbolic(is_err(fp_mul(9, 1, 1)))
    print_symbolic(is_err(fp_pow(7, 1, 340282366920938463463374607431768211455)))
    print_symbolic(is_err(fp_inv(7, 0)))
    print_symbolic(is_err(fp_div(7, 1, 0)))
    print_symbolic(is_err(fp_prime_test(257)))
}
"#;
    check(source, &vec!["true".into(); 17], "errors");
}

#[test]
fn record_labels_are_metadata_while_real_recursion_is_still_checked() {
    let source = r#"module test.labels
import std.finite_algebra
theory App extends StdFiniteAlgebra {
    record Labels(projection: Nat, recurse: Nat, read_nat: Nat)
    fn projection(p: Labels) -> Nat = field(p, projection)
    fn recurse(n: Nat, p: Labels) -> Nat decreasing n = if_then_else(nat_eq(n, 0), field(p, recurse), recurse(pred(n), p) + field(p, recurse))
    fn readout(p: Labels) -> Nat = field(p, read_nat)
    let p = Labels(7, 2, 42)
    print_decimal(projection(p))
    print_decimal(recurse(2, p))
    print_decimal(readout(p))
    print_symbolic(map(list(p), readout))
}
"#;
    check(source, &lines(&["7", "6", "42", "[42]"]), "labels");
    let module=dlm_core::parse_module("module m\ntheory T {\n record Box(count: Nat)\n fn f(n: Nat) -> Nat = field(Box(f(n)), count)\n}\n").unwrap();
    let report = Checker::new().check_module(&module);
    assert!(report
        .diagnostics
        .iter()
        .any(|d| d.kind == dlm_core::DiagnosticKind::RecursionBoundaryError));
}
