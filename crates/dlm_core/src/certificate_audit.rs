use crate::certificate::{verify_certificate_against_theorem, ProofCertificate};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{Passport, TypeKind};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CertificateAuditStatus {
    Verified,
    Rejected,
}

impl CertificateAuditStatus {
    pub fn is_verified(self) -> bool {
        matches!(self, CertificateAuditStatus::Verified)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CertificateAuditReport {
    pub status: CertificateAuditStatus,
    pub theory: String,
    pub theorem_name: String,
    pub proposition: String,
    pub fingerprint: String,
    pub axiom_tainted: bool,
    pub trace_len: usize,
    pub diagnostics: Vec<String>,
}

impl CertificateAuditReport {
    pub fn verified(&self) -> bool {
        self.status.is_verified()
    }
}

pub fn export_certificate_text(
    certificate: &ProofCertificate,
    line: usize,
) -> Result<String, Diagnostic> {
    validate_exportable_certificate(certificate, line)?;
    Ok(export_certificate_text_unchecked(certificate))
}

pub fn export_certificate_text_unchecked(certificate: &ProofCertificate) -> String {
    let mut out = String::new();
    push_line(&mut out, "DLM-PROOF-CERTIFICATE v1");
    push_field(&mut out, "theory", &certificate.theory);
    push_field(&mut out, "theorem", &certificate.theorem_name);
    push_field(&mut out, "proposition", &certificate.proposition);
    push_field(&mut out, "status", &format!("{:?}", certificate.status));
    push_field(&mut out, "trust", &format!("{:?}", certificate.trust));
    push_field(&mut out, "provenance", &format!("{:?}", certificate.provenance));
    push_field(&mut out, "fingerprint", &certificate.fingerprint);
    push_field(&mut out, "axiom_tainted", &certificate.is_axiom_tainted().to_string());
    push_field(&mut out, "trace_len", &certificate.trace_len.to_string());
    push_line(&mut out, "trace:");
    for (index, step) in certificate.trace.iter().enumerate() {
        push_line(&mut out, &format!("  {index}: {step}"));
    }
    out
}

pub fn audit_certificate_against_theorem(
    certificate: &ProofCertificate,
    theorem: &Passport,
    line: usize,
) -> CertificateAuditReport {
    let mut diagnostics = Vec::new();
    let mut status = CertificateAuditStatus::Verified;

    if let Err(diagnostic) = validate_exportable_certificate(certificate, line) {
        status = CertificateAuditStatus::Rejected;
        diagnostics.push(diagnostic.message);
    }

    if let Err(diagnostic) = verify_certificate_against_theorem(certificate, theorem, line) {
        status = CertificateAuditStatus::Rejected;
        diagnostics.push(diagnostic.message);
    }

    if let TypeKind::Theorem { name, proposition } = &theorem.ty {
        if certificate.theorem_name != name.as_str() || certificate.proposition != proposition.as_str() {
            status = CertificateAuditStatus::Rejected;
        }
    } else {
        status = CertificateAuditStatus::Rejected;
        diagnostics.push("audit target is not a theorem passport".to_string());
    }

    CertificateAuditReport {
        status,
        theory: certificate.theory.clone(),
        theorem_name: certificate.theorem_name.clone(),
        proposition: certificate.proposition.clone(),
        fingerprint: certificate.fingerprint.clone(),
        axiom_tainted: certificate.is_axiom_tainted(),
        trace_len: certificate.trace_len,
        diagnostics,
    }
}

pub fn render_certificate_audit_report(report: &CertificateAuditReport) -> String {
    let mut out = String::new();
    push_line(&mut out, "DLM-PROOF-CERTIFICATE-AUDIT v1");
    push_field(&mut out, "status", &format!("{:?}", report.status));
    push_field(&mut out, "theory", &report.theory);
    push_field(&mut out, "theorem", &report.theorem_name);
    push_field(&mut out, "proposition", &report.proposition);
    push_field(&mut out, "fingerprint", &report.fingerprint);
    push_field(&mut out, "axiom_tainted", &report.axiom_tainted.to_string());
    push_field(&mut out, "trace_len", &report.trace_len.to_string());
    if report.diagnostics.is_empty() {
        push_line(&mut out, "diagnostics: []");
    } else {
        push_line(&mut out, "diagnostics:");
        for (index, diagnostic) in report.diagnostics.iter().enumerate() {
            push_line(&mut out, &format!("  {index}: {diagnostic}"));
        }
    }
    out
}

fn validate_exportable_certificate(certificate: &ProofCertificate, line: usize) -> Result<(), Diagnostic> {
    if certificate.trace_len != certificate.trace.len() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateAuditError,
            Some(line),
            "certificate trace length does not match embedded trace",
        ));
    }

    if !certificate.fingerprint_is_stable() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateAuditError,
            Some(line),
            "certificate fingerprint does not match certificate contents",
        ));
    }

    if certificate.theory.trim().is_empty()
        || certificate.theorem_name.trim().is_empty()
        || certificate.proposition.trim().is_empty()
    {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateAuditError,
            Some(line),
            "certificate identity fields must be non-empty",
        ));
    }

    Ok(())
}

fn push_field(out: &mut String, key: &str, value: &str) {
    push_line(out, &format!("{key}: {value}"));
}

fn push_line(out: &mut String, line: &str) {
    out.push_str(line);
    out.push('\n');
}
