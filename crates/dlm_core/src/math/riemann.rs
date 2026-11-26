//! Binding to the externally checked Lean proof of Mathlib's RiemannHypothesis.

use crate::certificate::{ProofCertificate, ProofCertificateStatus};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext,
    Passport, Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

pub const SOURCE: &str = "Lean.riemannHypothesis";
pub const PROPOSITION: &str = concat!(
    "∀ (s : ℂ), riemannZeta s = 0 → ",
    "(¬ ∃ n : ℕ, s = -2 * (↑n + 1)) → s ≠ 1 → s.re = 1 / 2"
);
pub const SOLUTION_SHA256: &str =
    "5ea74b912147c5c93cc2b6d70cc727cf908df2f62ac6a20480a3fa02f6be244e";
pub const SOLUTION_BYTES: u64 = 61_570_595_788;
pub const CHALLENGE_SHA256: &str =
    "bd484ac9886a96f26b1d9a7a59ebd45b7a70d102c015d4f31f9302bbdd6ab2b9";
pub const CHALLENGE_BYTES: u64 = 350_862_311;
pub const VERIFICATION_URL: &str = "https://math.mit.edu/~drew/grh/";
pub const CHECKER_REVISION: &str = "5dc36dc15b7bee891a02406b78a8ac816a47baad";
pub const AXIOMS: [&str; 3] = ["propext", "Classical.choice", "Quot.sound"];

pub(crate) const PROOF_EVENT: &str = "proof:external-lean:";
pub(crate) const AXIOM_EVENT: &str = "axiom:lean:";
pub(crate) const BINDING_EVENT: &str = "binding:external-lean:";

pub(crate) fn require_source(source: &str, line: usize) -> Result<(), Diagnostic> {
    if source == SOURCE {
        return Ok(());
    }
    Err(Diagnostic::error(
        DiagnosticKind::StatementTheoremError,
        Some(line),
        format!("unknown external theorem '{source}'; supported declaration: {SOURCE}"),
    ))
}

fn trace() -> Vec<String> {
    let mut trace = vec![
        format!("{BINDING_EVENT}{SOURCE}"),
        "declaration:Main.lean:riemannHypothesis : RiemannHypothesis".into(),
        "statement:Mathlib.NumberTheory.LSeries.RiemannZeta:RiemannHypothesis".into(),
        "mathlib:0df444a360eaa60ab8c11dca51a86af692955474".into(),
        "lean:v4.33.1".into(),
        "author:Levent Alpöge:2026-11-20".into(),
        format!("verification:{VERIFICATION_URL}"),
        format!("comparison:con-leche:{CHECKER_REVISION}:--compare"),
        format!("sha256:solution.ndjson:{SOLUTION_SHA256}"),
        format!("sha256:challenge.ndjson:{CHALLENGE_SHA256}"),
        "sha256:audit.log:0a1473fce6eed840011f4a76b1c51558fc6f03edd1e6bd1ee542e6c738d87a62".into(),
        "sha256:con-leche-compare.log:5a068bf55f24904eea9f05eda947c63d43bdee3c6ab0490e43943a3bd9af1f6e".into(),
    ];
    trace.extend(AXIOMS.map(|axiom| format!("{AXIOM_EVENT}{axiom}")));
    trace
}

pub(crate) fn certify(theory: &str, name: &str) -> (Passport, ProofCertificate) {
    let trace = trace();
    let mut history = HistoryChain::from_event(format!("{PROOF_EVENT}theorem={name}"));
    for event in &trace {
        history.push(event.clone());
    }
    let passport = Passport {
        ty: TypeKind::Theorem {
            name: name.into(),
            proposition: PROPOSITION.into(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Axiom,
        provenance: Provenance::ExternalFile,
        validation: ValidationState::ConstraintChecked,
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    };
    let mut certificate = ProofCertificate {
        theory: theory.into(),
        theorem_name: name.into(),
        proposition: PROPOSITION.into(),
        status: ProofCertificateStatus::ExternallyVerified,
        trust: passport.trust,
        provenance: passport.provenance,
        fingerprint: String::new(),
        trace_len: trace.len(),
        trace,
        kernel: None,
    };
    certificate.fingerprint = certificate.recomputed_fingerprint();
    (passport, certificate)
}

pub(crate) fn verify_binding(
    certificate: &ProofCertificate,
    line: usize,
) -> Result<(), Diagnostic> {
    if certificate.proposition == PROPOSITION
        && certificate.trust == TrustLevel::Axiom
        && certificate.provenance == Provenance::ExternalFile
        && certificate.trace == trace()
        && certificate.kernel.is_none()
    {
        return Ok(());
    }
    Err(Diagnostic::error(
        DiagnosticKind::ProofCertificateError,
        Some(line),
        "external Lean certificate differs from the pinned RiemannHypothesis proof binding",
    ))
}

pub(crate) fn has_binding(passport: &Passport, theorem: &str) -> bool {
    let binding = format!("{PROOF_EVENT}theorem={theorem}");
    passport.history.events().contains(&binding)
}
