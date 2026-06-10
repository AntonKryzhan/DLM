use dlm_core::*;

fn static_eq_proof(theory: &str, lhs: &str, rhs: &str) -> Passport {
    let proposition = equality_proposition(lhs, rhs);
    let source = Passport::proposition(theory, proposition.clone(), None, "test:normalization:eq-source");
    Passport::static_proof(theory, proposition, &source)
}

fn rewrite_rule(theory: &str, name: &str, lhs: &str, rhs: &str) -> Passport {
    let proof = eq_proof_from_static_proof(theory, lhs, rhs, &static_eq_proof(theory, lhs, rhs), 1)
        .unwrap();
    rewrite_rule_from_eq_proof(theory, name, &proof, 1).unwrap()
}

#[test]
fn normalization_applies_ordered_forward_rewrites_to_normal_form() {
    let rules = vec![
        rewrite_rule("Core", "add_zero_right", "a + 0", "a"),
        rewrite_rule("Core", "a_to_b", "a", "b"),
    ];

    let report = normalize_with_rewrite_rules("Core", "a + 0", &rules, 8, 2).unwrap();
    let exported = export_rewrite_normalization_report(&report, 2).unwrap();

    assert_eq!(report.status, RewriteNormalizationStatus::Normalized);
    assert!(report.status.is_normal());
    assert_eq!(report.input, "a + 0");
    assert_eq!(report.normal_form, "b");
    assert_eq!(report.step_count(), 2);
    assert!(matches!(&report.certificate.ty, TypeKind::RewriteCertificate { from, to } if from == "a + 0" && to == "b"));
    assert!(exported.starts_with("DLM-REWRITE-NORMALIZATION v1\n"));
    assert!(exported.contains("status: Normalized\n"));
    assert!(exported.contains("0: add_zero_right:forward:a + 0->a"));
    assert!(exported.contains("1: a_to_b:forward:a->b"));
}

#[test]
fn already_normal_terms_emit_zero_step_certificate() {
    let rules = vec![rewrite_rule("Core", "x_to_y", "x", "y")];

    let report = normalize_with_rewrite_rules("Core", "z", &rules, 8, 3).unwrap();

    assert_eq!(report.status, RewriteNormalizationStatus::AlreadyNormal);
    assert!(report.is_already_normal());
    assert_eq!(report.normal_form, "z");
    assert_eq!(report.step_count(), 0);
    assert!(report.trace.is_empty());
    assert!(matches!(&report.certificate.ty, TypeKind::RewriteCertificate { from, to } if from == "z" && to == "z"));
    assert_eq!(report.certificate.trust, TrustLevel::Builtin);
}

#[test]
fn normalization_rejects_non_rewrite_rule_passports() {
    let raw_eq = reflexive_eq_proof("Core", "x", 4).unwrap();

    let err = normalize_with_rewrite_rules("Core", "x", &[raw_eq], 8, 4)
        .expect_err("normalization must require RewriteRule passports");

    assert_eq!(err.kind, DiagnosticKind::RewriteNormalizationError);
    assert!(err.message.contains("normalization rule #0"));
}

#[test]
fn normalization_step_limit_rejects_cyclic_rewrites() {
    let rules = vec![
        rewrite_rule("Core", "x_to_y", "x", "y"),
        rewrite_rule("Core", "y_to_x", "y", "x"),
    ];

    let err = normalize_with_rewrite_rules("Core", "x", &rules, 2, 5)
        .expect_err("cyclic rewrite chain must hit bounded normalization guard");

    assert_eq!(err.kind, DiagnosticKind::RewriteNormalizationError);
    assert!(err.message.contains("max_steps=2"));
    assert!(err.help.unwrap().contains("still rewriteable"));
}

#[test]
fn normalization_preserves_axiom_taint_in_certificate() {
    let proof = axiom_eq_proof("Core", "unsafe", "normal", "external normalization axiom", 6)
        .unwrap();
    let rule = rewrite_rule_from_eq_proof("Core", "unsafe_norm", &proof, 6).unwrap();

    let report = normalize_with_rewrite_rules("Core", "unsafe", &[rule], 8, 6).unwrap();

    assert_eq!(report.normal_form, "normal");
    assert!(report.is_axiom_tainted());
    assert_eq!(report.trace.trust, TrustLevel::Axiom);
    assert_eq!(report.certificate.trust, TrustLevel::Axiom);
    assert!(report.certificate.history.summary().contains("eq:axiom:unsafe:normal"));
}

#[test]
fn audit_rejects_tampered_normalization_reports() {
    let rules = vec![rewrite_rule("Core", "x_to_y", "x", "y")];
    let mut report = normalize_with_rewrite_rules("Core", "x", &rules, 8, 7).unwrap();

    report.normal_form = "tampered".to_string();
    let err = audit_rewrite_normalization_report(&report, 7)
        .expect_err("tampered normal form must not pass audit");

    assert_eq!(err.kind, DiagnosticKind::RewriteNormalizationError);
    assert!(err.message.contains("normal form"));
    assert!(export_rewrite_normalization_report(&report, 7).is_err());
    assert!(export_rewrite_normalization_report_unchecked(&report).contains("normal_form: tampered"));
}
