use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{Passport, Provenance, TrustLevel, TypeKind};
use crate::proof_context::{ProofClosure, ProofClosureStatus, TacticStep};
use crate::tactic::TacticScriptReport;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProofCertificateStatus {
    Checked,
    AxiomAdmitted,
}

impl ProofCertificateStatus {
    pub fn is_axiom_tainted(self) -> bool {
        matches!(self, ProofCertificateStatus::AxiomAdmitted)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProofCertificate {
    pub theory: String,
    pub theorem_name: String,
    pub proposition: String,
    pub status: ProofCertificateStatus,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub fingerprint: String,
    pub trace_len: usize,
    pub trace: Vec<String>,
}

impl ProofCertificate {
    pub fn is_axiom_tainted(&self) -> bool {
        self.status.is_axiom_tainted() || self.trust >= TrustLevel::Axiom
    }

    pub fn recomputed_fingerprint(&self) -> String {
        compute_certificate_fingerprint(
            &self.theory,
            &self.theorem_name,
            &self.proposition,
            self.status,
            self.trust,
            self.provenance,
            &self.trace,
        )
    }

    pub fn fingerprint_is_stable(&self) -> bool {
        self.fingerprint == self.recomputed_fingerprint()
    }
}

pub fn certificate_from_closure(
    closure: &ProofClosure,
    line: usize,
) -> Result<ProofCertificate, Diagnostic> {
    if !closure.obligations.is_empty() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "proof certificate cannot be emitted while proof obligations remain open",
        )
        .with_help(format!(
            "remaining obligations: {}",
            closure.obligations.len()
        )));
    }

    let (theorem_name, proposition) = theorem_parts(&closure.theorem, line)?;
    let status = match closure.status {
        ProofClosureStatus::ClosedByStaticProof => ProofCertificateStatus::Checked,
        ProofClosureStatus::AdmittedByAxiom => ProofCertificateStatus::AxiomAdmitted,
    };
    let trace = closure.steps.iter().map(trace_label).collect::<Vec<_>>();
    let theory = closure.theorem.theory.home.clone();
    let fingerprint = compute_certificate_fingerprint(
        &theory,
        &theorem_name,
        &proposition,
        status,
        closure.theorem.trust,
        closure.theorem.provenance,
        &trace,
    );

    Ok(ProofCertificate {
        theory,
        theorem_name,
        proposition,
        status,
        trust: closure.theorem.trust,
        provenance: closure.theorem.provenance,
        fingerprint,
        trace_len: trace.len(),
        trace,
    })
}

pub fn certificate_from_tactic_report(
    report: &TacticScriptReport,
    line: usize,
) -> Result<ProofCertificate, Diagnostic> {
    match &report.closure {
        Some(closure) => certificate_from_closure(closure, line),
        None => Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "tactic report has no proof closure to certify",
        )
        .with_help("execute a closing tactic before emitting a proof certificate")),
    }
}

pub fn verify_certificate_against_theorem(
    certificate: &ProofCertificate,
    theorem: &Passport,
    line: usize,
) -> Result<(), Diagnostic> {
    let (theorem_name, proposition) = theorem_parts(theorem, line)?;

    if certificate.theory != theorem.theory.home {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            format!(
                "certificate theory `{}` does not match theorem theory `{}`",
                certificate.theory, theorem.theory.home
            ),
        ));
    }

    if certificate.theorem_name != theorem_name || certificate.proposition != proposition {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "certificate theorem identity does not match theorem passport",
        )
        .with_help(format!(
            "certificate={}::{}, theorem={}::{}",
            certificate.theorem_name, certificate.proposition, theorem_name, proposition
        )));
    }

    if certificate.trust != theorem.trust || certificate.provenance != theorem.provenance {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "certificate trust/provenance does not match theorem passport",
        ));
    }

    if certificate.trace_len != certificate.trace.len() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "certificate trace length does not match embedded trace",
        ));
    }

    if !certificate.fingerprint_is_stable() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "certificate fingerprint does not match certificate contents",
        ));
    }

    Ok(())
}

pub fn compute_certificate_fingerprint(
    theory: &str,
    theorem_name: &str,
    proposition: &str,
    status: ProofCertificateStatus,
    trust: TrustLevel,
    provenance: Provenance,
    trace: &[String],
) -> String {
    let mut hash = FNV_OFFSET;
    hash = feed(hash, b"DLM-CERT-v1");
    hash = feed(hash, theory.as_bytes());
    hash = feed(hash, theorem_name.as_bytes());
    hash = feed(hash, proposition.as_bytes());
    hash = feed(hash, format!("{status:?}").as_bytes());
    hash = feed(hash, format!("{trust:?}").as_bytes());
    hash = feed(hash, format!("{provenance:?}").as_bytes());
    for item in trace {
        hash = feed(hash, item.as_bytes());
    }
    format!("dlm-cert-v1-{hash:016x}")
}

const FNV_OFFSET: u64 = 0xcbf29ce484222325;
const FNV_PRIME: u64 = 0x00000100000001B3;

fn feed(mut hash: u64, bytes: &[u8]) -> u64 {
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    hash ^= 0xff;
    hash = hash.wrapping_mul(FNV_PRIME);
    hash
}

fn theorem_parts(theorem: &Passport, line: usize) -> Result<(String, String), Diagnostic> {
    match &theorem.ty {
        TypeKind::Theorem { name, proposition } => Ok((name.clone(), proposition.clone())),
        other => Err(Diagnostic::error(
            DiagnosticKind::ProofCertificateError,
            Some(line),
            "proof certificate can only be emitted for Theorem passports",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

fn trace_label(step: &TacticStep) -> String {
    match step {
        TacticStep::OpenGoal { proposition } => format!("open:{proposition}"),
        TacticStep::Assume {
            hypothesis,
            proposition,
        } => format!("assume:{hypothesis}:{proposition}"),
        TacticStep::ExactStaticProof { proposition } => format!("exact:{proposition}"),
        TacticStep::AdmitAxiom { reason } => format!("admit:{reason}"),
        TacticStep::CloseTheorem { name, proposition } => format!("close:{name}:{proposition}"),
    }
}
