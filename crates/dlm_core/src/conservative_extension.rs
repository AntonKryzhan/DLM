use std::collections::BTreeSet;
use std::fmt;

use crate::axiom_registry::DependencyEntry;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::metatheory_closure::{MetatheoryClosureReport, MetatheoryClosureStatus};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ConservativeExtensionStatus {
    Verified,
    Open,
    Rejected,
}

impl fmt::Display for ConservativeExtensionStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ConservativeExtensionStatus::Verified => write!(f, "verified"),
            ConservativeExtensionStatus::Open => write!(f, "open"),
            ConservativeExtensionStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreservedTheorem {
    pub name: String,
    pub proposition: String,
    pub base_type: String,
    pub extension_type: String,
    pub base_history: Vec<String>,
    pub extension_history: Vec<String>,
    pub base_fingerprint: String,
    pub extension_fingerprint: String,
    pub preservation_fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct ConservativeExtensionAuditReport {
    pub base_subject: String,
    pub extension_subject: String,
    pub status: ConservativeExtensionStatus,
    pub base_closure_fingerprint: String,
    pub extension_closure_fingerprint: String,
    pub preserved_theorems: Vec<PreservedTheorem>,
    pub new_assumptions: Vec<DependencyEntry>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub audit_fingerprint: String,
}

pub fn preserved_theorem(
    name: impl Into<String>,
    base_theorem: &Passport,
    extension_theorem: &Passport,
    line: usize,
) -> Result<PreservedTheorem, Diagnostic> {
    let requested_name = require_non_empty(name.into(), "preserved theorem name", line)?;
    let (base_name, base_prop) = require_theorem(base_theorem, "base theorem", line)?;
    let (extension_name, extension_prop) = require_theorem(extension_theorem, "extension theorem", line)?;

    if requested_name != base_name {
        return Err(conservative_extension_error(
            line,
            format!(
                "preserved theorem request `{requested_name}` does not match base theorem `{base_name}`"
            ),
            "conservative extension evidence must identify the old theorem being preserved exactly",
        ));
    }

    if base_name != extension_name {
        return Err(conservative_extension_error(
            line,
            format!("base theorem `{base_name}` is not preserved by extension theorem `{extension_name}`"),
            "old theorem names must be preserved exactly across conservative extension audits",
        ));
    }

    if base_prop != extension_prop {
        return Err(conservative_extension_error(
            line,
            format!(
                "theorem `{base_name}` proposition changed from `{base_prop}` to `{extension_prop}`"
            ),
            "a conservative extension may add new vocabulary, but must not mutate old theorem statements",
        ));
    }

    let base_type = base_theorem.ty.to_string();
    let extension_type = extension_theorem.ty.to_string();
    let base_history = base_theorem.history.events().to_vec();
    let extension_history = extension_theorem.history.events().to_vec();
    let base_fingerprint = passport_fingerprint("base-theorem", base_theorem);
    let extension_fingerprint = passport_fingerprint("extension-theorem", extension_theorem);
    let preservation_fingerprint = stable_fingerprint(&[
        "preserved-theorem:v1".to_string(),
        requested_name.clone(),
        base_prop.clone(),
        base_fingerprint.clone(),
        extension_fingerprint.clone(),
    ]);

    Ok(PreservedTheorem {
        name: requested_name,
        proposition: base_prop,
        base_type,
        extension_type,
        base_history,
        extension_history,
        base_fingerprint,
        extension_fingerprint,
        preservation_fingerprint,
    })
}

pub fn audit_conservative_extension(
    base: &MetatheoryClosureReport,
    extension: &MetatheoryClosureReport,
    preserved_theorems: Vec<PreservedTheorem>,
    new_assumptions: Vec<DependencyEntry>,
    line: usize,
) -> ConservativeExtensionAuditReport {
    let mut diagnostics = Vec::new();
    let mut seen_theorems = BTreeSet::new();
    let mut seen_assumptions = BTreeSet::new();
    let mut max_trust = base.max_trust.max(extension.max_trust);
    let mut has_axiom_taint = base.has_axiom_taint || extension.has_axiom_taint;
    let mut has_oracle_taint = base.has_oracle_taint || extension.has_oracle_taint;
    let mut has_unsafe_taint = base.has_unsafe_taint || extension.has_unsafe_taint;

    if base.status != MetatheoryClosureStatus::Closed {
        diagnostics.push(conservative_extension_error(
            line,
            format!("base metatheory closure `{}` is {}", base.subject, base.status),
            "the base theory must be closed before extension conservativity can be judged",
        ));
    }

    if extension.status == MetatheoryClosureStatus::Rejected {
        diagnostics.push(conservative_extension_error(
            line,
            format!("extension metatheory closure `{}` is rejected", extension.subject),
            "rejected extension closure cannot serve as conservative extension evidence",
        ));
    }

    if preserved_theorems.is_empty() {
        diagnostics.push(conservative_extension_error(
            line,
            "conservative extension audit has no preserved theorem evidence",
            "at least one old theorem preservation witness is required to avoid vacuous extension audits",
        ));
    }

    for theorem in &preserved_theorems {
        if !seen_theorems.insert(theorem.name.clone()) {
            diagnostics.push(conservative_extension_error(
                line,
                format!("duplicate preserved theorem `{}`", theorem.name),
                "each old theorem should have one preservation witness in a single conservative extension audit",
            ));
        }
    }

    for assumption in &new_assumptions {
        if !seen_assumptions.insert(assumption.fingerprint.clone()) {
            diagnostics.push(conservative_extension_error(
                line,
                format!("duplicate new assumption fingerprint `{}`", assumption.fingerprint),
                "new assumptions must be visible and non-duplicated in conservative extension audits",
            ));
        }
        max_trust = max_trust.max(assumption.trust);
        has_axiom_taint |= assumption.trust >= TrustLevel::Axiom;
        has_oracle_taint |= assumption.trust >= TrustLevel::Oracle;
        has_unsafe_taint |= assumption.trust >= TrustLevel::Unsafe || assumption.provenance == Provenance::UnsafeExternal;
    }

    let status = if !diagnostics.is_empty() {
        ConservativeExtensionStatus::Rejected
    } else if extension.status == MetatheoryClosureStatus::Open {
        ConservativeExtensionStatus::Open
    } else {
        ConservativeExtensionStatus::Verified
    };

    let audit_fingerprint = compute_conservative_extension_fingerprint(
        &base.subject,
        &extension.subject,
        status,
        &base.closure_fingerprint,
        &extension.closure_fingerprint,
        &preserved_theorems,
        &new_assumptions,
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    ConservativeExtensionAuditReport {
        base_subject: base.subject.clone(),
        extension_subject: extension.subject.clone(),
        status,
        base_closure_fingerprint: base.closure_fingerprint.clone(),
        extension_closure_fingerprint: extension.closure_fingerprint.clone(),
        preserved_theorems,
        new_assumptions,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        audit_fingerprint,
    }
}

pub fn require_verified_conservative_extension_audit(
    report: &ConservativeExtensionAuditReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == ConservativeExtensionStatus::Verified {
        Ok(())
    } else {
        Err(conservative_extension_error(
            line,
            format!(
                "conservative extension audit `{} -> {}` is {}",
                report.base_subject, report.extension_subject, report.status
            ),
            "only verified conservative extension audits may serve as old-theorem preservation evidence",
        ))
    }
}

pub fn conservative_extension_audit_passport(
    theory: &str,
    report: &ConservativeExtensionAuditReport,
) -> Passport {
    let mut histories = vec![
        HistoryChain::from_event(format!(
            "metatheory:base_closure:fingerprint={}",
            report.base_closure_fingerprint
        )),
        HistoryChain::from_event(format!(
            "metatheory:extension_closure:fingerprint={}",
            report.extension_closure_fingerprint
        )),
    ];
    for theorem in &report.preserved_theorems {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:preserved_theorem:{}:{}",
            theorem.name, theorem.preservation_fingerprint
        )));
    }
    for assumption in &report.new_assumptions {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:new_assumption:{}:{}:{}",
            assumption.kind, assumption.label, assumption.fingerprint
        )));
    }
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "metatheory:conservative_extension:{}->{}:{}:fingerprint={}",
            report.base_subject, report.extension_subject, report.status, report.audit_fingerprint
        ),
    );

    Passport {
        ty: TypeKind::ConservativeExtensionAudit {
            base: report.base_subject.clone(),
            extension: report.extension_subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: conservative_extension_capabilities(),
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
        validation: if report.status == ConservativeExtensionStatus::Verified {
            ValidationState::StaticChecked
        } else if report.status == ConservativeExtensionStatus::Open {
            ValidationState::ConstraintChecked
        } else {
            ValidationState::Raw
        },
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_conservative_extension_audit_report(report: &ConservativeExtensionAuditReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Conservative Extension Audit v1\n");
    out.push_str(&format!("base: {}\n", report.base_subject));
    out.push_str(&format!("extension: {}\n", report.extension_subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("base_closure_fingerprint: {}\n", report.base_closure_fingerprint));
    out.push_str(&format!("extension_closure_fingerprint: {}\n", report.extension_closure_fingerprint));
    out.push_str(&format!("preserved_theorems: {}\n", report.preserved_theorems.len()));
    for theorem in &report.preserved_theorems {
        out.push_str(&format!(
            "- {} proposition={} preservation_fingerprint={}\n",
            theorem.name, theorem.proposition, theorem.preservation_fingerprint
        ));
    }
    out.push_str(&format!("new_assumptions: {}\n", report.new_assumptions.len()));
    for assumption in &report.new_assumptions {
        out.push_str(&format!(
            "- {} kind={} type={} trust={:?} fingerprint={}\n",
            assumption.label, assumption.kind, assumption.ty, assumption.trust, assumption.fingerprint
        ));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out.push_str(&format!("audit_fingerprint: {}\n", report.audit_fingerprint));
    out
}

pub fn export_conservative_extension_audit_report(report: &ConservativeExtensionAuditReport) -> String {
    render_conservative_extension_audit_report(report)
}

fn require_theorem<'a>(passport: &'a Passport, label: &str, line: usize) -> Result<(String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::Theorem { name, proposition } => Ok((name.clone(), proposition.clone())),
        other => Err(conservative_extension_error(
            line,
            format!("{label} must be Theorem, got {other}"),
            "conservative extension evidence preserves old theorems, not statements, goals, proof terms, or runtime witnesses",
        )),
    }
}

fn passport_fingerprint(prefix: &str, passport: &Passport) -> String {
    let mut parts = vec![
        format!("passport-fingerprint:{prefix}:v1"),
        passport.ty.to_string(),
        format!("{:?}", passport.trust),
        format!("{:?}", passport.provenance),
        format!("{:?}", passport.validation),
        passport.theory.home.clone(),
    ];
    parts.extend(passport.history.events().iter().cloned());
    stable_fingerprint(&parts)
}

fn compute_conservative_extension_fingerprint(
    base_subject: &str,
    extension_subject: &str,
    status: ConservativeExtensionStatus,
    base_closure_fingerprint: &str,
    extension_closure_fingerprint: &str,
    preserved_theorems: &[PreservedTheorem],
    new_assumptions: &[DependencyEntry],
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "conservative-extension-audit:v1".to_string(),
        base_subject.to_string(),
        extension_subject.to_string(),
        status.to_string(),
        base_closure_fingerprint.to_string(),
        extension_closure_fingerprint.to_string(),
        format!("{max_trust:?}"),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    for theorem in preserved_theorems {
        parts.push(theorem.preservation_fingerprint.clone());
    }
    for assumption in new_assumptions {
        parts.push(assumption.fingerprint.clone());
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
    format!("dlm-conservative-extension-v1-{hash:016x}")
}

fn conservative_extension_capabilities() -> CapabilitySet {
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
        Err(conservative_extension_error(
            line,
            format!("{label} must not be empty"),
            "conservative extension audit objects need stable semantic identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn conservative_extension_error(
    line: usize,
    message: impl Into<String>,
    help: impl Into<String>,
) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::ConservativeExtensionError, Some(line), message).with_help(help)
}
