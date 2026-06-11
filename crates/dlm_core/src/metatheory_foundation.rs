use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::trusted_base::{TrustedBaseClosureReport, TrustedBaseClosureStatus};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MetatheoryExitCriterionKind {
    MetaLevelStratification,
    TruthProvabilityBoundary,
    ConsistencyBoundary,
    ReflectionBoundary,
    StatementTheoremBoundary,
    ProofContextBoundary,
    EqualityRewriteBoundary,
    RewriteNormalizationBoundary,
    InductionBoundary,
    ModuleBoundary,
    AxiomAccounting,
    DependencyAccounting,
    ClosureAccounting,
    ConservativeExtensionAccounting,
    TheoremDependencyInventory,
    SoundnessBoundaryLedger,
    TrustedBaseClosure,
    DiagnosticCoverage,
    RegressionCoverage,
}

impl fmt::Display for MetatheoryExitCriterionKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetatheoryExitCriterionKind::MetaLevelStratification => write!(f, "meta_level_stratification"),
            MetatheoryExitCriterionKind::TruthProvabilityBoundary => write!(f, "truth_provability_boundary"),
            MetatheoryExitCriterionKind::ConsistencyBoundary => write!(f, "consistency_boundary"),
            MetatheoryExitCriterionKind::ReflectionBoundary => write!(f, "reflection_boundary"),
            MetatheoryExitCriterionKind::StatementTheoremBoundary => write!(f, "statement_theorem_boundary"),
            MetatheoryExitCriterionKind::ProofContextBoundary => write!(f, "proof_context_boundary"),
            MetatheoryExitCriterionKind::EqualityRewriteBoundary => write!(f, "equality_rewrite_boundary"),
            MetatheoryExitCriterionKind::RewriteNormalizationBoundary => write!(f, "rewrite_normalization_boundary"),
            MetatheoryExitCriterionKind::InductionBoundary => write!(f, "induction_boundary"),
            MetatheoryExitCriterionKind::ModuleBoundary => write!(f, "module_boundary"),
            MetatheoryExitCriterionKind::AxiomAccounting => write!(f, "axiom_accounting"),
            MetatheoryExitCriterionKind::DependencyAccounting => write!(f, "dependency_accounting"),
            MetatheoryExitCriterionKind::ClosureAccounting => write!(f, "closure_accounting"),
            MetatheoryExitCriterionKind::ConservativeExtensionAccounting => write!(f, "conservative_extension_accounting"),
            MetatheoryExitCriterionKind::TheoremDependencyInventory => write!(f, "theorem_dependency_inventory"),
            MetatheoryExitCriterionKind::SoundnessBoundaryLedger => write!(f, "soundness_boundary_ledger"),
            MetatheoryExitCriterionKind::TrustedBaseClosure => write!(f, "trusted_base_closure"),
            MetatheoryExitCriterionKind::DiagnosticCoverage => write!(f, "diagnostic_coverage"),
            MetatheoryExitCriterionKind::RegressionCoverage => write!(f, "regression_coverage"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MetatheoryExitCriterionStatus {
    Satisfied,
    Open,
    Failed,
}

impl fmt::Display for MetatheoryExitCriterionStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetatheoryExitCriterionStatus::Satisfied => write!(f, "satisfied"),
            MetatheoryExitCriterionStatus::Open => write!(f, "open"),
            MetatheoryExitCriterionStatus::Failed => write!(f, "failed"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MetatheoryFoundationStatus {
    Ready,
    Incomplete,
    Rejected,
}

impl fmt::Display for MetatheoryFoundationStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetatheoryFoundationStatus::Ready => write!(f, "ready"),
            MetatheoryFoundationStatus::Incomplete => write!(f, "incomplete"),
            MetatheoryFoundationStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MetatheoryExitCriterion {
    pub id: String,
    pub kind: MetatheoryExitCriterionKind,
    pub subject: String,
    pub status: MetatheoryExitCriterionStatus,
    pub evidence: String,
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
pub struct MetatheoryFoundationExitReport {
    pub subject: String,
    pub status: MetatheoryFoundationStatus,
    pub criteria: Vec<MetatheoryExitCriterion>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub exit_fingerprint: String,
}

pub fn metatheory_exit_criterion(
    id: impl Into<String>,
    kind: MetatheoryExitCriterionKind,
    subject: impl Into<String>,
    status: MetatheoryExitCriterionStatus,
    evidence: impl Into<String>,
    trust: TrustLevel,
    provenance: Provenance,
    validation: ValidationState,
    history: Vec<String>,
    line: usize,
) -> Result<MetatheoryExitCriterion, Diagnostic> {
    let id = require_non_empty(id.into(), "metatheory exit criterion id", line)?;
    let subject = require_non_empty(subject.into(), "metatheory exit criterion subject", line)?;
    let evidence = require_non_empty(evidence.into(), "metatheory exit criterion evidence", line)?;
    let has_axiom_taint = trust >= TrustLevel::Axiom;
    let has_oracle_taint = trust >= TrustLevel::Oracle || provenance == Provenance::OracleInput;
    let has_unsafe_taint = trust >= TrustLevel::Unsafe || provenance == Provenance::UnsafeExternal;
    let mut parts = vec![
        "metatheory-exit-criterion:v1".to_string(),
        id.clone(),
        kind.to_string(),
        subject.clone(),
        status.to_string(),
        evidence.clone(),
        format!("{:?}", trust),
        format!("{:?}", provenance),
        format!("{:?}", validation),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    parts.extend(history.iter().cloned());
    Ok(MetatheoryExitCriterion {
        id,
        kind,
        subject,
        status,
        evidence,
        trust,
        provenance,
        validation,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        history,
        fingerprint: stable_fingerprint(&parts),
    })
}

pub fn metatheory_exit_criterion_from_passport(
    id: impl Into<String>,
    kind: MetatheoryExitCriterionKind,
    passport: &Passport,
    line: usize,
) -> Result<MetatheoryExitCriterion, Diagnostic> {
    let subject = subject_from_passport(passport, line)?;
    let status = status_from_passport(passport);
    let history = passport.history.events().to_vec();
    metatheory_exit_criterion(
        id,
        kind,
        subject,
        status,
        passport.ty.to_string(),
        passport.trust,
        passport.provenance,
        passport.validation,
        history,
        line,
    )
}

pub fn metatheory_exit_criterion_from_trusted_base_report(
    id: impl Into<String>,
    report: &TrustedBaseClosureReport,
    line: usize,
) -> Result<MetatheoryExitCriterion, Diagnostic> {
    let status = match report.status {
        TrustedBaseClosureStatus::Closed => MetatheoryExitCriterionStatus::Satisfied,
        TrustedBaseClosureStatus::Open => MetatheoryExitCriterionStatus::Open,
        TrustedBaseClosureStatus::Rejected => MetatheoryExitCriterionStatus::Failed,
    };
    let history = vec![format!(
        "metatheory_exit:trusted_base:{}:{}:fingerprint={}",
        report.subject, report.status, report.closure_fingerprint
    )];
    metatheory_exit_criterion(
        id,
        MetatheoryExitCriterionKind::TrustedBaseClosure,
        report.subject.clone(),
        status,
        report.closure_fingerprint.clone(),
        report.max_trust,
        provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation_from_foundation_status(match status {
            MetatheoryExitCriterionStatus::Satisfied => MetatheoryFoundationStatus::Ready,
            MetatheoryExitCriterionStatus::Open => MetatheoryFoundationStatus::Incomplete,
            MetatheoryExitCriterionStatus::Failed => MetatheoryFoundationStatus::Rejected,
        }),
        history,
        line,
    )
}

pub fn metatheory_foundation_exit_report(
    subject: impl Into<String>,
    criteria: Vec<MetatheoryExitCriterion>,
    line: usize,
) -> MetatheoryFoundationExitReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut ids = BTreeSet::new();
    let mut fingerprints = BTreeSet::new();
    let mut present_kinds = BTreeSet::new();
    let mut has_open = false;
    let mut has_failed = false;
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;

    if subject.trim().is_empty() {
        diagnostics.push(metatheory_foundation_error(
            line,
            "metatheory foundation exit subject must not be empty",
            "the foundation exit report needs stable subject identity before it can unlock the next development phase",
        ));
    }
    if criteria.is_empty() {
        diagnostics.push(metatheory_foundation_error(
            line,
            "metatheory foundation exit report has no criteria",
            "the exit gate must enumerate every required metamathematical foundation criterion",
        ));
    }

    for criterion in &criteria {
        if !ids.insert(criterion.id.clone()) {
            diagnostics.push(metatheory_foundation_error(
                line,
                format!("duplicate metatheory exit criterion id `{}`", criterion.id),
                "each exit criterion must have a unique stable id",
            ));
        }
        if !fingerprints.insert(criterion.fingerprint.clone()) {
            diagnostics.push(metatheory_foundation_error(
                line,
                format!("duplicate metatheory exit criterion fingerprint `{}`", criterion.fingerprint),
                "duplicated exit evidence must be recorded once or made explicit with distinct identity",
            ));
        }
        present_kinds.insert(criterion.kind);
        match criterion.status {
            MetatheoryExitCriterionStatus::Satisfied => {}
            MetatheoryExitCriterionStatus::Open => has_open = true,
            MetatheoryExitCriterionStatus::Failed => {
                has_failed = true;
                diagnostics.push(metatheory_foundation_error(
                    line,
                    format!("metatheory exit criterion `{}` failed", criterion.id),
                    "failed foundation criteria block the transition to ordinary language mathematics",
                ));
            }
        }
        max_trust = max_trust.max(criterion.trust);
        has_axiom_taint |= criterion.has_axiom_taint || criterion.trust >= TrustLevel::Axiom;
        has_oracle_taint |= criterion.has_oracle_taint || criterion.trust >= TrustLevel::Oracle || criterion.provenance == Provenance::OracleInput;
        has_unsafe_taint |= criterion.has_unsafe_taint || criterion.trust >= TrustLevel::Unsafe || criterion.provenance == Provenance::UnsafeExternal;
    }

    let mut missing_required = false;
    for required in required_metatheory_exit_criteria() {
        if !present_kinds.contains(&required) {
            missing_required = true;
            diagnostics.push(metatheory_foundation_error(
                line,
                format!("metatheory foundation exit is missing required criterion `{required}`"),
                "the metamathematical foundation cannot be marked ready until every required criterion is explicitly satisfied",
            ));
        }
    }

    let status = if has_failed || diagnostics.iter().any(|d| d.message.contains("duplicate") || d.message.contains("subject must not be empty") || d.message.contains("has no criteria")) {
        MetatheoryFoundationStatus::Rejected
    } else if has_open || missing_required {
        MetatheoryFoundationStatus::Incomplete
    } else {
        MetatheoryFoundationStatus::Ready
    };

    let exit_fingerprint = compute_exit_fingerprint(
        &subject,
        status,
        &criteria,
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    MetatheoryFoundationExitReport {
        subject,
        status,
        criteria,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        exit_fingerprint,
    }
}

pub fn require_metatheory_foundation_ready(
    report: &MetatheoryFoundationExitReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == MetatheoryFoundationStatus::Ready {
        Ok(())
    } else {
        Err(metatheory_foundation_error(
            line,
            format!("metatheory foundation `{}` is {}", report.subject, report.status),
            "only a ready metatheory foundation exit report may unlock ordinary mathematics language development",
        ))
    }
}

pub fn metatheory_foundation_exit_passport(
    theory: &str,
    report: &MetatheoryFoundationExitReport,
) -> Passport {
    let histories: Vec<HistoryChain> = report
        .criteria
        .iter()
        .map(|item| {
            HistoryChain::from_event(format!(
                "metatheory_exit:criterion:{}:{}:{}",
                item.kind, item.id, item.fingerprint
            ))
        })
        .collect();
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "metatheory_exit:foundation:{}:{}:fingerprint={}",
            report.subject, report.status, report.exit_fingerprint
        ),
    );
    Passport {
        ty: TypeKind::MetatheoryFoundationExit {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: metatheory_foundation_capabilities(),
        cost: CostClass::ProofRequired,
        trust: report.max_trust,
        provenance: provenance_from_taints(report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint),
        validation: validation_from_foundation_status(report.status),
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_metatheory_foundation_exit_report(report: &MetatheoryFoundationExitReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Metatheory Foundation Exit Report v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("criteria: {}\n", report.criteria.len()));
    for item in &report.criteria {
        out.push_str(&format!(
            "- {} kind={} status={} trust={:?} evidence={} fingerprint={}\n",
            item.id, item.kind, item.status, item.trust, item.evidence, item.fingerprint
        ));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out.push_str(&format!("exit_fingerprint: {}\n", report.exit_fingerprint));
    out
}

pub fn export_metatheory_foundation_exit_report(report: &MetatheoryFoundationExitReport) -> String {
    render_metatheory_foundation_exit_report(report)
}

pub fn required_metatheory_exit_criteria() -> [MetatheoryExitCriterionKind; 17] {
    [
        MetatheoryExitCriterionKind::MetaLevelStratification,
        MetatheoryExitCriterionKind::TruthProvabilityBoundary,
        MetatheoryExitCriterionKind::ConsistencyBoundary,
        MetatheoryExitCriterionKind::ReflectionBoundary,
        MetatheoryExitCriterionKind::StatementTheoremBoundary,
        MetatheoryExitCriterionKind::ProofContextBoundary,
        MetatheoryExitCriterionKind::EqualityRewriteBoundary,
        MetatheoryExitCriterionKind::RewriteNormalizationBoundary,
        MetatheoryExitCriterionKind::InductionBoundary,
        MetatheoryExitCriterionKind::ModuleBoundary,
        MetatheoryExitCriterionKind::AxiomAccounting,
        MetatheoryExitCriterionKind::DependencyAccounting,
        MetatheoryExitCriterionKind::ClosureAccounting,
        MetatheoryExitCriterionKind::TheoremDependencyInventory,
        MetatheoryExitCriterionKind::SoundnessBoundaryLedger,
        MetatheoryExitCriterionKind::TrustedBaseClosure,
        MetatheoryExitCriterionKind::RegressionCoverage,
    ]
}

fn subject_from_passport(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    let subject = match &passport.ty {
        TypeKind::MetaLevel { level } => format!("M{level}"),
        TypeKind::Provable { object_theory, proposition } => format!("{object_theory}.{proposition}"),
        TypeKind::ConsistencyClaim { theory } => theory.clone(),
        TypeKind::ReflectionClaim { object_theory, proposition } => format!("{object_theory}.{proposition}"),
        TypeKind::SelfReferenceClaim { proposition } => proposition.clone(),
        TypeKind::Statement { proposition }
        | TypeKind::Goal { proposition }
        | TypeKind::Hypothesis { proposition } => proposition.clone(),
        TypeKind::Theorem { name, proposition } => format!("{name}:{proposition}"),
        TypeKind::EqProof { lhs, rhs } => format!("{lhs}={rhs}"),
        TypeKind::RewriteRule { name, lhs, rhs } => format!("{name}:{lhs}->{rhs}"),
        TypeKind::RewriteCertificate { from, to } => format!("{from}->{to}"),
        TypeKind::NatInductionScheme { proposition_family } => proposition_family.clone(),
        TypeKind::InductionProof { proposition }
        | TypeKind::InductionBaseCase { proposition }
        | TypeKind::InductionStepCase { proposition } => proposition.clone(),
        TypeKind::ModuleManifest { module }
        | TypeKind::ModuleInterface { module } => module.clone(),
        TypeKind::ImportGraph { root } => root.clone(),
        TypeKind::ModuleImportAudit { importer, provider, status } => format!("{importer}->{provider}:{status}"),
        TypeKind::AxiomRegistry { theory } => theory.clone(),
        TypeKind::MetatheoryDependencyAudit { subject, .. }
        | TypeKind::MetatheoryClosureReport { subject, .. }
        | TypeKind::GlobalMetatheoryInventory { subject, .. }
        | TypeKind::SoundnessBoundaryLedger { subject, .. }
        | TypeKind::TrustedBaseClosure { subject, .. }
        | TypeKind::MetatheoryFoundationExit { subject, .. } => subject.clone(),
        TypeKind::ConservativeExtensionAudit { base, extension, .. } => format!("{base}->{extension}"),
        _ => {
            return Err(metatheory_foundation_error(
                line,
                format!("passport `{}` is not metatheory-foundation exit evidence", passport.ty),
                "exit evidence must come from an explicit metatheory/passport/audit boundary artifact",
            ));
        }
    };
    require_non_empty(subject, "metatheory exit passport subject", line)
}

fn status_from_passport(passport: &Passport) -> MetatheoryExitCriterionStatus {
    let text_status = match &passport.ty {
        TypeKind::MetatheoryDependencyAudit { status, .. }
        | TypeKind::MetatheoryClosureReport { status, .. }
        | TypeKind::ConservativeExtensionAudit { status, .. }
        | TypeKind::GlobalMetatheoryInventory { status, .. }
        | TypeKind::SoundnessBoundaryLedger { status, .. }
        | TypeKind::TrustedBaseClosure { status, .. }
        | TypeKind::MetatheoryFoundationExit { status, .. } => Some(status.as_str()),
        _ => None,
    };
    match text_status {
        Some("ready") | Some("closed") | Some("verified") | Some("satisfied") => MetatheoryExitCriterionStatus::Satisfied,
        Some("open") | Some("incomplete") => MetatheoryExitCriterionStatus::Open,
        Some(_) => MetatheoryExitCriterionStatus::Failed,
        None => {
            if passport.validation == ValidationState::StaticChecked {
                MetatheoryExitCriterionStatus::Satisfied
            } else {
                MetatheoryExitCriterionStatus::Open
            }
        }
    }
}

fn validation_from_foundation_status(status: MetatheoryFoundationStatus) -> ValidationState {
    match status {
        MetatheoryFoundationStatus::Ready => ValidationState::StaticChecked,
        MetatheoryFoundationStatus::Incomplete => ValidationState::ConstraintChecked,
        MetatheoryFoundationStatus::Rejected => ValidationState::Raw,
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

fn metatheory_foundation_capabilities() -> CapabilitySet {
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

fn compute_exit_fingerprint(
    subject: &str,
    status: MetatheoryFoundationStatus,
    criteria: &[MetatheoryExitCriterion],
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "metatheory-foundation-exit:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        format!("{:?}", max_trust),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    for item in criteria {
        parts.push(format!(
            "criterion:{}:{}:{}:{}:{:?}",
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
    format!("dlm-metatheory-foundation-exit-v1-{hash:016x}")
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(metatheory_foundation_error(
            line,
            format!("{label} must not be empty"),
            "metatheory foundation exit evidence requires stable non-empty identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn metatheory_foundation_error(
    line: usize,
    message: impl Into<String>,
    help: impl Into<String>,
) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::MetatheoryFoundationError, Some(line), message).with_help(help)
}
