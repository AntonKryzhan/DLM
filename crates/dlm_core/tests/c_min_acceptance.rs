//! v0.101 — acceptance of the bounded transition criterion C-min (ROADMAP.md,
//! «C-min»): independent replay of mathematical Nat equalities in
//! `nat-semiring-v1`. One test per condition; the CLI parts (replay after the
//! source is deleted, exit codes) are in `dlm_cli/tests/cli_c_min.rs`.
use dlm_core::proof_kernel::{
    builder::build_equational_proof, check_kernel_proof, AlgebraLaw, KernelAxiom, KernelBudget,
    KernelContext, KernelErrorKind, KernelProof, KernelRule, KernelTerm,
};
use dlm_core::{
    build_c_executable, default_c_compiler, emit_c, function_contracts, lower_program,
    parse_module, CheckPolicy, Checker, ModuleItem, Program, StatementItem, TheoryItem,
};

fn statement(params: &str, body: &str) -> StatementItem {
    let source =
        format!("module m\ntheory T {{\n    theorem t({params}): {body} by normalize\n}}\n");
    let module = parse_module(&source).unwrap();
    let ModuleItem::Theory(t) = &module.items[0] else {
        panic!()
    };
    let TheoryItem::Statement(s) = &t.items[0] else {
        panic!()
    };
    s.clone()
}

fn produced(params: &str, body: &str) -> KernelProof {
    build_equational_proof(&statement(params, body))
        .unwrap_or_else(|| panic!("no kernel proof for {body}"))
}

fn checked(
    p: &KernelProof,
) -> Result<dlm_core::proof_kernel::KernelCheck, dlm_core::proof_kernel::KernelError> {
    check_kernel_proof(p, &KernelContext::default(), KernelBudget::default())
}

fn kind(p: &KernelProof) -> KernelErrorKind {
    checked(p).unwrap_err().kind
}

/// A proof written by hand (not produced from a source statement): x*(y+0) = y*x.
fn hand_proof() -> KernelProof {
    KernelProof {
        variables: vec!["x".into(), "y".into()],
        terms: vec![
            KernelTerm::Var(0),    // 0 x
            KernelTerm::Var(1),    // 1 y
            KernelTerm::Nat(0),    // 2 0
            KernelTerm::Add(1, 2), // 3 y + 0
            KernelTerm::Mul(0, 3), // 4 x*(y+0)
            KernelTerm::Mul(1, 0), // 5 y*x
        ],
        steps: vec![
            KernelRule::Refl(0),                              // 0: x = x
            KernelRule::Law(AlgebraLaw::AddZero, vec![1]),    // 1: y + 0 = y
            KernelRule::Mul(0, 1),                            // 2: x*(y+0) = x*y
            KernelRule::Law(AlgebraLaw::MulComm, vec![0, 1]), // 3: x*y = y*x
            KernelRule::Trans(2, 3),                          // 4: x*(y+0) = y*x
        ],
        goal: (4, 5),
        root: 4,
    }
}

/// Condition 1: A structured DAG is checked without the source checker, the runtime or
/// proof search: the kernel API takes only the artifact and a context.
#[test]
fn c_min_1_dag_checked_without_checker_runtime_or_search() {
    let p = hand_proof();
    let report = checked(&p).unwrap();
    assert_eq!((report.checked_terms, report.checked_steps), (6, 5));
    assert!(report.used_axioms.is_empty());
    assert_eq!(
        p.proposition(KernelBudget::default()).unwrap(),
        "forall x: Nat, y: Nat. nat_eq(mul(x, (y + 0)), mul(y, x))"
    );
    // The text form round-trips and is checked from text alone.
    let text = p.to_text();
    let parsed = KernelProof::from_text(&text, KernelBudget::default()).unwrap();
    assert_eq!(parsed, p);
    assert!(checked(&parsed).is_ok());
    // A false claim cannot be replayed whatever the producer says: no rule
    // derives x + 1 = x.
    let mut false_claim = p.clone();
    false_claim.terms.push(KernelTerm::Nat(1));
    false_claim.terms.push(KernelTerm::Add(0, 6));
    false_claim.goal = (7, 0);
    assert_eq!(kind(&false_claim), KernelErrorKind::Invalid);
}

/// Condition 2: Variable scopes, every reference and the exact match of the root with
/// the declared equality are checked; replacing the goal or a step is
/// rejected even after the certificate fingerprint is recomputed.
#[test]
fn c_min_2_scopes_references_root_and_rehashed_tampering() {
    let p = hand_proof();
    let mut out_of_scope = p.clone();
    out_of_scope.terms[1] = KernelTerm::Var(2);
    assert_eq!(kind(&out_of_scope), KernelErrorKind::Invalid);
    let mut forward = p.clone();
    forward.terms[3] = KernelTerm::Add(1, 4);
    assert_eq!(kind(&forward), KernelErrorKind::Invalid);
    let mut self_term = p.clone();
    self_term.terms[3] = KernelTerm::Add(3, 2);
    assert_eq!(kind(&self_term), KernelErrorKind::Invalid);
    let mut forward_premise = p.clone();
    forward_premise.steps[2] = KernelRule::Mul(0, 2);
    assert_eq!(kind(&forward_premise), KernelErrorKind::Invalid);
    let mut swapped_goal = p.clone();
    swapped_goal.goal = (5, 4);
    assert_eq!(kind(&swapped_goal), KernelErrorKind::Invalid);
    let mut other_root = p.clone();
    other_root.root = 2;
    assert_eq!(kind(&other_root), KernelErrorKind::Invalid);
    let mut missing_root = p.clone();
    missing_root.root = 99;
    assert_eq!(kind(&missing_root), KernelErrorKind::Invalid);
    let mut wrong_law_arguments = p.clone();
    wrong_law_arguments.steps[3] = KernelRule::Law(AlgebraLaw::MulComm, vec![1, 0]);
    assert_eq!(kind(&wrong_law_arguments), KernelErrorKind::Invalid);
    let mut duplicate_binder = p.clone();
    duplicate_binder.variables[1] = "x".into();
    assert_eq!(kind(&duplicate_binder), KernelErrorKind::Invalid);
    let mut bad_binder = p.clone();
    bad_binder.variables[0] = "x y".into();
    assert_eq!(kind(&bad_binder), KernelErrorKind::Invalid);

    // Certificates: tamper, recompute the fingerprint, still rejected.
    let source = "module m\ntheory T {\n    theorem shape(a: Nat, b: Nat): nat_eq(mul(a, b + 0), mul(b, a)) by normalize\n}\n";
    let report = Checker::new().check_module(&parse_module(source).unwrap());
    assert!(report.ok(), "{:?}", report.diagnostics);
    let certificate = &report.certificates[0];
    let theorem = &report
        .inferred
        .iter()
        .find(|(n, _)| n == "T.shape")
        .unwrap()
        .1;
    assert!(
        certificate.kernel.is_some()
            && dlm_core::verify_certificate_against_theorem(certificate, theorem, 1).is_ok()
    );
    let tamper = |edit: &dyn Fn(&mut KernelProof)| {
        let mut bad = certificate.clone();
        edit(bad.kernel.as_mut().unwrap());
        bad.fingerprint = bad.recomputed_fingerprint();
        assert!(bad.fingerprint_is_stable());
        assert!(dlm_core::verify_certificate_against_theorem(&bad, theorem, 1).is_err());
        assert!(!dlm_core::audit_certificate_against_theorem(&bad, theorem, 1).verified());
    };
    tamper(&|k| k.goal = (k.goal.1, k.goal.0));
    tamper(&|k| k.steps[k.root] = KernelRule::Refl(k.goal.0));
    tamper(&|k| *k = hand_proof()); // a valid proof of another proposition
    tamper(&|k| k.variables.reverse());
}

/// Condition 3: Axioms only from an explicitly supplied external context; an artifact
/// cannot declare axioms for itself (the format has no declaration line) and
/// the standalone check uses the empty context.
#[test]
fn c_min_3_axioms_only_from_an_external_context() {
    let mut p = hand_proof();
    p.terms.push(KernelTerm::Nat(1));
    p.terms.push(KernelTerm::Add(0, 6)); // 7: x + 1
    p.steps.push(KernelRule::Axiom(0)); // claims x + 1 = x
    p.goal = (7, 0);
    p.root = 5;
    assert_eq!(
        kind(&p),
        KernelErrorKind::Invalid,
        "no axiom without a context"
    );
    let axiom = KernelAxiom {
        name: "absurd".into(),
        variables: p.variables.clone(),
        terms: vec![
            KernelTerm::Var(0),
            KernelTerm::Nat(1),
            KernelTerm::Add(0, 1),
        ],
        equation: (2, 0),
    };
    let context = KernelContext {
        axioms: vec![axiom.clone()],
    };
    let report = check_kernel_proof(&p, &context, KernelBudget::default()).unwrap();
    assert_eq!(
        report.used_axioms,
        vec!["absurd".to_string()],
        "the dependency stays visible"
    );
    // the axiom owns its scope: different binders are refused
    let mut other_scope = axiom;
    other_scope.variables = vec!["u".into(), "v".into()];
    assert!(check_kernel_proof(
        &p,
        &KernelContext {
            axioms: vec![other_scope]
        },
        KernelBudget::default()
    )
    .is_err());
    // the text form carries only the reference; declarations are not part of it
    let text = p.to_text();
    assert!(text.contains("proof axiom 0") && !text.contains("absurd"));
    let declared = text.replace(
        "rules nat-semiring-v1\n",
        "rules nat-semiring-v1\naxiom absurd x + 1 = x\n",
    );
    assert!(KernelProof::from_text(&declared, KernelBudget::default()).is_err());
    let reparsed = KernelProof::from_text(&text, KernelBudget::default()).unwrap();
    assert_eq!(kind(&reparsed), KernelErrorKind::Invalid);
    // the source producer never emits axiom steps
    let produced = produced("a: Nat", "nat_eq(mul(a, 1) + 0, a)");
    assert!(!produced
        .steps
        .iter()
        .any(|s| matches!(s, KernelRule::Axiom(_))));
}

/// Condition 4: Wrong versions, rules, cycles, damaged format and extra data are
/// rejected; budget exhaustion is UnknownWithinBudget, not Invalid.
#[test]
fn c_min_4_format_strictness_and_budget_versus_falsity() {
    let text = hand_proof().to_text();
    let parse = |t: &str| KernelProof::from_text(t, KernelBudget::default());
    assert!(parse(&text).is_ok());
    for bad in [
        text.replace("DLM-KERNEL-PROOF v1", "DLM-KERNEL-PROOF v2"),
        text.replace("rules nat-semiring-v1", "rules nat-semiring-v2"),
        text.replace("proof refl 0", "proof cycle 0"),
        text.replace("law add_zero 1", "law add_zero 1 2"),
        text.replace("law mul_comm 0 1", "law mul_commutes 0 1"),
        text.replace("term nat 0", "term nat -1"),
        text.replace("goal 4 5", "goal 4 5 6"),
        text.replace("root 4\n", ""),
        text.replace("END-DLM-KERNEL-PROOF\n", ""),
        format!("{text}trailing\n"),
        format!("{text}\n"),
        text.replace("term nat 0", "term nat +0"),
        text.replace("term nat 0", "term nat 00"),
        text.replace("goal 4 5", "goal 04 5"),
        text.replace("goal 4 5", "goal  4 5"),
        text.replace("goal 4 5", "goal 4 5 "),
        text.replace('\n', "\r\n"),
        text.replacen("var x\n", "var x\n\n", 1),
        text.replace("var y\n", "term var 0\nvar y\n"),
        text.replace("root 4\n", "root 4\nroot 4\n"),
        String::new(),
    ] {
        assert!(parse(&bad).is_err(), "accepted:\n{bad}");
    }
    // cycles and forward edges are unrepresentable as checked DAGs
    let mut cyclic = hand_proof();
    cyclic.steps[4] = KernelRule::Trans(4, 3);
    assert_eq!(kind(&cyclic), KernelErrorKind::Invalid);
    // budgets: exhaustion is distinct from an invalid proof
    let p = produced(
        "a: Nat, b: Nat, c: Nat",
        "nat_eq(mul(a + b, c) + 0, mul(a, c) + mul(b, c))",
    );
    assert!(checked(&p).is_ok());
    for budget in [
        KernelBudget {
            max_steps: p.steps.len() - 1,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_nodes: p.terms.len() - 1,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_work: 3,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_depth: 1,
            ..KernelBudget::default()
        },
    ] {
        let error = check_kernel_proof(&p, &KernelContext::default(), budget).unwrap_err();
        assert_eq!(error.kind, KernelErrorKind::UnknownWithinBudget, "{error}");
    }
    let small_text = KernelBudget {
        max_text_bytes: text.len() - 1,
        ..KernelBudget::default()
    };
    assert_eq!(
        KernelProof::from_text(&text, small_text).unwrap_err().kind,
        KernelErrorKind::UnknownWithinBudget
    );
    // exact constant evaluation beyond the checker's capacity is unknown, not false
    let overflow = KernelProof {
        variables: vec![],
        terms: vec![
            KernelTerm::Nat(u128::MAX),
            KernelTerm::Nat(1),
            KernelTerm::Add(0, 1),
        ],
        steps: vec![KernelRule::Eval(2)],
        goal: (2, 2),
        root: 0,
    };
    assert_eq!(kind(&overflow), KernelErrorKind::UnknownWithinBudget);
}

/// Condition 5: The audit distinguishes kernel-replay from legacy-trusted evidence and
/// keeps the trust dependencies (replay after deleting the source is tested
/// through the CLI in cli_c_min.rs).
#[test]
fn c_min_5_audit_separates_kernel_replay_from_legacy_trust() {
    let source = "module m\ntheory T {\n    fn f(n: Nat) -> Nat = n\n    theorem replayed(a: Nat): nat_eq(mul(a, 1) + 0, a) by normalize\n    theorem legacy(a: Nat): nat_eq(f(a), a) by normalize\n    axiom assumed(a: Nat): nat_eq(f(a), a)\n    theorem uses(a: Nat): nat_eq(f(a) + 0, a) by normalize using assumed\n}\n";
    let report = Checker::new().check_module(&parse_module(source).unwrap());
    assert!(report.ok(), "{:?}", report.diagnostics);
    let text = |name: &str| {
        let c = report
            .certificates
            .iter()
            .find(|c| c.theorem_name == name)
            .unwrap();
        dlm_core::export_certificate_text(c, 1).unwrap()
    };
    assert!(text("replayed").contains("proof_evidence: kernel-replay"));
    assert!(text("legacy").contains("proof_evidence: legacy-trusted"));
    assert!(
        text("uses").contains("proof_evidence: legacy-trusted"),
        "a proof using an axiom is not replayed"
    );
    let audit = dlm_core::audit_check_report(&report, Vec::new());
    assert!(audit
        .components
        .contains(&dlm_core::TrustedComponent::EquationalReplayKernel));
    assert!(
        audit
            .components
            .contains(&dlm_core::TrustedComponent::SymbolicKernel),
        "legacy dependencies are not hidden"
    );
    let uses = audit
        .statements
        .iter()
        .find(|s| s.name == "T.uses")
        .unwrap();
    assert!(
        uses.axioms.iter().any(|a| a.starts_with("assumed")),
        "{:?}",
        uses.axioms
    );
    let replayed = audit
        .statements
        .iter()
        .find(|s| s.name == "T.replayed")
        .unwrap();
    assert!(replayed
        .components
        .contains(&dlm_core::TrustedComponent::EquationalReplayKernel));
}

/// Condition 6: The guarantee boundary is explicit: a replayed Nat equality about an
/// expression does not prove machine definedness, does not remove runtime
/// overflow checks and does not change the compiled program.
#[test]
fn c_min_6_nat_equality_does_not_discharge_machine_obligations() {
    let source = "module m\ntheory T {\n    fn g(n: Nat) -> Nat = mul(n, 1) + n\n    theorem g_body(n: Nat): nat_eq(mul(n, 1) + n, n + n) by normalize\n    fn main() -> Nat = g(170141183460469231731687303715884105728)\n}\n";
    let module = parse_module(source).unwrap();
    let report = Checker::new().check_module(&module);
    assert!(report.ok(), "{:?}", report.diagnostics);
    let c = report
        .certificates
        .iter()
        .find(|c| c.theorem_name == "g_body")
        .unwrap();
    assert!(c.kernel.is_some(), "the theorem is independently replayed");
    let program = Program::single(module.clone());
    let contracts = function_contracts(&program, CheckPolicy::trusted_only())
        .unwrap()
        .render();
    let g = contracts
        .split("fn T.")
        .find(|s| s.starts_with("g "))
        .unwrap();
    assert!(g.contains("machine arithmetic: open"), "{g}");
    assert!(g.contains("arithmetic-overflow"), "{g}");
    // the same program without the theorem (same line layout) compiles to the same C
    let without = parse_module(&source.replace(
        "theorem g_body(n: Nat): nat_eq(mul(n, 1) + n, n + n) by normalize",
        "// no theorem",
    ))
    .unwrap();
    let with_c = emit_c(&lower_program(&program, CheckPolicy::trusted_only()).unwrap());
    let without_c =
        emit_c(&lower_program(&Program::single(without), CheckPolicy::trusted_only()).unwrap());
    assert_eq!(
        with_c, without_c,
        "proofs are erased and grant no optimizer authority"
    );
    // and the native program still fails with the defined overflow error
    let dir = std::env::temp_dir().join(format!("dlm-c-min-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let exe = build_c_executable(
        &with_c,
        &dir.join("main.c"),
        &dir.join(if cfg!(windows) { "main.exe" } else { "main" }),
        &default_c_compiler(),
    )
    .unwrap();
    let output = std::process::Command::new(exe).output().unwrap();
    assert!(!output.status.success());
    assert!(
        String::from_utf8_lossy(&output.stderr).contains("overflow"),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    std::fs::remove_dir_all(dir).unwrap();
    // the checked statement names its model
    let p = produced("n: Nat", "nat_eq(mul(n, 1) + n, n + n)");
    assert!(p
        .proposition(KernelBudget::default())
        .unwrap()
        .contains("n: Nat"));
}
