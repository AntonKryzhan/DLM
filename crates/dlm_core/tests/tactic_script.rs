use dlm_core::{
    execute_tactic_script, goal_passport, open_proof_context, statement_passport, DiagnosticKind,
    Passport, TacticCommand, TacticScript, TacticScriptStatus, TacticStep, TrustLevel, TypeKind,
};

#[test]
fn empty_tactic_script_keeps_goal_open_with_obligation() {
    let goal = goal_passport("Meta", "P");
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let report = execute_tactic_script(context, &TacticScript::new(), 3).unwrap();

    assert_eq!(report.status, TacticScriptStatus::Open);
    assert!(!report.is_closed());
    assert_eq!(report.executed_steps, 0);
    assert_eq!(report.obligations.len(), 1);
    assert_eq!(report.obligations[0].proposition, "P");
    assert!(report.final_context.unwrap().goal_proposition() == Some("P"));
}

#[test]
fn assume_tactics_preserve_order_and_do_not_close_the_goal() {
    let goal = goal_passport("Meta", "P_implies_P");
    let context = open_proof_context("Meta", goal.clone(), 1).unwrap();
    let script = TacticScript::new()
        .assume("P", goal.clone())
        .assume("P", goal);

    let report = execute_tactic_script(context, &script, 4).unwrap();

    assert_eq!(report.status, TacticScriptStatus::Open);
    assert_eq!(report.executed_steps, 2);
    assert_eq!(report.obligations[0].proposition, "P_implies_P");
    let final_context = report.final_context.unwrap();
    assert_eq!(final_context.hypotheses.len(), 2);
    assert!(matches!(&report.trace[1], TacticStep::Assume { hypothesis, proposition } if hypothesis.0 == 0 && proposition == "P"));
    assert!(matches!(&report.trace[2], TacticStep::Assume { hypothesis, proposition } if hypothesis.0 == 1 && proposition == "P"));
}

#[test]
fn exact_static_proof_closes_goal_when_statement_and_proof_match() {
    let goal = goal_passport("Meta", "kernel_checked:true_intro");
    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let term = Passport::proof_term("Meta", "true_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "kernel_checked:true_intro", &term);
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().exact_static_proof("TrueIntro", statement, proof);

    let report = execute_tactic_script(context, &script, 5).unwrap();

    assert_eq!(report.status, TacticScriptStatus::ClosedByStaticProof);
    assert!(report.is_closed());
    assert_eq!(report.executed_steps, 1);
    assert!(report.obligations.is_empty());
    let closure = report.closure.unwrap();
    assert!(matches!(
        &closure.theorem.ty,
        TypeKind::Theorem { name, proposition }
            if name == "TrueIntro" && proposition == "kernel_checked:true_intro"
    ));
}

#[test]
fn admit_axiom_closes_goal_but_keeps_axiom_taint_visible() {
    let goal = goal_passport("Meta", "reflection_boundary");
    let statement = statement_passport("Meta", "reflection_boundary");
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().admit_axiom(
        "ReflectionBoundary",
        statement,
        "temporary metatheory axiom",
    );

    let report = execute_tactic_script(context, &script, 8).unwrap();

    assert_eq!(report.status, TacticScriptStatus::AdmittedByAxiom);
    let closure = report.closure.unwrap();
    assert_eq!(closure.theorem.trust, TrustLevel::Axiom);
    assert!(closure.theorem.history.contains_event("theorem:axiom:ReflectionBoundary:reflection_boundary"));
}

#[test]
fn closing_tactic_must_be_final() {
    let goal = goal_passport("Meta", "P");
    let statement = statement_passport("Meta", "P");
    let term = Passport::proof_term("Meta", "p_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "P", &term);
    let context = open_proof_context("Meta", goal.clone(), 1).unwrap();
    let script = TacticScript::new()
        .with(TacticCommand::ExactStaticProof {
            theorem_name: "PIntro".to_string(),
            statement,
            proof,
        })
        .assume("Q", goal);

    let err = execute_tactic_script(context, &script, 11).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TacticScriptError);
    assert!(err.message.contains("final tactic"));
}

#[test]
fn exact_tactic_preserves_proof_obligation_errors() {
    let goal = goal_passport("Meta", "P");
    let statement = statement_passport("Meta", "P");
    let term = Passport::proof_term("Meta", "q_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "Q", &term);
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().exact_static_proof("Bad", statement, proof);

    let err = execute_tactic_script(context, &script, 14).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofObligationError);
    assert!(err.message.contains("open goal requires"));
}
