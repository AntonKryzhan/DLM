use dlm_core::*;

fn prop(name: &str) -> Passport {
    Passport::proposition("Logic", name, None, format!("test:prop:{name}"))
}

#[test]
fn logical_connectives_are_formula_objects_not_theorems_or_proofs() {
    let p = prop("P");
    let q = prop("Q");
    let formula = logical_formula(
        LogicalConnective::And,
        vec![formula_from_passport(&p, 1).unwrap(), formula_from_passport(&q, 1).unwrap()],
        &[&p, &q],
        1,
    )
    .unwrap();
    assert_eq!(formula.proposition, "and(P, Q)");

    let passport = logical_formula_passport("Logic", &formula, &[&p, &q]);
    assert!(matches!(passport.ty, TypeKind::LogicalFormula { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(passport.ty, TypeKind::StaticProof(_)));
    assert_eq!(require_logical_formula(&passport, 1).unwrap(), "and(P, Q)");
}

#[test]
fn connective_arity_is_checked_explicitly() {
    let err = logical_formula(LogicalConnective::Implies, vec!["P".to_string()], &[], 7).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::LogicFormulaError);

    let err = logical_formula(
        LogicalConnective::Not,
        vec!["P".to_string(), "Q".to_string()],
        &[],
        8,
    )
    .unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::LogicFormulaError);
}

#[test]
fn proof_truth_runtime_and_theorem_objects_are_not_formula_operands() {
    let proof_term = Passport::proof_term("Logic", "p_intro", None);
    let static_proof = Passport::static_proof("Logic", "P", &proof_term);
    let theorem = theorem_from_static_proof("Logic", "T", &statement_passport("Logic", "P"), &static_proof, 1).unwrap();
    let witness = Passport::runtime_witness("Logic", "P", &Passport::runtime_nat_from_input("Logic"));
    let provable = Passport::provable_claim("Logic", "Logic", "P", &static_proof);
    let truth = Passport::axiom_truth_from_provable("Logic", "P", &provable);

    for bad in [&proof_term, &static_proof, &theorem, &witness, &provable, &truth] {
        let err = formula_from_passport(bad, 9).unwrap_err();
        assert_eq!(err.kind, DiagnosticKind::LogicFormulaError);
    }
}

#[test]
fn quantifier_objects_validate_binders_without_becoming_proofs() {
    let var = bound_variable("n", "Nat", 1).unwrap();
    let quantified = quantified_formula(QuantifierKind::Forall, var, "P(n)", &[], 1).unwrap();
    assert_eq!(quantified.proposition, "forall n:Nat. P(n)");

    let passport = quantified_formula_passport("Logic", &quantified, &[]);
    assert!(matches!(passport.ty, TypeKind::QuantifiedFormula { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(passport.ty, TypeKind::StaticProof(_)));
    assert_eq!(require_quantified_formula(&passport, 1).unwrap(), "forall n:Nat. P(n)");
}

#[test]
fn invalid_bound_variables_and_empty_bodies_are_rejected() {
    let err = bound_variable("1bad", "Nat", 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::LogicFormulaError);

    let var = bound_variable("x", "Nat", 3).unwrap();
    let err = quantified_formula(QuantifierKind::Exists, var, "", &[], 3).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::LogicFormulaError);
}

#[test]
fn formulas_preserve_axiom_oracle_and_unsafe_taint() {
    let axiom = Passport::axiom_bool("Logic");
    let unsafe_nat = Passport::unsafe_nat("Logic");
    let formula = logical_formula(
        LogicalConnective::Or,
        vec!["AxiomSeen".to_string(), "UnsafeSeen".to_string()],
        &[&axiom, &unsafe_nat],
        4,
    )
    .unwrap();
    assert_eq!(formula.max_trust, TrustLevel::Unsafe);
    assert!(formula.has_axiom_taint);
    assert!(formula.has_oracle_taint);
    assert!(formula.has_unsafe_taint);

    let passport = logical_formula_passport("Logic", &formula, &[&axiom, &unsafe_nat]);
    assert_eq!(passport.trust, TrustLevel::Unsafe);
    assert!(passport.history.contains_event("logic:formula:or"));
}

#[test]
fn formula_exports_are_stable_and_order_sensitive() {
    let pq = logical_formula(
        LogicalConnective::Implies,
        vec!["P".to_string(), "Q".to_string()],
        &[],
        1,
    )
    .unwrap();
    let qp = logical_formula(
        LogicalConnective::Implies,
        vec!["Q".to_string(), "P".to_string()],
        &[],
        1,
    )
    .unwrap();
    assert_ne!(pq.fingerprint, qp.fingerprint);

    let exported = export_logical_formula(&pq);
    assert!(exported.contains("logical_formula_report: v1"));
    assert!(exported.contains("connective: implies"));
    assert!(exported.contains("proposition: implies(P, Q)"));
    assert!(exported.contains(&pq.fingerprint));

    let var = bound_variable("x", "Nat", 1).unwrap();
    let q = quantified_formula(QuantifierKind::Exists, var, "P(x)", &[], 1).unwrap();
    let exported = export_quantified_formula(&q);
    assert!(exported.contains("quantified_formula_report: v1"));
    assert!(exported.contains("quantifier: exists"));
    assert!(exported.contains("body: P(x)"));
}
