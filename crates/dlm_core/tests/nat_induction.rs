use dlm_core::{
    induction_base_case, induction_step_case, nat_base_case_proposition, nat_induction_conclusion,
    nat_induction_proof, nat_induction_scheme, nat_step_case_proposition, statement_passport,
    theorem_from_induction_proof, ConstructionMode, CostClass, HistoryChain, LocationContext,
    Passport, Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState, CapabilitySet,
};

fn static_proof(theory: &str, proposition: &str) -> Passport {
    Passport {
        ty: TypeKind::StaticProof(proposition.to_string()),
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::empty(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalDerived,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("test:static_proof:{proposition}")),
        location: LocationContext::local(),
    }
}

fn axiom_static_proof(theory: &str, proposition: &str) -> Passport {
    let mut proof = static_proof(theory, proposition);
    proof.trust = TrustLevel::Axiom;
    proof.validation = ValidationState::Assumed;
    proof.history.push("test:axiom_static_proof");
    proof
}

#[test]
fn nat_induction_scheme_is_not_a_theorem_or_static_proof() {
    let scheme = nat_induction_scheme("Nat", "Even", 1).expect("scheme");

    assert_eq!(
        scheme.ty,
        TypeKind::NatInductionScheme {
            proposition_family: "Even".to_string()
        }
    );
    assert!(!matches!(&scheme.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(&scheme.ty, TypeKind::StaticProof(_)));
    assert!(scheme.history.contains_event("induction:nat:scheme:Even"));
}

#[test]
fn base_and_step_cases_require_exact_static_proofs() {
    let scheme = nat_induction_scheme("Nat", "Even", 1).expect("scheme");
    let base = static_proof("Nat", &nat_base_case_proposition("Even"));
    let step = static_proof("Nat", &nat_step_case_proposition("Even"));

    let base_case = induction_base_case("Nat", &scheme, &base, 2).expect("base case");
    let step_case = induction_step_case("Nat", &scheme, &step, 3).expect("step case");

    assert_eq!(
        base_case.ty,
        TypeKind::InductionBaseCase {
            proposition: "Even(0)".to_string()
        }
    );
    assert_eq!(
        step_case.ty,
        TypeKind::InductionStepCase {
            proposition: "forall n:Nat. Even(n) -> Even(succ(n))".to_string()
        }
    );

    let wrong_base = static_proof("Nat", "Odd(0)");
    assert!(induction_base_case("Nat", &scheme, &wrong_base, 4).is_err());

    let runtime_witness = Passport {
        ty: TypeKind::RuntimeWitness(nat_base_case_proposition("Even")),
        construction: ConstructionMode::External,
        capabilities: CapabilitySet::empty(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Checked,
        provenance: Provenance::RuntimeInput,
        validation: ValidationState::RuntimeChecked,
        theory: TheoryContext::new("Nat"),
        history: HistoryChain::from_event("test:runtime_witness"),
        location: LocationContext::local(),
    };
    assert!(induction_base_case("Nat", &scheme, &runtime_witness, 5).is_err());
}

#[test]
fn nat_induction_proof_requires_matching_scheme_base_and_step() {
    let scheme = nat_induction_scheme("Nat", "Even", 1).expect("scheme");
    let base = induction_base_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_base_case_proposition("Even")),
        2,
    )
    .expect("base");
    let step = induction_step_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_step_case_proposition("Even")),
        3,
    )
    .expect("step");

    let proof = nat_induction_proof("Nat", &scheme, &base, &step, 4).expect("induction proof");
    assert_eq!(
        proof.ty,
        TypeKind::InductionProof {
            proposition: nat_induction_conclusion("Even")
        }
    );
    assert!(proof.history.contains_event("induction:nat:proof:forall n:Nat. Even(n)"));

    let other_scheme = nat_induction_scheme("Nat", "Odd", 5).expect("other scheme");
    assert!(nat_induction_proof("Nat", &other_scheme, &base, &step, 6).is_err());
}

#[test]
fn theorem_from_induction_proof_requires_exact_statement_match() {
    let scheme = nat_induction_scheme("Nat", "P", 1).expect("scheme");
    let base = induction_base_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_base_case_proposition("P")),
        2,
    )
    .expect("base");
    let step = induction_step_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_step_case_proposition("P")),
        3,
    )
    .expect("step");
    let proof = nat_induction_proof("Nat", &scheme, &base, &step, 4).expect("proof");
    let statement = statement_passport("Nat", nat_induction_conclusion("P"));

    let theorem = theorem_from_induction_proof("Nat", "P_all", &statement, &proof, 5)
        .expect("theorem");
    assert_eq!(
        theorem.ty,
        TypeKind::Theorem {
            name: "P_all".to_string(),
            proposition: "forall n:Nat. P(n)".to_string()
        }
    );
    assert!(theorem.history.contains_event("theorem:induction:P_all:forall n:Nat. P(n)"));

    let wrong_statement = statement_passport("Nat", nat_induction_conclusion("Q"));
    assert!(theorem_from_induction_proof("Nat", "bad", &wrong_statement, &proof, 6).is_err());
}

#[test]
fn induction_preserves_axiom_taint_from_cases() {
    let scheme = nat_induction_scheme("Nat", "Trusted", 1).expect("scheme");
    let base = induction_base_case(
        "Nat",
        &scheme,
        &axiom_static_proof("Nat", &nat_base_case_proposition("Trusted")),
        2,
    )
    .expect("base");
    let step = induction_step_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_step_case_proposition("Trusted")),
        3,
    )
    .expect("step");

    let proof = nat_induction_proof("Nat", &scheme, &base, &step, 4).expect("proof");
    assert_eq!(base.trust, TrustLevel::Axiom);
    assert_eq!(proof.trust, TrustLevel::Axiom);
    assert_eq!(proof.validation, ValidationState::Assumed);
    assert!(proof.history.contains_event("test:axiom_static_proof"));
}

#[test]
fn induction_history_preserves_scheme_base_step_order() {
    let scheme = nat_induction_scheme("Nat", "R", 1).expect("scheme");
    let base = induction_base_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_base_case_proposition("R")),
        2,
    )
    .expect("base");
    let step = induction_step_case(
        "Nat",
        &scheme,
        &static_proof("Nat", &nat_step_case_proposition("R")),
        3,
    )
    .expect("step");
    let proof = nat_induction_proof("Nat", &scheme, &base, &step, 4).expect("proof");

    let events = proof.history.events().join("|");
    let scheme_pos = events.find("induction:nat:scheme:R").expect("scheme event");
    let base_pos = events.find("induction:nat:base:R(0)").expect("base event");
    let step_pos = events
        .find("induction:nat:step:forall n:Nat. R(n) -> R(succ(n))")
        .expect("step event");
    let proof_pos = events
        .find("induction:nat:proof:forall n:Nat. R(n)")
        .expect("proof event");

    assert!(scheme_pos < base_pos);
    assert!(base_pos < step_pos);
    assert!(step_pos < proof_pos);
}
