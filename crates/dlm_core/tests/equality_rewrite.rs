use dlm_core::*;

fn static_eq_proof(theory: &str, lhs: &str, rhs: &str) -> Passport {
    let source = Passport::proposition(theory, equality_proposition(lhs, rhs), None, "test:eq:source");
    Passport::static_proof(theory, equality_proposition(lhs, rhs), &source)
}

#[test]
fn reflexive_equality_is_proof_not_boolean() {
    let eq = reflexive_eq_proof("Core", "x", 1).unwrap();

    assert!(matches!(&eq.ty, TypeKind::EqProof { lhs, rhs } if lhs == "x" && rhs == "x"));
    assert!(!matches!(&eq.ty, TypeKind::Bool));
    assert!(eq.capabilities.contains(Capability::CanCompareByProof));
}

#[test]
fn equality_proof_requires_static_proof_of_exact_equality() {
    let good = static_eq_proof("Core", "a + 0", "a");
    let eq = eq_proof_from_static_proof("Core", "a + 0", "a", &good, 2).unwrap();
    assert!(matches!(&eq.ty, TypeKind::EqProof { lhs, rhs } if lhs == "a + 0" && rhs == "a"));

    let wrong_source = Passport::static_proof(
        "Core",
        equality_proposition("a", "a + 0"),
        &Passport::proposition("Core", "wrong", None, "test:wrong"),
    );
    let err = eq_proof_from_static_proof("Core", "a + 0", "a", &wrong_source, 3)
        .expect_err("wrong equality direction must not construct EqProof");
    assert_eq!(err.kind, DiagnosticKind::EqualityRewriteError);

    let runtime = Passport::runtime_witness("Core", equality_proposition("a + 0", "a"), &good);
    let err = eq_proof_from_static_proof("Core", "a + 0", "a", &runtime, 4)
        .expect_err("runtime witness must not justify static EqProof");
    assert_eq!(err.kind, DiagnosticKind::EqualityRewriteError);
}

#[test]
fn rewrite_rule_is_derived_from_eq_proof_and_applies_in_both_directions() {
    let proof = eq_proof_from_static_proof("Core", "a + 0", "a", &static_eq_proof("Core", "a + 0", "a"), 5).unwrap();
    let rule = rewrite_rule_from_eq_proof("Core", "add_zero_right", &proof, 6).unwrap();

    assert!(matches!(&rule.ty, TypeKind::RewriteRule { name, lhs, rhs } if name == "add_zero_right" && lhs == "a + 0" && rhs == "a"));

    let forward = apply_rewrite_rule(&rule, "a + 0", RewriteDirection::Forward, 7).unwrap();
    assert_eq!(forward.from, "a + 0");
    assert_eq!(forward.to, "a");

    let reverse = apply_rewrite_rule(&rule, "a", RewriteDirection::Reverse, 8).unwrap();
    assert_eq!(reverse.from, "a");
    assert_eq!(reverse.to, "a + 0");
}

#[test]
fn rewrite_application_rejects_non_matching_source_term() {
    let proof = eq_proof_from_static_proof("Core", "x", "y", &static_eq_proof("Core", "x", "y"), 9).unwrap();
    let rule = rewrite_rule_from_eq_proof("Core", "x_to_y", &proof, 10).unwrap();

    let err = apply_rewrite_rule(&rule, "z", RewriteDirection::Forward, 11)
        .expect_err("rewrite rule must only apply to matching source terms");
    assert_eq!(err.kind, DiagnosticKind::EqualityRewriteError);
}

#[test]
fn rewrite_trace_preserves_order_and_builds_certificate() {
    let proof1 = eq_proof_from_static_proof("Core", "a + 0", "a", &static_eq_proof("Core", "a + 0", "a"), 12).unwrap();
    let rule1 = rewrite_rule_from_eq_proof("Core", "add_zero_right", &proof1, 13).unwrap();

    let proof2 = eq_proof_from_static_proof("Core", "a", "b", &static_eq_proof("Core", "a", "b"), 14).unwrap();
    let rule2 = rewrite_rule_from_eq_proof("Core", "a_eq_b", &proof2, 15).unwrap();

    let trace = rewrite_trace(
        "Core",
        "a + 0",
        &[(rule1, RewriteDirection::Forward), (rule2, RewriteDirection::Forward)],
        16,
    )
    .unwrap();

    assert_eq!(trace.from, "a + 0");
    assert_eq!(trace.to, "b");
    assert_eq!(trace.len(), 2);
    assert_eq!(trace.step_labels()[0], "rewrite:add_zero_right:forward:a + 0->a");
    assert_eq!(trace.step_labels()[1], "rewrite:a_eq_b:forward:a->b");

    let certificate = rewrite_certificate_passport("Core", &trace);
    assert!(matches!(&certificate.ty, TypeKind::RewriteCertificate { from, to } if from == "a + 0" && to == "b"));
    assert!(certificate.history.summary().contains("rewrite:certificate:a + 0->b:steps=2"));
}

#[test]
fn axiom_equality_taint_is_visible_in_rewrite_certificate() {
    let proof = axiom_eq_proof("Core", "unsafe_lhs", "unsafe_rhs", "external rewrite axiom", 17).unwrap();
    assert_eq!(proof.trust, TrustLevel::Axiom);

    let rule = rewrite_rule_from_eq_proof("Core", "unsafe_rewrite", &proof, 18).unwrap();
    let trace = rewrite_trace("Core", "unsafe_lhs", &[(rule, RewriteDirection::Forward)], 19).unwrap();
    let certificate = rewrite_certificate_passport("Core", &trace);

    assert!(trace.is_axiom_tainted());
    assert!(matches!(&certificate.ty, TypeKind::RewriteCertificate { from, to } if from == "unsafe_lhs" && to == "unsafe_rhs"));
    assert_eq!(certificate.trust, TrustLevel::Axiom);
    assert!(certificate.history.summary().contains("eq:axiom:unsafe_lhs:unsafe_rhs"));
}

#[test]
fn bool_equality_and_raw_eqproof_are_not_rewrite_applications() {
    let bool_value = Passport {
        ty: TypeKind::Bool,
        construction: ConstructionMode::Literal,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::Trivial,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalLiteral,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new("Core"),
        history: HistoryChain::from_event("test:bool-equality-result"),
        location: LocationContext::local(),
    };

    let err = rewrite_rule_from_eq_proof("Core", "bad", &bool_value, 20)
        .expect_err("Bool equality result must not be accepted as EqProof");
    assert_eq!(err.kind, DiagnosticKind::EqualityRewriteError);

    let raw_eq = reflexive_eq_proof("Core", "x", 21).unwrap();
    let err = apply_rewrite_rule(&raw_eq, "x", RewriteDirection::Forward, 22)
        .expect_err("EqProof must be turned into RewriteRule before application");
    assert_eq!(err.kind, DiagnosticKind::EqualityRewriteError);
}
