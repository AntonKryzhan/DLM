use dlm_core::*;

fn prop(name: &str) -> Passport {
    Passport::proposition("Logic", name, None, format!("test:prop:{name}"))
}

#[test]
fn variable_scope_distinguishes_free_and_bound_identifiers() {
    let report = variable_scope_report("forall x:Nat. implies(P(x), Q(y))", 1).unwrap();
    assert!(report.free_variables.contains(&"P".to_string()));
    assert!(report.free_variables.contains(&"Q".to_string()));
    assert!(report.free_variables.contains(&"y".to_string()));
    assert!(!report.free_variables.contains(&"x".to_string()));
    assert_eq!(report.bound_variables.len(), 1);
    assert_eq!(report.bound_variables[0].name, "x");
    assert_eq!(report.bound_variables[0].domain.as_deref(), Some("Nat"));

    let passport = variable_scope_passport("Logic", &report, &[]);
    assert!(matches!(passport.ty, TypeKind::VariableScopeReport { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(passport.ty, TypeKind::StaticProof(_)));
}

#[test]
fn alpha_equivalence_allows_only_binder_renaming() {
    let equivalent = alpha_equivalence_report(
        "forall x:Nat. P(x)",
        "forall y:Nat. P(y)",
        &[],
        1,
    )
    .unwrap();
    assert_eq!(equivalent.status, AlphaEquivalenceStatus::Equivalent);
    require_alpha_equivalent(&equivalent, 1).unwrap();
    assert_eq!(equivalent.canonical_lhs, "forall $0:Nat. P($0)");
    assert_eq!(equivalent.canonical_rhs, "forall $0:Nat. P($0)");

    let not_equivalent = alpha_equivalence_report(
        "forall x:Nat. P(x)",
        "exists y:Nat. P(y)",
        &[],
        1,
    )
    .unwrap();
    assert_eq!(not_equivalent.status, AlphaEquivalenceStatus::NotEquivalent);
    assert_eq!(require_alpha_equivalent(&not_equivalent, 1).unwrap_err().kind, DiagnosticKind::SubstitutionError);
}

#[test]
fn substitution_applies_to_free_identifiers_only_and_blocks_shadowed_binders() {
    let applied = substitution_report("implies(P(x), Q(x))", "x", "succ(x0)", &[], 1).unwrap();
    assert_eq!(applied.status, SubstitutionStatus::Applied);
    assert_eq!(applied.result, "implies(P(succ(x0)), Q(succ(x0)))");
    assert!(applied.free_variables_after.contains(&"x0".to_string()));

    let blocked = substitution_report("forall x:Nat. P(x)", "x", "zero", &[], 1).unwrap();
    assert_eq!(blocked.status, SubstitutionStatus::BlockedByBinder);
    assert_eq!(blocked.result, "forall x:Nat. P(x)");
}

#[test]
fn substitution_rejects_capture_risk_in_quantifier_scope() {
    let report = substitution_report("forall x:Nat. P(y)", "y", "f(x)", &[], 2).unwrap();
    assert_eq!(report.status, SubstitutionStatus::RejectedCaptureRisk);
    assert_eq!(report.result, "forall x:Nat. P(y)");
    assert_eq!(report.capture_risk_variables, vec!["x".to_string()]);

    let passport = substitution_report_passport("Logic", &report, &[]);
    assert!(matches!(passport.ty, TypeKind::SubstitutionReport { .. }));
    assert!(passport.history.contains_event("substitution:y:rejected_capture_risk"));
}

#[test]
fn alpha_rename_quantified_formula_produces_fresh_equivalent_formula() {
    let var = bound_variable("x", "Nat", 1).unwrap();
    let quantified = quantified_formula(QuantifierKind::Forall, var, "P(x)", &[], 1).unwrap();
    let renamed = alpha_rename_quantified_formula(&quantified, "z", 1).unwrap();
    assert_eq!(renamed.proposition, "forall z:Nat. P(z)");

    let report = alpha_equivalence_report(quantified.proposition, renamed.proposition, &[], 1).unwrap();
    assert_eq!(report.status, AlphaEquivalenceStatus::Equivalent);
}

#[test]
fn theorem_proof_truth_and_runtime_evidence_are_not_substitution_sources() {
    let proof_term = Passport::proof_term("Logic", "p_intro", None);
    let static_proof = Passport::static_proof("Logic", "P", &proof_term);
    let theorem = theorem_from_static_proof("Logic", "T", &statement_passport("Logic", "P"), &static_proof, 1).unwrap();
    let witness = Passport::runtime_witness("Logic", "P", &Passport::runtime_nat_from_input("Logic"));
    let provable = Passport::provable_claim("Logic", "Logic", "P", &static_proof);
    let truth = Passport::axiom_truth_from_provable("Logic", "P", &provable);

    for bad in [&proof_term, &static_proof, &theorem, &witness, &provable, &truth] {
        let err = substitution_source_from_passport(bad, 9).unwrap_err();
        assert_eq!(err.kind, DiagnosticKind::SubstitutionError);
    }

    let p = prop("P(x)");
    assert_eq!(substitution_source_from_passport(&p, 1).unwrap(), "P(x)");
}

#[test]
fn substitution_and_alpha_reports_preserve_taint() {
    let axiom = Passport::axiom_bool("Logic");
    let unsafe_nat = Passport::unsafe_nat("Logic");
    let report = substitution_report("P(x)", "x", "U", &[&axiom, &unsafe_nat], 1).unwrap();
    assert_eq!(report.max_trust, TrustLevel::Unsafe);
    assert!(report.has_axiom_taint);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);

    let passport = substitution_report_passport("Logic", &report, &[&axiom, &unsafe_nat]);
    assert_eq!(passport.trust, TrustLevel::Unsafe);

    let alpha = alpha_equivalence_report("forall x:Nat. P(x)", "forall y:Nat. P(y)", &[&axiom], 1).unwrap();
    assert!(alpha.has_axiom_taint);
    let passport = alpha_equivalence_passport("Logic", &alpha, &[&axiom]);
    assert!(matches!(passport.ty, TypeKind::AlphaEquivalenceReport { .. }));
    assert_eq!(passport.trust, TrustLevel::Axiom);
}

#[test]
fn substitution_exports_are_stable_and_order_sensitive() {
    let xy = substitution_report("R(x, y)", "x", "A", &[], 1).unwrap();
    let yx = substitution_report("R(x, y)", "y", "A", &[], 1).unwrap();
    assert_ne!(xy.fingerprint, yx.fingerprint);

    let exported = export_substitution_report(&xy);
    assert!(exported.contains("substitution_report: v1"));
    assert!(exported.contains("variable: x"));
    assert!(exported.contains("result: R(A, y)"));
    assert!(exported.contains(&xy.fingerprint));

    let scope = variable_scope_report("forall n:Nat. P(n)", 1).unwrap();
    let exported = export_variable_scope_report(&scope);
    assert!(exported.contains("variable_scope_report: v1"));
    assert!(exported.contains("- n:Nat (bound)"));

    let alpha = alpha_equivalence_report("forall x:Nat. P(x)", "forall z:Nat. P(z)", &[], 1).unwrap();
    let exported = export_alpha_equivalence_report(&alpha);
    assert!(exported.contains("alpha_equivalence_report: v1"));
    assert!(exported.contains("status: equivalent"));
}
