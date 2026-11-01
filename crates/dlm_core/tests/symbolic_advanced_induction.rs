//! v0.79 — step-s induction, generalized induction hypotheses, Bool case
//! split, Bool hypotheses, and kernel guards against rewrite cycles.

use dlm_core::{
    audit_certificate_against_theorem, nat_induction_scheme, nat_step_base_case_proposition, nat_step_induction_base_case,
    nat_step_induction_proof, nat_step_induction_scheme, nat_step_induction_step_case, nat_step_step_case_proposition,
    parse_expr, parse_module, CheckReport, Checker, DiagnosticKind, InductionOptions, Kernel, Passport,
    ProofCertificateStatus, TrustLevel, TypeKind, KERNEL_STACK_BYTES, MAX_SYMBOLIC_DEPTH,
};

const EXAMPLE: &str = include_str!("../../../examples/valid/symbolic_advanced_induction.dlm");

fn check(source: &str) -> CheckReport {
    Checker::new().check_module(&parse_module(source).expect("parse"))
}

fn passport<'a>(report: &'a CheckReport, name: &str) -> &'a Passport {
    report.inferred.iter().find(|(n, _)| n == name).map(|(_, p)| p).unwrap_or_else(|| panic!("missing passport {name}"))
}

fn history(report: &CheckReport, name: &str) -> String {
    passport(report, &format!("Advanced.{name}")).to_string()
}

fn rejects_with(source: &str, kind: DiagnosticKind, needle: &str) {
    let diagnostics = match parse_module(source) {
        Ok(module) => Checker::new().check_module(&module).diagnostics,
        Err(diagnostics) => diagnostics,
    };
    assert!(
        diagnostics.iter().any(|d| d.kind == kind && d.message.contains(needle)),
        "expected {kind:?} containing `{needle}`, got {diagnostics:?}"
    );
}

/// The example theory with one extra theorem line.
fn with_theorem(theorem: &str) -> String {
    let mut lines: Vec<&str> = EXAMPLE.lines().filter(|l| !l.trim_start().starts_with("theorem")).collect();
    let close = lines.iter().rposition(|l| l.trim() == "}").unwrap();
    let line = format!("    theorem {theorem}");
    lines.insert(close, &line);
    lines.join("\n")
}

#[test]
fn example_checks_and_certifies() {
    let report = check(EXAMPLE);
    assert!(report.ok(), "{:?}", report.diagnostics);
    assert_eq!(report.certificates.len(), 9);
    for certificate in &report.certificates {
        assert_eq!(certificate.status, ProofCertificateStatus::Checked, "{}", certificate.theorem_name);
        let theorem = passport(&report, &format!("Advanced.{}", certificate.theorem_name));
        assert!(audit_certificate_against_theorem(certificate, theorem, 0).verified());
        assert!(theorem.trust < TrustLevel::Axiom);
        assert!(theorem.to_string().contains("crosscheck:runtime:agree"), "{}", certificate.theorem_name);
    }
    assert_eq!(
        passport(&report, "Advanced.add_acc_spec").ty,
        TypeKind::Theorem { name: "add_acc_spec".into(), proposition: "forall a: Nat, b: Nat. nat_eq(add_acc(a, b), (a + b))".into() }
    );
}

#[test]
fn step_induction_goes_through_the_core_step_scheme() {
    let report = check(EXAMPLE);
    let half = history(&report, "half_bound");
    for event in [
        "induction:nat_step:scheme:step=2:P_half_bound",
        "induction:nat_step:base:P_half_bound(0)",
        "induction:nat_step:base:P_half_bound(1)",
        "induction:nat_step:step:forall n:Nat. P_half_bound(n) and P_half_bound(n+1) -> P_half_bound(n+2)",
        "induction:nat_step:proof:step=2:forall n:Nat. P_half_bound(n)",
        "proof:symbolic:step:normalize+hypothesis_nonnegative_combination",
    ] {
        assert!(half.contains(event), "missing `{event}` in {half}");
    }
    // Bool hypotheses: `or(even(k), even(k+1))` is recognized in the goal.
    assert!(history(&report, "even_or_next").contains("induction:nat_step:proof:step=2"));
}

#[test]
fn step_and_generalization_are_necessary() {
    for (theorem, needle) in [
        ("half_bound(n: Nat): not(mul(2, half(n)) > n) by induction n", "step case n = n_k + 1"),
        ("even_or_next(n: Nat): or(even(n), even(n + 1)) by induction n", "step case n = n_k + 1"),
        ("add_acc_spec(a: Nat, b: Nat): nat_eq(add_acc(a, b), a + b) by induction a", "residual: add_acc(a_k,1 + b) = 1 + a_k + b"),
        (
            "rev_len_spec(xs: List<Nat>, acc: Nat): nat_eq(rev_len(xs, acc), length(xs) + acc) by induction xs",
            "step case xs = cons(xs_h, xs_t)",
        ),
    ] {
        rejects_with(&with_theorem(theorem), DiagnosticKind::StatementTheoremError, needle);
    }
}

#[test]
fn generalized_hypothesis_is_recorded_in_the_family() {
    let report = check(EXAMPLE);
    let spec = history(&report, "add_acc_spec");
    assert!(spec.contains("induction:instantiate:P_add_acc_spec(a) := forall b: Nat. nat_eq(add_acc(a, b), (a + b))"), "{spec}");
    let rev = history(&report, "rev_len_spec");
    assert!(rev.contains("induction:list:proof:forall xs:List<Nat>. P_rev_len_spec(xs)"), "{rev}");
    assert!(rev.contains("forall acc: Nat."), "{rev}");
}

#[test]
fn bool_variables_are_split_by_cases() {
    let report = check(EXAMPLE);
    let pick = history(&report, "pick_ge");
    assert!(pick.contains("proof:symbolic:normalize:cases=2+normalize"), "{pick}");
    assert!(pick.contains("crosscheck:runtime:agree:cases=12:theorem=pick_ge"), "{pick}");
    // Each case is really checked: false in one case is refuted.
    rejects_with(
        &with_theorem("pick_gt(b: Bool, n: Nat): pick(b, n) > n by normalize"),
        DiagnosticKind::StatementTheoremError,
        "goal [b = false]",
    );
    // Induction with Bool parameters splits the Bool cases outside the induction.
    let report = check(&with_theorem("pick_add(b: Bool, n: Nat, m: Nat): not(n + m > pick(b, n) + m) by induction n"));
    assert!(report.ok(), "{:?}", report.diagnostics);
    rejects_with(
        &with_theorem("pick_ind(b: Bool, n: Nat): not(n > pick(b, n)) by induction b"),
        DiagnosticKind::StatementTheoremError,
        "inducts on Bool variable 'b'",
    );
    rejects_with(
        &with_theorem("pick_gen(b: Bool, n: Nat): not(n > pick(b, n)) by induction n generalizing b"),
        DiagnosticKind::StatementTheoremError,
        "generalizes Bool variable 'b'",
    );
}

#[test]
fn proof_clause_options_are_validated() {
    let theory = |theorem: &str| format!("module m\ntheory T {{\n    theorem {theorem}\n}}\n");
    for (theorem, needle) in [
        ("t(n: Nat): nat_eq(n, n) by induction n step 0", "supported steps are 1..=4"),
        ("t(n: Nat): nat_eq(n, n) by induction n step 5", "supported steps are 1..=4"),
        ("t(n: Nat): nat_eq(n, n) by induction n step two", "unknown proof clause"),
        ("t(n: Nat, m: Nat): nat_eq(n, n) by induction n generalizing k", "generalizes unknown variable 'k'"),
        ("t(n: Nat, m: Nat): nat_eq(n, n) by induction n generalizing m, m", "generalizes 'm' twice"),
        ("t(n: Nat, m: Nat): nat_eq(n, n) by normalize generalizing m", "applies only to `by induction`"),
    ] {
        rejects_with(&theory(theorem), DiagnosticKind::ParseError, needle);
    }
    rejects_with(
        include_str!("../../../examples/invalid/generalize_induction_variable.dlm"),
        DiagnosticKind::ParseError,
        "cannot generalize its induction variable 'n'",
    );
    rejects_with(
        include_str!("../../../examples/invalid/induction_step_requires_nat.dlm"),
        DiagnosticKind::StatementTheoremError,
        "step induction needs a Nat variable",
    );
    rejects_with(
        include_str!("../../../examples/invalid/step_induction_false.dlm"),
        DiagnosticKind::StatementTheoremError,
        "base case n = 1 does not normalize to true; residual: false",
    );
    // Options combine with lemmas in any order of the clause parts.
    let module = parse_module(&theory("t(n: Nat, m: Nat): nat_eq(n, n) by induction n step 3 generalizing m using a, b")).unwrap();
    let dlm_core::ModuleItem::Theory(theory) = &module.items[0] else { panic!("theory") };
    let dlm_core::TheoryItem::Statement(statement) = &theory.items[0] else { panic!("statement") };
    let hint = statement.proof.as_ref().unwrap();
    assert_eq!(hint.induction, InductionOptions { step: 3, generalizing: vec!["m".into()] });
    assert_eq!(hint.lemmas, vec!["a".to_string(), "b".to_string()]);
}

#[test]
fn rewrite_cycles_terminate_instead_of_overflowing() {
    // `l` rewrites h(n) -> g(n), and g unfolds back to h(n). Before v0.79 this
    // overflowed the native stack.
    let source = "module m\ntheory T {\n    fn h(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, h(pred(n)))\n    fn g(n: Nat) -> Nat = h(n)\n    theorem l(n: Nat): nat_eq(h(n), g(n)) by normalize\n    theorem t(n: Nat): nat_eq(h(n) + 0, h(n)) by normalize using l\n}\n";
    let report = check(source);
    assert!(report.ok(), "{:?}", report.diagnostics);
}

#[test]
fn deep_unfolding_is_cut_by_the_depth_guard() {
    // Declared bottom-up: a fn may only call fns declared before it.
    let mut source = String::from("module m\ntheory T {\n    fn f400(n: Nat) -> Nat = n\n");
    for i in (0..400).rev() {
        source.push_str(&format!("    fn f{i}(n: Nat) -> Nat = f{}(n) + 1\n", i + 1));
    }
    let module = parse_module(&(source.clone() + "}\n")).unwrap();
    let dlm_core::ModuleItem::Theory(theory) = &module.items[0] else { panic!("theory") };
    let functions = theory
        .items
        .iter()
        .filter_map(|item| match item {
            dlm_core::TheoryItem::Fn(decl) => Some((decl.name.clone(), decl.clone())),
            _ => None,
        })
        .collect();
    // Direct `Kernel::normalize` runs on the caller's stack; give it the
    // kernel stack, as `prove_by_*` do.
    std::thread::scope(|scope| {
        std::thread::Builder::new()
            .stack_size(KERNEL_STACK_BYTES)
            .spawn_scoped(scope, || {
                let kernel = Kernel::new(&functions, &[]);
                let env = [("n".to_string(), dlm_core::Sym::Nat(dlm_core::Poly::atom("n")))].into_iter().collect();
                let error = kernel.normalize(&parse_expr("f0(n)", 1).unwrap(), &env, true).unwrap_err();
                assert_eq!(error.to_string(), format!("normalization nested deeper than {MAX_SYMBOLIC_DEPTH}"));
                let shallow = Kernel::new(&functions, &[]);
                assert!(shallow.normalize(&parse_expr("f300(n)", 1).unwrap(), &env, true).is_ok());
            })
            .unwrap()
            .join()
            .unwrap();
    });
    // Through the checker (and the proof API) the guard yields an error, not
    // an abort, on any caller stack.
    let report = check(&(source + "    theorem deep(n: Nat): nat_eq(f0(n), n + 400) by normalize
}
"));
    assert!(
        report.diagnostics.iter().any(|d| d.message.contains("normalization nested deeper than")),
        "{:?}",
        report.diagnostics
    );
}

#[test]
fn step_induction_core_objects_check_their_cases() {
    let scheme = nat_step_induction_scheme("T", "P", 2, 1).unwrap();
    assert_eq!(scheme.ty.to_string(), "InductionScheme<Nat,step=2,P>");
    assert_eq!(nat_step_step_case_proposition("P", 3), "forall n:Nat. P(n) and P(n+1) and P(n+2) -> P(n+3)");
    let base = |i| Passport::static_proof("T", nat_step_base_case_proposition("P", i), &scheme);
    let b0 = nat_step_induction_base_case("T", &scheme, 0, &base(0), 1).unwrap();
    let b1 = nat_step_induction_base_case("T", &scheme, 1, &base(1), 1).unwrap();
    let step_proof = Passport::static_proof("T", nat_step_step_case_proposition("P", 2), &scheme);
    let step = nat_step_induction_step_case("T", &scheme, &step_proof, 1).unwrap();
    let proof = nat_step_induction_proof("T", &scheme, &[b0.clone(), b1.clone()], &step, 1).unwrap();
    assert_eq!(proof.ty, TypeKind::InductionProof { proposition: "forall n:Nat. P(n)".into() });

    assert!(nat_step_induction_scheme("T", "P", 1, 1).is_err(), "step 1 uses the plain Nat scheme");
    assert!(nat_step_induction_base_case("T", &scheme, 2, &base(2), 1).is_err(), "base outside 0..step");
    assert!(nat_step_induction_base_case("T", &scheme, 0, &base(1), 1).is_err(), "wrong base proposition");
    assert!(nat_step_induction_proof("T", &scheme, std::slice::from_ref(&b0), &step, 1).is_err(), "missing base");
    assert!(nat_step_induction_proof("T", &scheme, &[b1, b0], &step, 1).is_err(), "bases out of order");
    let plain = nat_induction_scheme("T", "P", 1).unwrap();
    assert!(nat_step_induction_step_case("T", &plain, &step_proof, 1).is_err(), "a plain scheme is not a step scheme");
}
