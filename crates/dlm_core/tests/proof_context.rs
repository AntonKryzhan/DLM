use dlm_core::{
    assume_hypothesis, close_proof_by_axiom, close_proof_with_static_proof, open_proof_context,
    proof_obligation_for_goal, goal_passport, statement_passport, DiagnosticKind, Passport,
    ProofClosureStatus, TacticStep, TrustLevel, TypeKind,
};

#[test]
fn proof_context_opens_only_from_goal_passports() {
    let goal = goal_passport("Meta", "kernel_checked:true_intro");
    let context = open_proof_context("Meta", goal, 1).unwrap();

    assert_eq!(context.goal_proposition(), Some("kernel_checked:true_intro"));
    assert_eq!(context.hypotheses.len(), 0);
    assert!(matches!(
        &context.steps[0],
        TacticStep::OpenGoal { proposition } if proposition == "kernel_checked:true_intro"
    ));

    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let err = open_proof_context("Meta", statement, 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofObligationError);
    assert!(err.message.contains("Goal passport"));
}

#[test]
fn hypotheses_are_ordered_and_do_not_close_the_goal_implicitly() {
    let goal = goal_passport("Meta", "P_implies_P");
    let mut context = open_proof_context("Meta", goal.clone(), 1).unwrap();

    let h0 = assume_hypothesis(&mut context, "P", &goal, 3).unwrap();
    let h1 = assume_hypothesis(&mut context, "P", &goal, 4).unwrap();

    assert_ne!(h0, h1);
    assert_eq!(context.hypotheses.len(), 2);
    assert!(context.has_hypothesis("P"));
    assert!(matches!(&context.hypotheses.get(h0).unwrap().passport.ty, TypeKind::Hypothesis { .. }));
    assert!(!matches!(&context.goal.ty, TypeKind::Theorem { .. }));

    assert!(matches!(&context.steps[1], TacticStep::Assume { hypothesis, proposition } if *hypothesis == h0 && proposition == "P"));
    assert!(matches!(&context.steps[2], TacticStep::Assume { hypothesis, proposition } if *hypothesis == h1 && proposition == "P"));
}

#[test]
fn close_proof_requires_goal_statement_and_static_proof_to_match_exactly() {
    let goal = goal_passport("Meta", "kernel_checked:true_intro");
    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let term = Passport::proof_term("Meta", "true_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "kernel_checked:true_intro", &term);

    let closure = close_proof_with_static_proof(
        open_proof_context("Meta", goal, 1).unwrap(),
        "TrueIntro",
        &statement,
        &proof,
        6,
    )
    .unwrap();

    assert_eq!(closure.status, ProofClosureStatus::ClosedByStaticProof);
    assert!(closure.obligations.is_empty());
    assert!(matches!(
        &closure.theorem.ty,
        TypeKind::Theorem { name, proposition }
            if name == "TrueIntro" && proposition == "kernel_checked:true_intro"
    ));
    assert!(closure.steps.iter().any(|step| matches!(step, TacticStep::ExactStaticProof { proposition } if proposition == "kernel_checked:true_intro")));
}

#[test]
fn close_proof_rejects_statement_or_proof_mismatches() {
    let goal = goal_passport("Meta", "P");
    let wrong_statement = statement_passport("Meta", "Q");
    let term = Passport::proof_term("Meta", "p_intro", None);
    let proof_of_p = Passport::kernel_checked_proof("Meta", "P", &term);

    let statement_err = close_proof_with_static_proof(
        open_proof_context("Meta", goal.clone(), 1).unwrap(),
        "BadStatement",
        &wrong_statement,
        &proof_of_p,
        8,
    )
    .unwrap_err();
    assert_eq!(statement_err.kind, DiagnosticKind::ProofObligationError);
    assert!(statement_err.message.contains("does not match open goal"));

    let statement_p = statement_passport("Meta", "P");
    let proof_of_q = Passport::kernel_checked_proof("Meta", "Q", &term);
    let proof_err = close_proof_with_static_proof(
        open_proof_context("Meta", goal, 1).unwrap(),
        "BadProof",
        &statement_p,
        &proof_of_q,
        9,
    )
    .unwrap_err();
    assert_eq!(proof_err.kind, DiagnosticKind::ProofObligationError);
    assert!(proof_err.message.contains("but open goal requires"));
}

#[test]
fn proof_obligation_reports_open_goal_and_axiom_close_is_tainted() {
    let goal = goal_passport("Meta", "reflection_boundary");
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let obligation = proof_obligation_for_goal(&context);

    assert_eq!(obligation.proposition, "reflection_boundary");
    assert!(obligation.reason.contains("StaticProof"));

    let statement = statement_passport("Meta", "reflection_boundary");
    let closure = close_proof_by_axiom(
        context,
        "ReflectionBoundary",
        &statement,
        "temporary metatheory axiom",
        12,
    )
    .unwrap();

    assert_eq!(closure.status, ProofClosureStatus::AdmittedByAxiom);
    assert_eq!(closure.theorem.trust, TrustLevel::Axiom);
    assert!(closure.steps.iter().any(|step| matches!(step, TacticStep::AdmitAxiom { reason } if reason == "temporary metatheory axiom")));
}
