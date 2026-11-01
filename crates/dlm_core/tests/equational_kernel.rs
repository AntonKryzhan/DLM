use dlm_core::proof_kernel::{
    builder::build_equational_proof, check_kernel_proof, AlgebraLaw, KernelAxiom, KernelBudget,
    KernelContext, KernelErrorKind, KernelProof, KernelRule, KernelTerm,
};
use dlm_core::{parse_module, Checker, ModuleItem, StatementItem, TheoryItem};

fn item(lhs: &str, rhs: &str) -> StatementItem {
    let source = format!("module proofs\ntheory T {{\n    theorem t(a: Nat, b: Nat, c: Nat): nat_eq({lhs}, {rhs}) by normalize\n}}\n");
    let module = parse_module(&source).unwrap();
    let ModuleItem::Theory(t) = &module.items[0] else {
        panic!()
    };
    let TheoryItem::Statement(s) = &t.items[0] else {
        panic!()
    };
    s.clone()
}

fn proof(lhs: &str, rhs: &str) -> KernelProof {
    build_equational_proof(&item(lhs, rhs))
        .unwrap_or_else(|| panic!("no evidence for {lhs} = {rhs}"))
}

fn check(p: &KernelProof) -> dlm_core::proof_kernel::KernelCheck {
    check_kernel_proof(p, &KernelContext::default(), KernelBudget::default()).unwrap()
}

fn invalid(p: &KernelProof) {
    let error =
        check_kernel_proof(p, &KernelContext::default(), KernelBudget::default()).unwrap_err();
    assert_eq!(error.kind, KernelErrorKind::Invalid, "{error}");
}

fn eval(p: &KernelProof, vars: &[u128]) -> (u128, u128) {
    let mut values = Vec::new();
    for term in &p.terms {
        values.push(match *term {
            KernelTerm::Var(id) => vars[id],
            KernelTerm::Nat(n) => n,
            KernelTerm::Add(a, b) => values[a] + values[b],
            KernelTerm::Mul(a, b) => values[a] * values[b],
        });
    }
    (values[p.goal.0], values[p.goal.1])
}

#[test]
fn algebra_rules_replay_without_search_and_agree_with_independent_evaluation() {
    let cases = [
        ("a", "a"),
        ("a + 0", "a"),
        ("0 + a", "a"),
        ("mul(a, 1)", "a"),
        ("mul(1, a)", "a"),
        ("mul(a, 0)", "0"),
        ("mul(0, a)", "0"),
        ("a + b", "b + a"),
        ("mul(a, b)", "mul(b, a)"),
        ("(a + b) + c", "a + (b + c)"),
        ("mul(mul(a, b), c)", "mul(a, mul(b, c))"),
        ("mul(a, b + c)", "mul(a, b) + mul(a, c)"),
        ("mul(a + b, c)", "mul(a, c) + mul(b, c)"),
        ("mul(1, a + 0)", "a"),
        ("(a + 0) + b", "b + a"),
        ("2 + mul(3, 4)", "14"),
    ];
    for (lhs, rhs) in cases {
        let p = proof(lhs, rhs);
        let report = check(&p);
        assert!(report.used_axioms.is_empty());
        let text = p.to_text();
        assert_eq!(
            KernelProof::from_text(&text, KernelBudget::default()).unwrap(),
            p
        );
        assert_eq!(
            p.proposition(KernelBudget::default()).unwrap(),
            dlm_core::ordinary::statement_proposition(&item(lhs, rhs))
        );
        for a in 0..6 {
            for b in 0..6 {
                for c in 0..6 {
                    let (x, y) = eval(&p, &[a, b, c]);
                    assert_eq!(x, y, "{lhs} = {rhs}");
                }
            }
        }
    }
    assert!(build_equational_proof(&item("a", "a + 1")).is_none());
}

#[test]
fn damaged_rules_premises_goals_and_variable_scopes_are_rejected() {
    let p = proof("a + 0", "a");
    let mut bad = p.clone();
    bad.steps[bad.root] = KernelRule::Law(AlgebraLaw::MulOne, vec![0]);
    invalid(&bad);
    let mut bad = p.clone();
    bad.steps[bad.root] = KernelRule::Refl(p.goal.0);
    invalid(&bad);
    let mut bad = p.clone();
    bad.steps[bad.root] = KernelRule::Sym(bad.root);
    invalid(&bad);
    let mut bad = p.clone();
    bad.steps[bad.root] = KernelRule::Trans(usize::MAX, 0);
    invalid(&bad);
    let mut bad = p.clone();
    bad.goal = (p.goal.1, p.goal.0);
    invalid(&bad);
    let mut bad = p.clone();
    bad.terms[0] = KernelTerm::Var(100);
    invalid(&bad);
    let mut bad = p.clone();
    bad.terms[0] = KernelTerm::Add(0, 0);
    invalid(&bad);
    let mut bad = p.clone();
    bad.variables[1] = bad.variables[0].clone();
    invalid(&bad);
    let mut bad = p.clone();
    bad.steps.push(KernelRule::Axiom(0));
    invalid(&bad); // unused nodes are checked too
    let mut bad = p.clone();
    bad.steps[bad.root] = KernelRule::Law(AlgebraLaw::AddZero, vec![]);
    invalid(&bad);
    let mut bad = p;
    bad.steps[bad.root] = KernelRule::Eval(bad.goal.1);
    invalid(&bad);
}

#[test]
fn axioms_require_an_external_context_and_remain_visible() {
    let p = KernelProof {
        variables: vec!["x".into()],
        terms: vec![KernelTerm::Var(0), KernelTerm::Nat(0)],
        steps: vec![KernelRule::Axiom(0)],
        goal: (0, 1),
        root: 0,
    };
    invalid(&p);
    let context = KernelContext {
        axioms: vec![KernelAxiom {
            name: "declared-assumption".into(),
            variables: p.variables.clone(),
            terms: p.terms.clone(),
            equation: (0, 1),
        }],
    };
    let report = check_kernel_proof(&p, &context, KernelBudget::default()).unwrap();
    assert_eq!(report.used_axioms, ["declared-assumption"]);
    let mut duplicate_context = context.clone();
    duplicate_context.axioms.push(context.axioms[0].clone());
    assert_eq!(
        check_kernel_proof(&p, &duplicate_context, KernelBudget::default())
            .unwrap_err()
            .kind,
        KernelErrorKind::Invalid
    );
    let mut swapped = p.clone();
    swapped.terms.swap(0, 1);
    assert!(check_kernel_proof(&swapped, &context, KernelBudget::default()).is_err());
    let mut wrong_context = context;
    wrong_context.axioms[0].equation = (1, 0);
    assert_eq!(
        check_kernel_proof(&p, &wrong_context, KernelBudget::default())
            .unwrap_err()
            .kind,
        KernelErrorKind::Invalid
    );
}

#[test]
fn budget_exhaustion_is_distinct_from_an_invalid_proof() {
    let p = proof("mul(a + 0, 1)", "a");
    for budget in [
        KernelBudget {
            max_nodes: 0,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_steps: 0,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_work: 0,
            ..KernelBudget::default()
        },
        KernelBudget {
            max_depth: 1,
            ..KernelBudget::default()
        },
    ] {
        assert_eq!(
            check_kernel_proof(&p, &KernelContext::default(), budget)
                .unwrap_err()
                .kind,
            KernelErrorKind::UnknownWithinBudget
        );
    }
    let budget = KernelBudget {
        max_text_bytes: 8,
        ..KernelBudget::default()
    };
    assert_eq!(
        KernelProof::from_text(&p.to_text(), budget)
            .unwrap_err()
            .kind,
        KernelErrorKind::UnknownWithinBudget
    );
    let p = KernelProof {
        variables: vec![],
        terms: vec![
            KernelTerm::Nat(u128::MAX),
            KernelTerm::Nat(1),
            KernelTerm::Add(0, 1),
        ],
        steps: vec![KernelRule::Eval(2)],
        goal: (2, 1),
        root: 0,
    };
    assert_eq!(
        check_kernel_proof(&p, &KernelContext::default(), KernelBudget::default())
            .unwrap_err()
            .kind,
        KernelErrorKind::UnknownWithinBudget
    );
    // Mathematical equality can still be proved without evaluating the overflowing expression.
    let mut reflexive = p;
    reflexive.steps[0] = KernelRule::Refl(2);
    reflexive.goal = (2, 2);
    check(&reflexive);
    let mut shared = KernelProof {
        variables: vec!["x".into()],
        terms: vec![KernelTerm::Var(0)],
        steps: vec![],
        goal: (0, 0),
        root: 0,
    };
    for _ in 0..24 {
        let id = shared.terms.len() - 1;
        shared.terms.push(KernelTerm::Add(id, id));
    }
    let last = shared.terms.len() - 1;
    shared.goal = (last, last);
    shared.steps.push(KernelRule::Refl(last));
    check(&shared);
    assert_eq!(
        shared
            .proposition(KernelBudget::default())
            .unwrap_err()
            .kind,
        KernelErrorKind::UnknownWithinBudget
    );
}

#[test]
fn certificates_bind_the_semantic_proof_even_after_rehashing() {
    let source = "module m\ntheory T {\n    theorem zero(n: Nat): nat_eq(n + 0, n) by normalize\n    fn identity(n: Nat) -> Nat = n\n    theorem legacy(n: Nat): nat_eq(identity(n), n) by normalize\n}\n";
    let report = Checker::new().check_module(&parse_module(source).unwrap());
    assert!(report.ok(), "{:?}", report.diagnostics);
    let certificate = &report.certificates[0];
    assert!(certificate.kernel.is_some());
    assert!(certificate.fingerprint.starts_with("dlm-cert-v2-"));
    assert!(report.certificates[1].kernel.is_none());
    assert!(
        dlm_core::export_certificate_text(&report.certificates[1], 1)
            .unwrap()
            .contains("proof_evidence: legacy-trusted")
    );
    let theorem = &report
        .inferred
        .iter()
        .find(|(name, _)| name == "T.zero")
        .unwrap()
        .1;
    let audit = dlm_core::audit_certificate_against_theorem(certificate, theorem, 1);
    assert!(audit.verified() && audit.independently_checked);
    let program_audit = dlm_core::audit_check_report(&report, Vec::new());
    assert!(program_audit
        .components
        .contains(&dlm_core::TrustedComponent::EquationalReplayKernel));
    let mut bad = certificate.clone();
    let dag = bad.kernel.as_mut().unwrap();
    dag.steps[dag.root] = KernelRule::Refl(dag.goal.0);
    bad.fingerprint = bad.recomputed_fingerprint();
    assert!(bad.fingerprint_is_stable());
    assert!(dlm_core::verify_certificate_against_theorem(&bad, theorem, 1).is_err());
    assert!(dlm_core::export_certificate_text(&bad, 1).is_err());
    assert!(!dlm_core::audit_certificate_against_theorem(&bad, theorem, 1).verified());
    let mut bad = certificate.clone();
    // Build a valid but different claim; its own proof succeeds, binding must fail.
    bad.kernel = Some(proof("a", "a"));
    bad.fingerprint = bad.recomputed_fingerprint();
    assert!(dlm_core::verify_certificate_against_theorem(&bad, theorem, 1).is_err());
    let mut oversized = certificate.clone();
    oversized
        .kernel
        .as_mut()
        .unwrap()
        .terms
        .resize(KernelBudget::default().max_nodes + 1, KernelTerm::Nat(0));
    oversized.fingerprint = "untrusted".into();
    let error = dlm_core::verify_certificate_against_theorem(&oversized, theorem, 1).unwrap_err();
    assert!(error.message.contains("UnknownWithinBudget"));
}

#[test]
fn parser_rejects_unknown_versions_trailing_data_and_malformed_input_without_panics() {
    let p = proof("a", "a");
    let text = p.to_text();
    for bad in [
        text.replace("v1", "v99"),
        text.replace("rules nat-semiring-v1", "rules unknown-v1"),
        text.clone() + "extra\n",
        text.replace("root 0", "root -1"),
        text.replace("term var 0", "term unknown 0"),
        text.replace("END-DLM-KERNEL-PROOF", ""),
    ] {
        assert!(
            KernelProof::from_text(&bad, KernelBudget::default()).is_err(),
            "{bad}"
        );
    }
    let mut seed = 0x12345678u64;
    for n in 0..400 {
        let mut text = String::from("DLM-KERNEL-PROOF v1\n");
        for _ in 0..n % 100 {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            text.push(char::from((seed % 128) as u8));
        }
        assert!(KernelProof::from_text(&text, KernelBudget::default()).is_err());
    }
}
