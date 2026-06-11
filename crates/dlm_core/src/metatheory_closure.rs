use std::collections::BTreeSet;
use std::fmt;

use crate::axiom_registry::{DependencyAuditStatus, MetatheoryDependencyAuditReport};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ClosureObligationKind {
    AxiomRegistry,
    DependencyAudit,
    SoundnessBoundary,
    ReflectionBoundary,
    ConsistencyBoundary,
    ModuleBoundary,
    ConservativeExtension,
    Unknown,
}

impl fmt::Display for ClosureObligationKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ClosureObligationKind::AxiomRegistry => write!(f, "axiom_registry"),
            ClosureObligationKind::DependencyAudit => write!(f, "dependency_audit"),
            ClosureObligationKind::SoundnessBoundary => write!(f, "soundness_boundary"),
            ClosureObligationKind::ReflectionBoundary => write!(f, "reflection_boundary"),
            ClosureObligationKind::ConsistencyBoundary => write!(f, "consistency_boundary"),
            ClosureObligationKind::ModuleBoundary => write!(f, "module_boundary"),
            ClosureObligationKind::ConservativeExtension => write!(f, "conservative_extension"),
            ClosureObligationKind::Unknown => write!(f, "unknown"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MetatheoryClosureStatus {
    Closed,
    Open,
    Rejected,
}

impl fmt::Display for MetatheoryClosureStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetatheoryClosureStatus::Closed => write!(f, "closed"),
            MetatheoryClosureStatus::Open => write!(f, "open"),
            MetatheoryClosureStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ClosureObligation {
    pub label: String,
    pub kind: ClosureObligationKind,
    pub description: String,
    pub closed_by: Option<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct MetatheoryClosureReport {
    pub subject: String,
    pub status: MetatheoryClosureStatus,
    pub primary_audit_fingerprint: String,
    pub supporting_audit_fingerprints: Vec<String>,
    pub obligations: Vec<ClosureObligation>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub closure_fingerprint: String,
}

pub fn closure_obligation(
    label: impl Into<String>,
    kind: ClosureObligationKind,
    description: impl Into<String>,
    closed_by: Option<impl Into<String>>,
    line: usize,
) -> Result<ClosureObligation, Diagnostic> {
    let label = require_non_empty(label.into(), "closure obligation label", line)?;
    let description = require_non_empty(description.into(), "closure obligation description", line)?;
    let closed_by = closed_by.map(|value| value.into()).filter(|value| !value.trim().is_empty());
    let fingerprint = stable_fingerprint(&[
        "closure-obligation:v1".to_string(),
        label.clone(),
        kind.to_string(),
        description.clone(),
        closed_by.clone().unwrap_or_else(|| "open".to_string()),
    ]);
    Ok(ClosureObligation { label, kind, description, closed_by, fingerprint })
}

pub fn open_closure_obligation(
    label: impl Into<String>,
    kind: ClosureObligationKind,
    description: impl Into<String>,
    line: usize,
) -> Result<ClosureObligation, Diagnostic> {
    closure_obligation(label, kind, description, Option::<String>::None, line)
}

pub fn closed_closure_obligation(
    label: impl Into<String>,
    kind: ClosureObligationKind,
    description: impl Into<String>,
    closed_by: impl Into<String>,
    line: usize,
) -> Result<ClosureObligation, Diagnostic> {
    closure_obligation(label, kind, description, Some(closed_by), line)
}

pub fn metatheory_closure_report(
    subject: impl Into<String>,
    primary_audit: &MetatheoryDependencyAuditReport,
    supporting_audits: &[MetatheoryDependencyAuditReport],
    obligations: Vec<ClosureObligation>,
    line: usize,
) -> MetatheoryClosureReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut max_trust = primary_audit.max_trust;
    let mut has_axiom_taint = primary_audit.has_axiom_taint;
    let mut has_oracle_taint = primary_audit.has_oracle_taint;
    let mut has_unsafe_taint = primary_audit.has_unsafe_taint;
    let primary_audit_fingerprint = primary_audit.audit_fingerprint.clone();
    let mut supporting_audit_fingerprints = Vec::new();
    let mut seen_audits = BTreeSet::new();
    let mut seen_obligations = BTreeSet::new();

    if subject.trim().is_empty() {
        diagnostics.push(meta_closure_error(
            line,
            "metatheory closure subject must not be empty",
            "closure reports need stable subject identity before they can serve as global metatheory evidence",
        ));
    }

    if primary_audit.status != DependencyAuditStatus::Verified {
        diagnostics.push(meta_closure_error(
            line,
            format!(
                "primary dependency audit `{}` is {}",
                primary_audit.subject, primary_audit.status
            ),
            "metatheory closure can only close over verified dependency audits",
        ));
    }

    if !seen_audits.insert(primary_audit.audit_fingerprint.clone()) {
        diagnostics.push(meta_closure_error(
            line,
            format!("duplicate audit fingerprint `{}`", primary_audit.audit_fingerprint),
            "closure audit fingerprints must be unique inside one closure report",
        ));
    }

    for audit in supporting_audits {
        if !seen_audits.insert(audit.audit_fingerprint.clone()) {
            diagnostics.push(meta_closure_error(
                line,
                format!("duplicate audit fingerprint `{}`", audit.audit_fingerprint),
                "supporting audits must not repeat the primary audit or each other",
            ));
        }
        if audit.status != DependencyAuditStatus::Verified {
            diagnostics.push(meta_closure_error(
                line,
                format!("supporting dependency audit `{}` is {}", audit.subject, audit.status),
                "rejected dependency audits cannot serve as closure evidence",
            ));
        }
        max_trust = max_trust.max(audit.max_trust);
        has_axiom_taint |= audit.has_axiom_taint;
        has_oracle_taint |= audit.has_oracle_taint;
        has_unsafe_taint |= audit.has_unsafe_taint;
        supporting_audit_fingerprints.push(audit.audit_fingerprint.clone());
    }

    for obligation in &obligations {
        if !seen_obligations.insert(obligation.fingerprint.clone()) {
            diagnostics.push(meta_closure_error(
                line,
                format!("duplicate closure obligation fingerprint `{}`", obligation.fingerprint),
                "closure obligations must be stable and non-duplicated inside one report",
            ));
        }
    }

    let has_open_obligations = obligations.iter().any(|obligation| obligation.closed_by.is_none());
    let status = if !diagnostics.is_empty() {
        MetatheoryClosureStatus::Rejected
    } else if has_open_obligations {
        MetatheoryClosureStatus::Open
    } else {
        MetatheoryClosureStatus::Closed
    };

    let closure_fingerprint = compute_closure_fingerprint(
        &subject,
        status,
        &primary_audit_fingerprint,
        &supporting_audit_fingerprints,
        &obligations,
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    MetatheoryClosureReport {
        subject,
        status,
        primary_audit_fingerprint,
        supporting_audit_fingerprints,
        obligations,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        closure_fingerprint,
    }
}

pub fn require_closed_metatheory_closure(
    report: &MetatheoryClosureReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == MetatheoryClosureStatus::Closed {
        Ok(())
    } else {
        Err(meta_closure_error(
            line,
            format!("metatheory closure `{}` is {}", report.subject, report.status),
            "only closed metatheory closure reports may serve as final closure evidence for later math/proof-kernel layers",
        ))
    }
}

pub fn metatheory_closure_report_passport(theory: &str, report: &MetatheoryClosureReport) -> Passport {
    let mut histories = vec![HistoryChain::from_event(format!(
        "metatheory:primary_dependency_audit:fingerprint={}",
        report.primary_audit_fingerprint
    ))];
    for fingerprint in &report.supporting_audit_fingerprints {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:supporting_dependency_audit:fingerprint={fingerprint}"
        )));
    }
    for obligation in &report.obligations {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:closure_obligation:{}:{}:{}",
            obligation.kind, obligation.label, obligation.fingerprint
        )));
    }
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "metatheory:closure_report:{}:{}:fingerprint={}",
            report.subject, report.status, report.closure_fingerprint
        ),
    );
    Passport {
        ty: TypeKind::MetatheoryClosureReport {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: metatheory_closure_capabilities(),
        cost: CostClass::ProofRequired,
        trust: report.max_trust,
        provenance: if report.has_unsafe_taint {
            Provenance::UnsafeExternal
        } else if report.has_oracle_taint {
            Provenance::OracleInput
        } else if report.has_axiom_taint {
            Provenance::BuiltinKnown
        } else {
            Provenance::InternalDerived
        },
        validation: if report.status == MetatheoryClosureStatus::Closed {
            ValidationState::StaticChecked
        } else if report.status == MetatheoryClosureStatus::Open {
            ValidationState::ConstraintChecked
        } else {
            ValidationState::Raw
        },
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_metatheory_closure_report(report: &MetatheoryClosureReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Metatheory Closure Report v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("primary_audit_fingerprint: {}\n", report.primary_audit_fingerprint));
    out.push_str(&format!("supporting_audits: {}\n", report.supporting_audit_fingerprints.len()));
    for fingerprint in &report.supporting_audit_fingerprints {
        out.push_str(&format!("- supporting_audit: {fingerprint}\n"));
    }
    out.push_str(&format!("obligations: {}\n", report.obligations.len()));
    for obligation in &report.obligations {
        out.push_str(&format!(
            "- {} kind={} closed_by={} fingerprint={} description={}\n",
            obligation.label,
            obligation.kind,
            obligation.closed_by.as_deref().unwrap_or("OPEN"),
            obligation.fingerprint,
            obligation.description
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

pub fn export_metatheory_closure_report(report: &MetatheoryClosureReport) -> String {
    render_metatheory_closure_report(report)
}

fn compute_closure_fingerprint(
    subject: &str,
    status: MetatheoryClosureStatus,
    primary_audit_fingerprint: &str,
    supporting_audit_fingerprints: &[String],
    obligations: &[ClosureObligation],
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "metatheory-closure:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        primary_audit_fingerprint.to_string(),
        format!("{max_trust:?}"),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    parts.extend(supporting_audit_fingerprints.iter().cloned());
    for obligation in obligations {
        parts.push(obligation.fingerprint.clone());
    }
    for diagnostic in diagnostics {
        parts.push(format!("{:?}:{}", diagnostic.kind, diagnostic.message));
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
    format!("dlm-meta-closure-v1-{hash:016x}")
}

fn metatheory_closure_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanInspectAst,
        Capability::CanMetaLevelReason,
        Capability::CanPropositionReason,
        Capability::CanTruthBoundaryReason,
    ])
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(meta_closure_error(
            line,
            format!("{label} must not be empty"),
            "metatheory closure objects need stable semantic identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn meta_closure_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::MetatheoryClosureError, Some(line), message).with_help(help)
}
