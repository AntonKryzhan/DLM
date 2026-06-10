use dlm_core::{
    certificate_from_closure, certificate_from_tactic_report, goal_passport, open_proof_context,
    statement_passport, verify_certificate_against_theorem, DiagnosticKind, Passport,
    ProofCertificateStatus, TacticScript, TrustLevel,
};

fn checked_script_report() -> dlm_core::TacticScriptReport {
    let goal = goal_passport("Meta", "kernel_checked:true_intro");
    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let term = Passport::proof_term("Meta", "true_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "kernel_checked:true_intro", &term);
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().exact_static_proof("TrueIntro", statement, proof);

    dlm_core::execute_tactic_script(context, &script, 4).unwrap()
}

#[test]
fn certificate_from_static_closure_is_stable_and_verifies() {
    let report = checked_script_report();
    let closure = report.closure.as_ref().unwrap();
    let certificate = certificate_from_closure(closure, 7).unwrap();

    assert_eq!(certificate.status, ProofCertificateStatus::Checked);
    assert_eq!(certificate.theorem_name, "TrueIntro");
    assert_eq!(certificate.proposition, "kernel_checked:true_intro");
    assert_eq!(certificate.trace_len, certificate.trace.len());
    assert!(certificate.fingerprint.starts_with("dlm-cert-v1-"));
    assert!(certificate.fingerprint_is_stable());
    assert!(!certificate.is_axiom_tainted());
    assert!(verify_certificate_against_theorem(&certificate, &closure.theorem, 7).is_ok());
}

#[test]
fn certificate_from_tactic_report_rejects_open_goals() {
    let goal = goal_passport("Meta", "P");
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let report = dlm_core::execute_tactic_script(context, &TacticScript::new(), 3).unwrap();

    let err = certificate_from_tactic_report(&report, 3).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofCertificateError);
    assert!(err.message.contains("no proof closure"));
}

#[test]
fn axiom_admission_certificate_keeps_axiom_taint_visible() {
    let goal = goal_passport("Meta", "reflection_boundary");
    let statement = statement_passport("Meta", "reflection_boundary");
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().admit_axiom(
        "ReflectionBoundary",
        statement,
        "temporary metatheory axiom",
    );
    let report = dlm_core::execute_tactic_script(context, &script, 8).unwrap();

    let certificate = certificate_from_tactic_report(&report, 8).unwrap();

    assert_eq!(certificate.status, ProofCertificateStatus::AxiomAdmitted);
    assert_eq!(certificate.trust, TrustLevel::Axiom);
    assert!(certificate.is_axiom_tainted());
    assert!(certificate.trace.iter().any(|step| step.contains("admit:temporary metatheory axiom")));
}

#[test]
fn certificate_verification_rejects_wrong_theorem_identity() {
    let report = checked_script_report();
    let closure = report.closure.as_ref().unwrap();
    let certificate = certificate_from_closure(closure, 10).unwrap();

    let wrong_statement = statement_passport("Meta", "Q");
    let wrong_theorem = dlm_core::axiom_theorem("Meta", "TrueIntro", &wrong_statement, 10).unwrap();

    let err = verify_certificate_against_theorem(&certificate, &wrong_theorem, 10).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofCertificateError);
    assert!(err.message.contains("identity"));
}

#[test]
fn certificate_fingerprint_depends_on_trace_order_and_contents() {
    let goal = goal_passport("Meta", "P");
    let statement = statement_passport("Meta", "P");
    let term = Passport::proof_term("Meta", "p_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "P", &term);

    let context_a = open_proof_context("Meta", goal.clone(), 1).unwrap();
    let script_a = TacticScript::new().exact_static_proof("PIntro", statement.clone(), proof.clone());
    let report_a = dlm_core::execute_tactic_script(context_a, &script_a, 11).unwrap();
    let cert_a = certificate_from_tactic_report(&report_a, 11).unwrap();

    let context_b = open_proof_context("Meta", goal.clone(), 1).unwrap();
    let script_b = TacticScript::new()
        .assume("P", goal)
        .exact_static_proof("PIntro", statement, proof);
    let report_b = dlm_core::execute_tactic_script(context_b, &script_b, 11).unwrap();
    let cert_b = certificate_from_tactic_report(&report_b, 11).unwrap();

    assert_eq!(cert_a.theorem_name, cert_b.theorem_name);
    assert_eq!(cert_a.proposition, cert_b.proposition);
    assert_ne!(cert_a.trace, cert_b.trace);
    assert_ne!(cert_a.fingerprint, cert_b.fingerprint);
}

#[test]
fn certificate_tampering_breaks_fingerprint_validation() {
    let report = checked_script_report();
    let closure = report.closure.as_ref().unwrap();
    let mut certificate = certificate_from_closure(closure, 13).unwrap();

    certificate.trace.push("tampered:extra-step".to_string());
    certificate.trace_len = certificate.trace.len();

    let err = verify_certificate_against_theorem(&certificate, &closure.theorem, 13).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofCertificateError);
    assert!(err.message.contains("fingerprint"));
}
