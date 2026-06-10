use dlm_core::{
    audit_certificate_against_theorem, certificate_from_tactic_report, export_certificate_text,
    export_certificate_text_unchecked, goal_passport, open_proof_context, render_certificate_audit_report,
    statement_passport, CertificateAuditStatus, DiagnosticKind, Passport, TacticScript,
};

fn checked_certificate() -> (dlm_core::ProofCertificate, dlm_core::Passport) {
    let goal = goal_passport("Meta", "kernel_checked:true_intro");
    let statement = statement_passport("Meta", "kernel_checked:true_intro");
    let term = Passport::proof_term("Meta", "true_intro", None);
    let proof = Passport::kernel_checked_proof("Meta", "kernel_checked:true_intro", &term);
    let context = open_proof_context("Meta", goal, 1).unwrap();
    let script = TacticScript::new().exact_static_proof("TrueIntro", statement, proof);
    let report = dlm_core::execute_tactic_script(context, &script, 4).unwrap();
    let theorem = report.closure.as_ref().unwrap().theorem.clone();
    let certificate = certificate_from_tactic_report(&report, 4).unwrap();
    (certificate, theorem)
}

#[test]
fn certificate_export_is_stable_and_contains_audit_fields() {
    let (certificate, _) = checked_certificate();
    let first = export_certificate_text(&certificate, 1).unwrap();
    let second = export_certificate_text(&certificate, 1).unwrap();

    assert_eq!(first, second);
    assert!(first.starts_with("DLM-PROOF-CERTIFICATE v1\n"));
    assert!(first.contains("theorem: TrueIntro\n"));
    assert!(first.contains("proposition: kernel_checked:true_intro\n"));
    assert!(first.contains("status: Checked\n"));
    assert!(first.contains("fingerprint: dlm-cert-v1-"));
    assert!(first.contains("trace_len: 3\n"));
    assert!(first.contains("0: open:kernel_checked:true_intro"));
    assert!(first.contains("2: close:TrueIntro:kernel_checked:true_intro"));
}

#[test]
fn audit_report_verifies_certificate_against_theorem() {
    let (certificate, theorem) = checked_certificate();
    let report = audit_certificate_against_theorem(&certificate, &theorem, 2);
    let rendered = render_certificate_audit_report(&report);

    assert_eq!(report.status, CertificateAuditStatus::Verified);
    assert!(report.verified());
    assert!(report.diagnostics.is_empty());
    assert!(rendered.starts_with("DLM-PROOF-CERTIFICATE-AUDIT v1\n"));
    assert!(rendered.contains("status: Verified\n"));
    assert!(rendered.contains("diagnostics: []\n"));
}

#[test]
fn audit_report_rejects_tampered_certificate() {
    let (mut certificate, theorem) = checked_certificate();
    certificate.trace.push("tampered:extra".to_string());
    certificate.trace_len = certificate.trace.len();

    let err = export_certificate_text(&certificate, 3).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::ProofCertificateAuditError);
    assert!(err.message.contains("fingerprint"));

    let report = audit_certificate_against_theorem(&certificate, &theorem, 3);
    assert_eq!(report.status, CertificateAuditStatus::Rejected);
    assert!(!report.verified());
    assert!(report.diagnostics.iter().any(|d| d.contains("fingerprint")));
}

#[test]
fn audit_report_rejects_wrong_theorem() {
    let (certificate, _) = checked_certificate();
    let wrong_statement = statement_passport("Meta", "different proposition");
    let wrong_theorem = dlm_core::axiom_theorem("Meta", "TrueIntro", &wrong_statement, 9).unwrap();

    let report = audit_certificate_against_theorem(&certificate, &wrong_theorem, 9);

    assert_eq!(report.status, CertificateAuditStatus::Rejected);
    assert!(report.diagnostics.iter().any(|d| d.contains("identity")));
}

#[test]
fn export_unchecked_is_available_for_forensic_rendering_only() {
    let (mut certificate, _) = checked_certificate();
    certificate.trace_len += 1;

    assert!(export_certificate_text(&certificate, 4).is_err());
    let forensic = export_certificate_text_unchecked(&certificate);

    assert!(forensic.contains("DLM-PROOF-CERTIFICATE v1"));
    assert!(forensic.contains("trace_len: 4"));
}
