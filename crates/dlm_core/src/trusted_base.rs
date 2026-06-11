use std::collections::BTreeSet;
use std::fmt;

use crate::axiom_registry::{AxiomRegistry, DependencyAuditStatus, MetatheoryDependencyAuditReport};
use crate::bridge_assumption::{SoundnessBoundaryLedgerReport, SoundnessBoundaryStatus};
use crate::conservative_extension::{ConservativeExtensionAuditReport, ConservativeExtensionStatus};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::metatheory_closure::{MetatheoryClosureReport, MetatheoryClosureStatus};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::theorem_dependency::{GlobalMetatheoryInventoryReport, MetatheoryInventoryStatus};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TrustedBaseEvidenceKind {
    AxiomRegistry,
    DependencyAudit,
    MetatheoryClosure,
    GlobalInventory,
    SoundnessBoundaryLedger,
    ConservativeExtensionAudit,
}

impl fmt::Display for TrustedBaseEvidenceKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TrustedBaseEvidenceKind::AxiomRegistry => write!(f, "axiom_registry"),
            TrustedBaseEvidenceKind::DependencyAudit => write!(f, "dependency_audit"),
            TrustedBaseEvidenceKind::MetatheoryClosure => write!(f, "metatheory_closure"),
            TrustedBaseEvidenceKind::GlobalInventory => write!(f, "global_inventory"),
            TrustedBaseEvidenceKind::SoundnessBoundaryLedger => write!(f, "soundness_boundary_ledger"),
            TrustedBaseEvidenceKind::ConservativeExtensionAudit => write!(f, "conservative_extension_audit"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TrustedBaseClosureStatus {
    Closed,
    Open,
    Rejected,
}

impl fmt::Display for TrustedBaseClosureStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TrustedBaseClosureStatus::Closed => write!(f, "closed"),
            TrustedBaseClosureStatus::Open => write!(f, "open"),
            TrustedBaseClosureStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TrustedBaseEvidence {
    pub id: String,
    pub kind: TrustedBaseEvidenceKind,
    pub subject: String,
    pub status: TrustedBaseClosureStatus,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub history: Vec<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct TrustedBaseClosureReport {
    pub subject: String,
    pub status: TrustedBaseClosureStatus,
    pub evidence: Vec<TrustedBaseEvidence>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub closure_fingerprint: String,
}

pub fn trusted_base_evidence_from_axiom_registry(
    id: impl Into<String>,
    registry: &AxiomRegistry,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(registry.theory.clone(), "axiom registry theory", line)?;
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;
    for axiom in &registry.axioms {
        max_trust = max_trust.max(axiom.trust);
        has_axiom_taint |= axiom.trust >= TrustLevel::Axiom;
        has_oracle_taint |= axiom.trust >= TrustLevel::Oracle || axiom.provenance == Provenance::OracleInput;
        has_unsafe_taint |= axiom.trust >= TrustLevel::Unsafe || axiom.provenance == Provenance::UnsafeExternal;
    }
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::AxiomRegistry,
        subject: subject.clone(),
        status: TrustedBaseClosureStatus::Closed,
        trust: max_trust,
        provenance: provenance_from_taints(has_axiom_taint, has_oracle_taint, has_unsafe_taint),
        validation: ValidationState::StaticChecked,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        history: vec![format!("trusted_base:axiom_registry:{subject}:{}", registry.fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:axiom-registry:v1".to_string(),
            subject,
            registry.fingerprint.clone(),
            format!("{:?}", max_trust),
            has_axiom_taint.to_string(),
            has_oracle_taint.to_string(),
            has_unsafe_taint.to_string(),
        ]),
    })
}

pub fn trusted_base_evidence_from_dependency_audit(
    id: impl Into<String>,
    report: &MetatheoryDependencyAuditReport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(report.subject.clone(), "dependency audit subject", line)?;
    let status = match report.status {
        DependencyAuditStatus::Verified => TrustedBaseClosureStatus::Closed,
        DependencyAuditStatus::Rejected => TrustedBaseClosureStatus::Rejected,
    };
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::DependencyAudit,
        subject: subject.clone(),
        status,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: if status == TrustedBaseClosureStatus::Closed { ValidationState::StaticChecked } else { ValidationState::Raw },
        has_axiom_taint: report.has_axiom_taint,
        has_oracle_taint: report.has_oracle_taint,
        has_unsafe_taint: report.has_unsafe_taint,
        history: vec![format!("trusted_base:dependency_audit:{subject}:{}", report.audit_fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:dependency-audit:v1".to_string(),
            subject,
            status.to_string(),
            report.audit_fingerprint.clone(),
            format!("{:?}", report.max_trust),
        ]),
    })
}

pub fn trusted_base_evidence_from_metatheory_closure(
    id: impl Into<String>,
    report: &MetatheoryClosureReport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(report.subject.clone(), "metatheory closure subject", line)?;
    let status = match report.status {
        MetatheoryClosureStatus::Closed => TrustedBaseClosureStatus::Closed,
        MetatheoryClosureStatus::Open => TrustedBaseClosureStatus::Open,
        MetatheoryClosureStatus::Rejected => TrustedBaseClosureStatus::Rejected,
    };
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::MetatheoryClosure,
        subject: subject.clone(),
        status,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_status(status),
        has_axiom_taint: report.has_axiom_taint,
        has_oracle_taint: report.has_oracle_taint,
        has_unsafe_taint: report.has_unsafe_taint,
        history: vec![format!("trusted_base:metatheory_closure:{subject}:{}", report.closure_fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:metatheory-closure:v1".to_string(),
            subject,
            status.to_string(),
            report.closure_fingerprint.clone(),
            format!("{:?}", report.max_trust),
        ]),
    })
}

pub fn trusted_base_evidence_from_global_inventory(
    id: impl Into<String>,
    report: &GlobalMetatheoryInventoryReport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(report.subject.clone(), "global inventory subject", line)?;
    let status = match report.status {
        MetatheoryInventoryStatus::Verified => TrustedBaseClosureStatus::Closed,
        MetatheoryInventoryStatus::Open => TrustedBaseClosureStatus::Open,
        MetatheoryInventoryStatus::Rejected => TrustedBaseClosureStatus::Rejected,
    };
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::GlobalInventory,
        subject: subject.clone(),
        status,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_status(status),
        has_axiom_taint: report.has_axiom_taint,
        has_oracle_taint: report.has_oracle_taint,
        has_unsafe_taint: report.has_unsafe_taint,
        history: vec![format!("trusted_base:global_inventory:{subject}:{}", report.inventory_fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:global-inventory:v1".to_string(),
            subject,
            status.to_string(),
            report.inventory_fingerprint.clone(),
            format!("{:?}", report.max_trust),
        ]),
    })
}

pub fn trusted_base_evidence_from_soundness_boundary_ledger(
    id: impl Into<String>,
    report: &SoundnessBoundaryLedgerReport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(report.subject.clone(), "soundness boundary ledger subject", line)?;
    let status = match report.status {
        SoundnessBoundaryStatus::Verified => TrustedBaseClosureStatus::Closed,
        SoundnessBoundaryStatus::Open => TrustedBaseClosureStatus::Open,
        SoundnessBoundaryStatus::Rejected => TrustedBaseClosureStatus::Rejected,
    };
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::SoundnessBoundaryLedger,
        subject: subject.clone(),
        status,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_status(status),
        has_axiom_taint: report.has_axiom_taint,
        has_oracle_taint: report.has_oracle_taint,
        has_unsafe_taint: report.has_unsafe_taint,
        history: vec![format!("trusted_base:soundness_boundary_ledger:{subject}:{}", report.ledger_fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:soundness-boundary-ledger:v1".to_string(),
            subject,
            status.to_string(),
            report.ledger_fingerprint.clone(),
            format!("{:?}", report.max_trust),
        ]),
    })
}

pub fn trusted_base_evidence_from_conservative_extension(
    id: impl Into<String>,
    report: &ConservativeExtensionAuditReport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let subject = require_non_empty(
        format!("{}->{}", report.base_subject, report.extension_subject),
        "conservative extension subject",
        line,
    )?;
    let status = match report.status {
        ConservativeExtensionStatus::Verified => TrustedBaseClosureStatus::Closed,
        ConservativeExtensionStatus::Open => TrustedBaseClosureStatus::Open,
        ConservativeExtensionStatus::Rejected => TrustedBaseClosureStatus::Rejected,
    };
    Ok(TrustedBaseEvidence {
        id,
        kind: TrustedBaseEvidenceKind::ConservativeExtensionAudit,
        subject: subject.clone(),
        status,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_status(status),
        has_axiom_taint: report.has_axiom_taint,
        has_oracle_taint: report.has_oracle_taint,
        has_unsafe_taint: report.has_unsafe_taint,
        history: vec![format!("trusted_base:conservative_extension:{subject}:{}", report.audit_fingerprint)],
        fingerprint: stable_fingerprint(&[
            "trusted-base-evidence:conservative-extension:v1".to_string(),
            subject,
            status.to_string(),
            report.audit_fingerprint.clone(),
            format!("{:?}", report.max_trust),
        ]),
    })
}

pub fn trusted_base_evidence_from_passport(
    id: impl Into<String>,
    passport: &Passport,
    line: usize,
) -> Result<TrustedBaseEvidence, Diagnostic> {
    let id = require_non_empty(id.into(), "trusted-base evidence id", line)?;
    let (kind, subject, status) = match &passport.ty {
        TypeKind::AxiomRegistry { theory } => (TrustedBaseEvidenceKind::AxiomRegistry, theory.clone(), TrustedBaseClosureStatus::Closed),
        TypeKind::MetatheoryDependencyAudit { subject, status } => (
            TrustedBaseEvidenceKind::DependencyAudit,
            subject.clone(),
            status_from_text(status),
        ),
        TypeKind::MetatheoryClosureReport { subject, status } => (
            TrustedBaseEvidenceKind::MetatheoryClosure,
            subject.clone(),
            status_from_text(status),
        ),
        TypeKind::GlobalMetatheoryInventory { subject, status } => (
            TrustedBaseEvidenceKind::GlobalInventory,
            subject.clone(),
            status_from_text(status),
        ),
        TypeKind::SoundnessBoundaryLedger { subject, status } => (
            TrustedBaseEvidenceKind::SoundnessBoundaryLedger,
            subject.clone(),
            status_from_text(status),
        ),
        TypeKind::ConservativeExtensionAudit { base, extension, status } => (
            TrustedBaseEvidenceKind::ConservativeExtensionAudit,
            format!("{base}->{extension}"),
            status_from_text(status),
        ),
        _ => {
            return Err(trusted_base_error(
                line,
                format!("passport `{}` is not trusted-base closure evidence", passport.ty),
                "trusted-base closure evidence must be an axiom registry, dependency audit, metatheory closure, global inventory, soundness boundary ledger, or conservative extension audit",
            ));
        }
    };
    let subject = require_non_empty(subject, "trusted-base evidence subject", line)?;
    let history = passport.history.events().to_vec();
    let has_axiom_taint = passport.trust >= TrustLevel::Axiom;
    let has_oracle_taint = passport.trust >= TrustLevel::Oracle || passport.provenance == Provenance::OracleInput;
    let has_unsafe_taint = passport.trust >= TrustLevel::Unsafe || passport.provenance == Provenance::UnsafeExternal;
    let mut parts = vec![
        "trusted-base-evidence:passport:v1".to_string(),
        id.clone(),
        kind.to_string(),
        subject.clone(),
        status.to_string(),
        passport.ty.to_string(),
        format!("{:?}", passport.trust),
        format!("{:?}", passport.provenance),
        format!("{:?}", passport.validation),
    ];
    parts.extend(history.iter().cloned());
    Ok(TrustedBaseEvidence {
        id,
        kind,
        subject,
        status,
        trust: passport.trust,
        provenance: passport.provenance,
        validation: passport.validation,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        history,
        fingerprint: stable_fingerprint(&parts),
    })
}

pub fn trusted_base_closure(
    subject: impl Into<String>,
    evidence: Vec<TrustedBaseEvidence>,
    line: usize,
) -> TrustedBaseClosureReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut ids = BTreeSet::new();
    let mut fingerprints = BTreeSet::new();
    let mut present_kinds = BTreeSet::new();
    let mut has_open_evidence = false;
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;

    if subject.trim().is_empty() {
        diagnostics.push(trusted_base_error(
            line,
            "trusted-base closure subject must not be empty",
            "trusted-base closure reports need stable subject identity before they can serve as final metatheory gate evidence",
        ));
    }
    if evidence.is_empty() {
        diagnostics.push(trusted_base_error(
            line,
            "trusted-base closure has no evidence",
            "the final metatheory gate must account for axiom registry, dependency audit, closure, inventory, and soundness-boundary ledger evidence",
        ));
    }

    for item in &evidence {
        if !ids.insert(item.id.clone()) {
            diagnostics.push(trusted_base_error(
                line,
                format!("duplicate trusted-base evidence id `{}`", item.id),
                "each final-gate evidence item must have a unique stable id",
            ));
        }
        if !fingerprints.insert(item.fingerprint.clone()) {
            diagnostics.push(trusted_base_error(
                line,
                format!("duplicate trusted-base evidence fingerprint `{}`", item.fingerprint),
                "duplicated final-gate evidence must be recorded once or made explicit with a distinct id and fingerprint",
            ));
        }
        present_kinds.insert(item.kind);
        match item.status {
            TrustedBaseClosureStatus::Closed => {}
            TrustedBaseClosureStatus::Open => has_open_evidence = true,
            TrustedBaseClosureStatus::Rejected => diagnostics.push(trusted_base_error(
                line,
                format!("trusted-base evidence `{}` is rejected", item.id),
                "rejected evidence cannot be hidden inside a final metatheory closure gate",
            )),
        }
        max_trust = max_trust.max(item.trust);
        has_axiom_taint |= item.has_axiom_taint || item.trust >= TrustLevel::Axiom;
        has_oracle_taint |= item.has_oracle_taint || item.trust >= TrustLevel::Oracle || item.provenance == Provenance::OracleInput;
        has_unsafe_taint |= item.has_unsafe_taint || item.trust >= TrustLevel::Unsafe || item.provenance == Provenance::UnsafeExternal;
    }

    for required in required_final_gate_kinds() {
        if !present_kinds.contains(&required) {
            diagnostics.push(trusted_base_error(
                line,
                format!("trusted-base closure is missing required evidence kind `{required}`"),
                "the final metatheory gate requires axiom registry, dependency audit, metatheory closure, global inventory, and soundness boundary ledger evidence",
            ));
        }
    }

    let status = if !diagnostics.is_empty() {
        TrustedBaseClosureStatus::Rejected
    } else if has_open_evidence {
        TrustedBaseClosureStatus::Open
    } else {
        TrustedBaseClosureStatus::Closed
    };

    let closure_fingerprint = compute_closure_fingerprint(
        &subject,
        status,
        &evidence,
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    TrustedBaseClosureReport {
        subject,
        status,
        evidence,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        closure_fingerprint,
    }
}

pub fn require_closed_trusted_base_closure(
    report: &TrustedBaseClosureReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == TrustedBaseClosureStatus::Closed {
        Ok(())
    } else {
        Err(trusted_base_error(
            line,
            format!("trusted-base closure `{}` is {}", report.subject, report.status),
            "only closed trusted-base closure reports may mark the metamathematical foundation as complete",
        ))
    }
}

pub fn trusted_base_closure_passport(
    theory: &str,
    report: &TrustedBaseClosureReport,
) -> Passport {
    let histories: Vec<HistoryChain> = report
        .evidence
        .iter()
        .map(|item| {
            HistoryChain::from_event(format!(
                "trusted_base:evidence:{}:{}:{}",
                item.kind, item.id, item.fingerprint
            ))
        })
        .collect();
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "trusted_base:closure:{}:{}:fingerprint={}",
            report.subject, report.status, report.closure_fingerprint
        ),
    );
    Passport {
        ty: TypeKind::TrustedBaseClosure {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: trusted_base_capabilities(),
        cost: CostClass::ProofRequired,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_status(report.status),
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_trusted_base_closure(report: &TrustedBaseClosureReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Trusted Base Closure v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("evidence: {}\n", report.evidence.len()));
    for item in &report.evidence {
        out.push_str(&format!(
            "- {} kind={} subject={} status={} trust={:?} fingerprint={}\n",
            item.id, item.kind, item.subject, item.status, item.trust, item.fingerprint
        ));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out.push_str(&format!("closure_fingerprint: {}\n", report.closure_fingerprint));
    out
}

pub fn export_trusted_base_closure(report: &TrustedBaseClosureReport) -> String {
    render_trusted_base_closure(report)
}

fn required_final_gate_kinds() -> [TrustedBaseEvidenceKind; 5] {
    [
        TrustedBaseEvidenceKind::AxiomRegistry,
        TrustedBaseEvidenceKind::DependencyAudit,
        TrustedBaseEvidenceKind::MetatheoryClosure,
        TrustedBaseEvidenceKind::GlobalInventory,
        TrustedBaseEvidenceKind::SoundnessBoundaryLedger,
    ]
}

fn validation_from_status(status: TrustedBaseClosureStatus) -> ValidationState {
    match status {
        TrustedBaseClosureStatus::Closed => ValidationState::StaticChecked,
        TrustedBaseClosureStatus::Open => ValidationState::ConstraintChecked,
        TrustedBaseClosureStatus::Rejected => ValidationState::Raw,
    }
}

fn status_from_text(status: &str) -> TrustedBaseClosureStatus {
    match status {
        "closed" | "verified" => TrustedBaseClosureStatus::Closed,
        "open" => TrustedBaseClosureStatus::Open,
        _ => TrustedBaseClosureStatus::Rejected,
    }
}

fn provenance_from_taints(
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> Provenance {
    if has_unsafe_taint {
        Provenance::UnsafeExternal
    } else if has_oracle_taint {
        Provenance::OracleInput
    } else if has_axiom_taint {
        Provenance::BuiltinKnown
    } else {
        Provenance::InternalDerived
    }
}

fn trusted_base_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanInspectAst,
        Capability::CanMetaLevelReason,
        Capability::CanPropositionReason,
        Capability::CanProofKernelCheck,
        Capability::CanTruthBoundaryReason,
        Capability::CanConsistencyReason,
    ])
}

fn compute_closure_fingerprint(
    subject: &str,
    status: TrustedBaseClosureStatus,
    evidence: &[TrustedBaseEvidence],
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "trusted-base-closure:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        format!("{:?}", max_trust),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    for item in evidence {
        parts.push(format!(
            "evidence:{}:{}:{}:{}:{:?}",
            item.id, item.kind, item.status, item.fingerprint, item.trust
        ));
    }
    for diagnostic in diagnostics {
        parts.push(format!("diagnostic:{:?}:{}", diagnostic.kind, diagnostic.message));
    }
    stable_fingerprint(&parts)
}

fn stable_fingerprint(parts: &[String]) -> String {
    let mut hash: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
        hash ^= 0xff;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    format!("dlm-trusted-base-closure-v1-{hash:016x}")
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(trusted_base_error(
            line,
            format!("{label} must not be empty"),
            "trusted-base closure evidence requires stable non-empty identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn trusted_base_error(
    line: usize,
    message: impl Into<String>,
    help: impl Into<String>,
) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::TrustedBaseError, Some(line), message).with_help(help)
}
