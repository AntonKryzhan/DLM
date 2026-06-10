use dlm_core::{
    axiom_theorem, goal_passport, hypothesis_passport, proposition_of, require_static_proof,
    statement_passport, theorem_from_static_proof, Capability, DiagnosticKind, Passport, TrustLevel,
    TypeKind,
};

#[test]
fn statement_is_a_proposition_carrier_not_a_theorem_or_proof() {
    let statement = statement_passport("Meta", "true");

    assert_eq!(proposition_of(&statement), Some("true"));
    assert!(matches!(&statement.ty, TypeKind::Statement { proposition } if proposition == "true"));
    assert!(!matches!(&statement.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
    assert!(statement.capabilities.contains(Capability::CanPropositionReason));
    assert!(statement.history.contains_event("statement:declare:true"));
}

#[test]
fn theorem_requires_static_proof_not_runtime_witness_or_raw_proof_term() {
    let statement = statement_passport("Meta", "true");
    let term = Passport::proof_term("Meta", "true_intro", None);

    let term_err = theorem_from_static_proof("Meta", "TrueIntro", &statement, &term, 4)
        .unwrap_err();
    assert_eq!(term_err.kind, DiagnosticKind::StatementTheoremError);
    assert!(term_err.message.contains("ProofTerm must be kernel-checked"));

    let source = Passport::literal_nat("Meta");
    let witness = Passport::runtime_witness("Meta", "x_gt_0", &source);
    let witness_err = theorem_from_static_proof("Meta", "RuntimeClaim", &statement, &witness, 5)
        .unwrap_err();
    assert_eq!(witness_err.kind, DiagnosticKind::StatementTheoremError);
    assert!(witness_err.message.contains("RuntimeWitness is not a static proof"));

    assert_eq!(require_static_proof(&witness, 5).unwrap_err().kind, DiagnosticKind::StatementTheoremError);
}

#[test]
fn theorem_from_static_proof_is_checked_and_keeps_the_proof_history() {
    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let term = Passport::proof_term("Meta", "true_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "kernel_checked:true_intro", &term);

    let theorem = theorem_from_static_proof("Meta", "TrueIntro", &statement, &proof, 9)
        .unwrap();

    assert!(matches!(
        &theorem.ty,
        TypeKind::Theorem { name, proposition }
            if name == "TrueIntro" && proposition == "kernel_checked:true_intro"
    ));
    assert_eq!(theorem.trust, TrustLevel::Builtin);
    assert!(theorem.capabilities.contains(Capability::CanProofKernelCheck));
    assert!(theorem.history.contains_event("proof_kernel:check"));
    assert!(theorem.history.contains_event("theorem:proved:TrueIntro:kernel_checked:true_intro"));
}

#[test]
fn theorem_from_axiom_is_visible_as_axiom_tainted() {
    let statement = statement_passport("Meta", "reflection_safe_boundary");
    let theorem = axiom_theorem("Meta", "ReflectionBoundary", &statement, 12).unwrap();

    assert!(matches!(
        &theorem.ty,
        TypeKind::Theorem { name, proposition }
            if name == "ReflectionBoundary" && proposition == "reflection_safe_boundary"
    ));
    assert_eq!(theorem.trust, TrustLevel::Axiom);
    assert!(theorem.history.contains_event("theorem:axiom:ReflectionBoundary:reflection_safe_boundary"));
}

#[test]
fn goal_and_hypothesis_do_not_become_theorems_implicitly() {
    let goal = goal_passport("Meta", "P_implies_P");
    assert!(matches!(&goal.ty, TypeKind::Goal { proposition } if proposition == "P_implies_P"));
    assert!(!matches!(&goal.ty, TypeKind::Theorem { .. }));

    let hypothesis = hypothesis_passport("Meta", "P", &goal);
    assert!(matches!(&hypothesis.ty, TypeKind::Hypothesis { proposition } if proposition == "P"));
    assert_eq!(hypothesis.trust, TrustLevel::Axiom);
    assert!(!matches!(&hypothesis.ty, TypeKind::StaticProof(_) | TypeKind::Theorem { .. }));

    let proof = Passport::kernel_checked_proof(
        "Meta",
        "kernel_checked:true_intro",
        &Passport::proof_term("Meta", "true_intro", None),
    );
    let err = theorem_from_static_proof("Meta", "Bad", &goal, &proof, 15).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::StatementTheoremError);
}
