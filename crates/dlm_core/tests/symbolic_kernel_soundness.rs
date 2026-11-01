//! v0.77 — soundness fuzzing of the symbolic proof kernel.
//! v0.78 — inequality goals and hypotheses, and `List<Nat>` statements with
//! structural induction.
//! v0.79 — step-s induction, generalized hypotheses, Bool case split and Bool
//! hypotheses.
//!
//! Random Nat statements are given to the kernel. Every statement the kernel
//! claims to prove (by normalization or by induction) is then evaluated on a
//! grid of values by the runtime. A single counterexample is a soundness bug.

use std::collections::BTreeMap;

use dlm_core::{
    parse_expr, parse_module, prove_by_induction, prove_by_induction_with, prove_by_normalization, FnDecl, FnParam,
    InductionOptions, LemmaRule, ModuleItem, Runtime, TheoryItem,
};

const DEFINITIONS: &str = r#"
module defs
theory T {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn double(n: Nat) -> Nat = n + n
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    fn clamp(n: Nat) -> Nat = if_then_else(n > 3, 3, n)
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    fn total(xs: List<Nat>) -> Nat = fold(xs, 0, fun(acc: Nat, x: Nat, acc + x))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))
    fn dbl_all(xs: List<Nat>) -> List<Nat> = map(xs, double)
    fn sum_list(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + sum_list(tail(xs)))
    fn half(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 0, half(monus(n, 2)) + 1))
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    fn add_acc(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add_acc(pred(a), b + 1))
    fn mul_acc(a: Nat, b: Nat, c: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), c, mul_acc(pred(a), b, c + b))
    fn pick(b: Bool, n: Nat) -> Nat = if_then_else(b, n + 1, n)
    fn sel(n: Nat) -> Nat = match n { 0 => 5, 1 => 0, 3 => 9, succ(k) => k + 1 }
    fn tri(n: Nat) -> Nat = let m = n + 1 in if_then_else(m > 3, mul(m, 2), m)
}
"#;

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
}

fn nat(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(4) == 0 {
        return match rng.below(6) {
            0 | 1 => "n".to_string(),
            2 => "m".to_string(),
            3 => rng.below(4).to_string(),
            4 => format!("sum_to({})", if rng.below(2) == 0 { "n" } else { "m" }),
            _ => format!("double({})", if rng.below(2) == 0 { "n" } else { "m" }),
        };
    }
    match rng.below(8) {
        0 | 1 => format!("{} + {}", nat(rng, depth - 1), nat(rng, depth - 1)),
        2 | 3 => format!("mul({}, {})", nat(rng, depth - 1), nat(rng, depth - 1)),
        4 => format!("pred({})", nat(rng, depth - 1)),
        5 => format!("monus({}, {})", nat(rng, depth - 1), nat(rng, depth - 1)),
        6 => format!("if_then_else({}, {}, {})", boolean(rng, depth - 1), nat(rng, depth - 1), nat(rng, depth - 1)),
        _ => format!("add({}, {})", nat(rng, depth - 1), if rng.below(2) == 0 { "n" } else { "1" }),
    }
}

fn boolean(rng: &mut Rng, depth: u32) -> String {
    match rng.below(3) {
        0 => format!("nat_eq({}, {})", nat(rng, depth), nat(rng, depth)),
        1 => format!("{} > {}", nat(rng, depth), nat(rng, depth)),
        _ => format!("not(nat_eq({}, {}))", nat(rng, depth), nat(rng, depth)),
    }
}

/// Semantics-preserving rewrites, so that a good share of statements is true.
fn variant(rng: &mut Rng, expr: &str) -> String {
    match rng.below(6) {
        0 => format!("{expr} + 0"),
        1 => format!("mul({expr}, 1)"),
        2 => format!("monus({expr} + 2, 2)"),
        3 => format!("pred({expr} + 1)"),
        4 => format!("if_then_else(nat_eq(n, n), {expr}, 7)"),
        _ => expr.to_string(),
    }
}

fn definitions() -> (BTreeMap<String, FnDecl>, Vec<FnDecl>) {
    let module = parse_module(DEFINITIONS).expect("definitions parse");
    let ModuleItem::Theory(theory) = &module.items[0] else { panic!("theory") };
    let decls: Vec<FnDecl> = theory
        .items
        .iter()
        .filter_map(|item| match item {
            TheoryItem::Fn(decl) => Some(decl.clone()),
            _ => None,
        })
        .collect();
    (decls.iter().map(|d| (d.name.clone(), d.clone())).collect(), decls)
}

fn vars() -> Vec<FnParam> {
    vec![FnParam { name: "n".into(), ty: "Nat".into() }, FnParam { name: "m".into(), ty: "Nat".into() }]
}

fn assert_true_on_grid(decls: &[FnDecl], statement: &str) {
    let body = parse_expr(statement, 1).expect("statement parses");
    let outcome = Runtime::new().evaluate_statement("T", decls, &[], &vars(), 8, &body).unwrap_or_else(|err| {
        panic!("kernel proved `{statement}` but evaluation failed: {}", err.message)
    });
    assert!(
        outcome.counterexample.is_none(),
        "SOUNDNESS BUG: kernel proved `{statement}` but it is false at {:?}",
        outcome.counterexample
    );
}

#[test]
fn normalization_never_proves_a_false_statement() {
    let (functions, decls) = definitions();
    let names = vars();
    let mut rng = Rng(0x9E37_79B9_7F4A_7C15);
    let mut proved = 0usize;
    for round in 0..3000 {
        let lhs = nat(&mut rng, 3);
        let rhs = if round % 2 == 0 { variant(&mut rng, &lhs) } else { nat(&mut rng, 3) };
        let statement = format!("nat_eq({lhs}, {rhs})");
        let body = parse_expr(&statement, 1).expect("parse");
        if prove_by_normalization(&functions, &[], &names, &body).is_ok() {
            proved += 1;
            assert_true_on_grid(&decls, &statement);
        }
    }
    eprintln!("normalization fuzz: proved {proved} of 3000");
    assert!(proved > 600, "fuzzer should exercise many successful proofs, got {proved}");
}

#[test]
fn induction_never_proves_a_false_statement() {
    let (functions, decls) = definitions();
    let names = vars();
    let mut rng = Rng(0xD1B5_4A32_D192_ED03);
    let mut proved = 0usize;
    let mut attempted_false = 0usize;
    for round in 0..2000 {
        let lhs = nat(&mut rng, 2);
        let rhs = match round % 3 {
            0 => variant(&mut rng, &lhs),
            1 => nat(&mut rng, 2),
            _ => format!("{lhs} + {}", rng.below(2)),
        };
        let statement = format!("nat_eq({lhs}, {rhs})");
        let body = parse_expr(&statement, 1).expect("parse");
        match prove_by_induction(&functions, &[], &names, "n", &body) {
            Ok(_) => {
                proved += 1;
                assert_true_on_grid(&decls, &statement);
            }
            Err(_) => attempted_false += 1,
        }
    }
    eprintln!("induction fuzz: proved {proved}, rejected {attempted_false} of 2000");
    assert!(proved > 300, "fuzzer should exercise many successful inductions, got {proved}");
    assert!(attempted_false > 300, "fuzzer should also exercise failing inductions, got {attempted_false}");
}

#[test]
fn known_inductive_identities_are_proved() {
    let (functions, _) = definitions();
    let names = vars();
    for (statement, variable) in [
        ("nat_eq(mul(2, sum_to(n)), mul(n, n + 1))", "n"),
        ("nat_eq(add(n, m), n + m)", "n"),
        ("nat_eq(sum_to(n + 1), sum_to(n) + n + 1)", "n"),
    ] {
        let body = parse_expr(statement, 1).unwrap();
        prove_by_induction(&functions, &[], &names, variable, &body).unwrap_or_else(|f| panic!("{statement}: {f}"));
    }
    for statement in ["nat_eq(sum_to(n), n)", "nat_eq(add(n, m), n)", "nat_eq(pred(n) + 1, n)", "nat_eq(clamp(n), n)"] {
        let body = parse_expr(statement, 1).unwrap();
        assert!(prove_by_induction(&functions, &[], &names, "n", &body).is_err(), "{statement} must not be provable");
        assert!(prove_by_normalization(&functions, &[], &names, &body).is_err(), "{statement} must not be provable");
    }
}

fn assert_true_on(decls: &[FnDecl], vars: &[FnParam], bound: u64, statement: &str) {
    let body = parse_expr(statement, 1).expect("statement parses");
    let outcome = Runtime::new().evaluate_statement("T", decls, &[], vars, bound, &body).unwrap_or_else(|err| {
        panic!("kernel proved `{statement}` but evaluation failed: {}", err.message)
    });
    assert!(
        outcome.counterexample.is_none(),
        "SOUNDNESS BUG: kernel proved `{statement}` but it is false at {:?}",
        outcome.counterexample
    );
}

fn small_nat(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(5) {
            0 | 1 => "n".to_string(),
            2 => rng.below(3).to_string(),
            3 => "fact(n)".to_string(),
            _ => "sum_to(n)".to_string(),
        };
    }
    match rng.below(5) {
        0 | 1 => format!("{} + {}", small_nat(rng, depth - 1), small_nat(rng, depth - 1)),
        2 => format!("mul({}, {})", small_nat(rng, depth - 1), small_nat(rng, depth - 1)),
        3 => format!("pred({})", small_nat(rng, depth - 1)),
        _ => format!("monus({}, {})", small_nat(rng, depth - 1), small_nat(rng, depth - 1)),
    }
}

#[test]
fn inequalities_are_never_proved_false() {
    let (functions, decls) = definitions();
    let vars = vec![FnParam { name: "n".into(), ty: "Nat".into() }];
    let mut rng = Rng(0x2545_F491_4F6C_DD1D);
    let (mut proved_normalize, mut proved_induction) = (0usize, 0usize);
    for round in 0..3000 {
        let lhs = small_nat(&mut rng, 2);
        let rhs = small_nat(&mut rng, 2);
        let statement = match round % 3 {
            0 => format!("{lhs} > {rhs}"),
            1 => format!("not({lhs} > {rhs})"),
            _ => format!("{lhs} + {} > {rhs}", rng.below(3)),
        };
        let body = parse_expr(&statement, 1).expect("parse");
        if prove_by_normalization(&functions, &[], &vars, &body).is_ok() {
            proved_normalize += 1;
            assert_true_on(&decls, &vars, 7, &statement);
        }
        if prove_by_induction(&functions, &[], &vars, "n", &body).is_ok() {
            proved_induction += 1;
            assert_true_on(&decls, &vars, 7, &statement);
        }
    }
    eprintln!("inequality fuzz: normalize {proved_normalize}, induction {proved_induction} of 3000");
    assert!(proved_normalize > 200, "got {proved_normalize}");
    assert!(proved_induction > proved_normalize, "induction must prove more: {proved_induction} vs {proved_normalize}");
}

fn list_expr(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(4) {
            0 | 1 => "xs".to_string(),
            2 => "list(1, n)".to_string(),
            _ => "cons(n, xs)".to_string(),
        };
    }
    match rng.below(5) {
        0 => format!("cons({}, {})", list_nat(rng, depth - 1), list_expr(rng, depth - 1)),
        1 => format!("tail({})", list_expr(rng, depth - 1)),
        2 => format!("inc_all({})", list_expr(rng, depth - 1)),
        3 => format!("dbl_all({})", list_expr(rng, depth - 1)),
        _ => format!("if_then_else(is_empty(xs), {}, {})", list_expr(rng, depth - 1), list_expr(rng, depth - 1)),
    }
}

fn list_nat(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(3) {
            0 => "n".to_string(),
            1 => rng.below(3).to_string(),
            _ => "length(xs)".to_string(),
        };
    }
    match rng.below(7) {
        0 => format!("length({})", list_expr(rng, depth - 1)),
        1 => format!("len({})", list_expr(rng, depth - 1)),
        2 => format!("total({})", list_expr(rng, depth - 1)),
        3 => format!("sum_list({})", list_expr(rng, depth - 1)),
        4 => format!("unwrap_or(head({}), {})", list_expr(rng, depth - 1), list_nat(rng, depth - 1)),
        5 => format!("{} + {}", list_nat(rng, depth - 1), list_nat(rng, depth - 1)),
        _ => format!("mul(2, {})", list_nat(rng, depth - 1)),
    }
}

#[test]
fn list_statements_are_never_proved_false() {
    let (functions, decls) = definitions();
    let vars = vec![FnParam { name: "xs".into(), ty: "List<Nat>".into() }, FnParam { name: "n".into(), ty: "Nat".into() }];
    let mut rng = Rng(0x94D0_49BB_1331_11EB);
    let (mut proved, mut rejected) = (0usize, 0usize);
    for round in 0..2000 {
        let lhs = list_nat(&mut rng, 3);
        let rhs = match round % 3 {
            0 => list_nat(&mut rng, 3),
            1 => format!("{lhs} + {}", rng.below(2)),
            _ => lhs.replace("len(", "length(").replace("sum_list(", "total("),
        };
        let statement = if round % 5 == 0 { format!("not({lhs} > {rhs})") } else { format!("nat_eq({lhs}, {rhs})") };
        let body = parse_expr(&statement, 1).expect("parse");
        let by_normalize = prove_by_normalization(&functions, &[], &vars, &body).is_ok();
        let by_induction = prove_by_induction(&functions, &[], &vars, "xs", &body).is_ok();
        if by_normalize || by_induction {
            proved += 1;
            assert_true_on(&decls, &vars, 4, &statement);
        } else {
            rejected += 1;
        }
    }
    eprintln!("list fuzz: proved {proved}, rejected {rejected} of 2000");
    assert!(proved > 300, "fuzzer should exercise many successful list proofs, got {proved}");
    assert!(rejected > 300, "fuzzer should also exercise failing list proofs, got {rejected}");
}

#[test]
fn known_list_identities_are_proved() {
    let (functions, _) = definitions();
    let vars = vec![FnParam { name: "xs".into(), ty: "List<Nat>".into() }];
    for statement in [
        "nat_eq(len(xs), length(xs))",
        "nat_eq(length(inc_all(xs)), length(xs))",
        "nat_eq(length(dbl_all(inc_all(xs))), length(xs))",
        "nat_eq(sum_list(inc_all(xs)), sum_list(xs) + length(xs))",
        "nat_eq(sum_list(dbl_all(xs)), mul(2, sum_list(xs)))",
        "not(length(xs) > sum_list(inc_all(xs)))",
    ] {
        let body = parse_expr(statement, 1).unwrap();
        prove_by_induction(&functions, &[], &vars, "xs", &body).unwrap_or_else(|f| panic!("{statement}: {f}"));
    }
    for statement in ["nat_eq(len(xs), 0)", "nat_eq(sum_list(inc_all(xs)), sum_list(xs))", "length(xs) > 0"] {
        let body = parse_expr(statement, 1).unwrap();
        assert!(prove_by_induction(&functions, &[], &vars, "xs", &body).is_err(), "{statement} must not be provable");
    }
}

fn adv_nat(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(7) {
            0 | 1 => "n".to_string(),
            2 => "m".to_string(),
            3 => rng.below(3).to_string(),
            4 => "half(n)".to_string(),
            5 => format!("pick(b, {})", if rng.below(2) == 0 { "n" } else { "m" }),
            _ => "add_acc(n, m)".to_string(),
        };
    }
    match rng.below(8) {
        0 | 1 => format!("{} + {}", adv_nat(rng, depth - 1), adv_nat(rng, depth - 1)),
        2 => format!("mul(2, {})", adv_nat(rng, depth - 1)),
        3 => format!("half({})", adv_nat(rng, depth - 1)),
        4 => format!("add_acc({}, {})", adv_nat(rng, depth - 1), adv_nat(rng, depth - 1)),
        5 => format!("mul_acc(n, {}, {})", adv_nat(rng, depth - 1), adv_nat(rng, depth - 1)),
        6 => format!("if_then_else(b, {}, {})", adv_nat(rng, depth - 1), adv_nat(rng, depth - 1)),
        _ => format!("monus({}, {})", adv_nat(rng, depth - 1), adv_nat(rng, depth - 1)),
    }
}

fn adv_statement(rng: &mut Rng) -> String {
    let lhs = adv_nat(rng, 2);
    match rng.below(9) {
        6 => format!("even({lhs})"),
        7 => format!("not(even({lhs} + {}))", rng.below(2)),
        8 => format!("or(even({lhs}), b)"),
        0 => format!("nat_eq({lhs}, {})", adv_nat(rng, 2)),
        1 => format!("nat_eq({lhs}, {})", lhs.replace("add_acc(n, m)", "n + m")),
        2 => format!("not({lhs} > {})", adv_nat(rng, 2)),
        3 => format!("{lhs} + 1 > {}", adv_nat(rng, 1)),
        4 => format!("or(even({lhs}), even({lhs} + 1))"),
        _ => format!("even(mul(2, {lhs}))"),
    }
}

#[test]
fn advanced_induction_never_proves_a_false_statement() {
    let (functions, decls) = definitions();
    let vars = vec![
        FnParam { name: "n".into(), ty: "Nat".into() },
        FnParam { name: "m".into(), ty: "Nat".into() },
        FnParam { name: "b".into(), ty: "Bool".into() },
    ];
    let mut rng = Rng(0xA076_1D64_78BD_642F);
    let (mut proved, mut rejected) = (0usize, 0usize);
    let mut by_option = [0usize; 4];
    for _ in 0..2500 {
        let statement = adv_statement(&mut rng);
        let body = parse_expr(&statement, 1).expect("parse");
        let mut any = false;
        if prove_by_normalization(&functions, &[], &vars, &body).is_ok() {
            any = true;
        }
        for (slot, options) in [
            InductionOptions::default(),
            InductionOptions { step: 2, generalizing: Vec::new() },
            InductionOptions { step: 1, generalizing: vec!["m".into()] },
            InductionOptions { step: 2, generalizing: vec!["m".into()] },
        ]
        .into_iter()
        .enumerate()
        {
            if prove_by_induction_with(&functions, &[], &vars, "n", &body, &options).is_ok() {
                by_option[slot] += 1;
                any = true;
            }
        }
        if any {
            proved += 1;
            assert_true_on(&decls, &vars, 7, &statement);
        } else {
            rejected += 1;
        }
    }
    eprintln!("advanced induction fuzz: proved {proved}, rejected {rejected}; per option {by_option:?}");
    assert!(proved > 400 && rejected > 400, "{proved} / {rejected}");
    assert!(by_option[1] > by_option[0], "step 2 must prove more than step 1: {by_option:?}");
    assert!(by_option[2] > by_option[0], "generalizing must prove more: {by_option:?}");
}

#[test]
fn known_advanced_identities_are_proved() {
    let (functions, _) = definitions();
    let vars = vec![
        FnParam { name: "n".into(), ty: "Nat".into() },
        FnParam { name: "m".into(), ty: "Nat".into() },
        FnParam { name: "k".into(), ty: "Nat".into() },
    ];
    let general = |names: &[&str]| InductionOptions { step: 1, generalizing: names.iter().map(|s| s.to_string()).collect() };
    for (statement, options) in [
        ("nat_eq(add_acc(n, m), n + m)", general(&["m"])),
        ("nat_eq(mul_acc(n, m, k), mul(n, m) + k)", general(&["k"])),
        ("not(mul(2, half(n)) > n)", InductionOptions { step: 2, generalizing: Vec::new() }),
        ("not(n > mul(2, half(n)) + 1)", InductionOptions { step: 2, generalizing: Vec::new() }),
        ("or(even(n), even(n + 1))", InductionOptions { step: 2, generalizing: Vec::new() }),
        ("nat_eq(half(mul(2, n) + 1), n)", InductionOptions::default()),
    ] {
        let body = parse_expr(statement, 1).unwrap();
        prove_by_induction_with(&functions, &[], &vars, "n", &body, &options).unwrap_or_else(|f| panic!("{statement}: {f}"));
    }
    for (statement, options) in [
        ("nat_eq(add_acc(n, m), n)", general(&["m"])),
        ("nat_eq(mul_acc(n, m, k), mul(n, m))", general(&["k"])),
        ("even(n)", InductionOptions { step: 2, generalizing: Vec::new() }),
        ("even(n)", InductionOptions::default()),
        ("not(even(n + 1))", InductionOptions::default()),
        ("nat_eq(half(n), n)", InductionOptions { step: 3, generalizing: Vec::new() }),
        ("not(n > mul(2, half(n)))", InductionOptions { step: 4, generalizing: Vec::new() }),
    ] {
        let body = parse_expr(statement, 1).unwrap();
        assert!(prove_by_induction_with(&functions, &[], &vars, "n", &body, &options).is_err(), "{statement} must not be provable");
    }
    // A plain induction keeps the v0.77 entry point.
    let body = parse_expr("nat_eq(add(n, m), n + m)", 1).unwrap();
    assert!(prove_by_induction(&functions, &[], &vars, "n", &body).is_ok());
}

/// v0.80: lemmas with nested call patterns, applied syntactically.
#[test]
fn nested_pattern_lemmas_never_prove_a_false_statement() {
    let (functions, decls) = definitions();
    let list_vars = vec![FnParam { name: "xs".into(), ty: "List<Nat>".into() }];
    // Every lemma is first proved by the kernel itself.
    let mut lemmas = Vec::new();
    for (name, statement) in [
        ("inc_sum", "nat_eq(sum_list(inc_all(xs)), sum_list(xs) + length(xs))"),
        ("len_dbl", "nat_eq(len(dbl_all(xs)), length(xs))"),
        ("dbl_sum", "nat_eq(sum_list(dbl_all(xs)), mul(2, sum_list(xs)))"),
        ("len_inc", "nat_eq(len(inc_all(xs)), length(xs))"),
    ] {
        let body = parse_expr(statement, 1).unwrap();
        prove_by_induction(&functions, &[], &list_vars, "xs", &body).unwrap_or_else(|f| panic!("{name}: {f}"));
        lemmas.push(LemmaRule::from_equation(name, &["xs".to_string()], &body, &functions).unwrap());
    }
    let vars = vec![FnParam { name: "xs".into(), ty: "List<Nat>".into() }, FnParam { name: "n".into(), ty: "Nat".into() }];
    let mut rng = Rng(0x1F12_3BB5_159A_55E5);
    let (mut proved, mut with_lemmas_only) = (0usize, 0usize);
    for round in 0..2000 {
        let e = list_expr(&mut rng, 2);
        let k = rng.below(3);
        let off = if round % 3 == 0 { 1 } else { 0 };
        let statement = match rng.below(6) {
            0 => format!("nat_eq(sum_list(inc_all({e})) + {k}, sum_list({e}) + length({e}) + {})", k + off),
            1 => format!("nat_eq(sum_list(dbl_all({e})), mul(2, sum_list({e})) + {off})"),
            2 => format!("nat_eq(len(dbl_all({e})) + {off}, length({e}))"),
            3 => format!("nat_eq(len(inc_all({e})), length({e}) + {off})"),
            4 => format!("nat_eq(sum_list(dbl_all({e})), sum_list(inc_all({e})))"),
            _ => {
                let lhs = list_nat(&mut rng, 3);
                format!("nat_eq({lhs}, {})", list_nat(&mut rng, 3))
            }
        };
        let body = parse_expr(&statement, 1).expect("parse");
        let plain = prove_by_normalization(&functions, &[], &vars, &body).is_ok();
        let with = prove_by_normalization(&functions, &lemmas, &vars, &body).is_ok();
        if with {
            proved += 1;
            if !plain {
                with_lemmas_only += 1;
            }
            assert_true_on(&decls, &vars, 4, &statement);
        }
    }
    eprintln!("nested-pattern lemma fuzz: proved {proved}, needing lemmas {with_lemmas_only} of 2000");
    assert!(with_lemmas_only > 50, "lemmas must matter: {with_lemmas_only}");
}

fn split_nat(rng: &mut Rng, depth: u32) -> String {
    if depth == 0 || rng.below(3) == 0 {
        return match rng.below(6) {
            0 | 1 => "n".to_string(),
            2 => rng.below(5).to_string(),
            3 => "sel(n)".to_string(),
            4 => "tri(n)".to_string(),
            _ => "clamp(n)".to_string(),
        };
    }
    match rng.below(6) {
        0 => format!("{} + {}", split_nat(rng, depth - 1), split_nat(rng, depth - 1)),
        1 => format!("mul(2, {})", split_nat(rng, depth - 1)),
        2 => format!("(let v = {} in v + {})", split_nat(rng, depth - 1), split_nat(rng, depth - 1)),
        3 => format!("(match {} {{ 0 => {}, succ(j) => j }})", split_nat(rng, depth - 1), split_nat(rng, depth - 1)),
        4 => format!("if_then_else(nat_eq(n, {}), {}, {})", rng.below(4), split_nat(rng, depth - 1), split_nat(rng, depth - 1)),
        _ => format!("monus({}, {})", split_nat(rng, depth - 1), split_nat(rng, depth - 1)),
    }
}

/// v0.82: `let`, `match` and Nat case splitting in `by normalize`.
#[test]
fn case_splitting_never_proves_a_false_statement() {
    let (functions, decls) = definitions();
    let vars = vec![FnParam { name: "n".into(), ty: "Nat".into() }];
    let mut rng = Rng(0x6A09_E667_F3BC_C908);
    let (mut proved, mut rejected) = (0usize, 0usize);
    for round in 0..2500 {
        let lhs = split_nat(&mut rng, 2);
        let rhs = split_nat(&mut rng, 2);
        let statement = match round % 4 {
            0 => format!("nat_eq({lhs}, {rhs})"),
            1 => format!("not({lhs} > {rhs})"),
            2 => format!("{lhs} + 1 > {rhs}"),
            _ => format!("nat_eq({lhs}, {lhs} + {})", rng.below(2)),
        };
        let body = parse_expr(&statement, 1).expect("parse");
        if prove_by_normalization(&functions, &[], &vars, &body).is_ok() {
            proved += 1;
            // The grid goes well past every split threshold (<= 4 here).
            assert_true_on(&decls, &vars, 14, &statement);
        } else {
            rejected += 1;
        }
    }
    eprintln!("case-split fuzz: proved {proved}, rejected {rejected} of 2500");
    assert!(proved > 400 && rejected > 400, "{proved} / {rejected}");
    for statement in ["not(clamp(n) > 3)", "not(sel(n) > n + 6)", "nat_eq(sel(n + 4), n + 4)", "not(tri(n) > mul(2, n) + 2)"] {
        let body = parse_expr(statement, 1).unwrap();
        prove_by_normalization(&functions, &[], &vars, &body).unwrap_or_else(|f| panic!("{statement}: {f}"));
    }
    for statement in ["nat_eq(sel(n), n)", "not(clamp(n) > 2)", "nat_eq(sel(3), 4)", "not(sel(n) > n + 5)"] {
        let body = parse_expr(statement, 1).unwrap();
        assert!(prove_by_normalization(&functions, &[], &vars, &body).is_err(), "{statement} must not be provable");
    }
}
