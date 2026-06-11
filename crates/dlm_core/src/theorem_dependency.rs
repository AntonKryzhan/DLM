use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

use crate::conservative_extension::{ConservativeExtensionAuditReport, ConservativeExtensionStatus};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TheoremDependencyNodeKind {
    Theorem,
    DependencyAudit,
    ClosureReport,
    ConservativeExtensionAudit,
    ModuleInterface,
    ModuleImportAudit,
    ProofCertificate,
    RewriteCertificate,
    SoundnessBoundaryLedger,
    Unknown,
}

impl fmt::Display for TheoremDependencyNodeKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TheoremDependencyNodeKind::Theorem => write!(f, "theorem"),
            TheoremDependencyNodeKind::DependencyAudit => write!(f, "dependency_audit"),
            TheoremDependencyNodeKind::ClosureReport => write!(f, "closure_report"),
            TheoremDependencyNodeKind::ConservativeExtensionAudit => write!(f, "conservative_extension_audit"),
            TheoremDependencyNodeKind::ModuleInterface => write!(f, "module_interface"),
            TheoremDependencyNodeKind::ModuleImportAudit => write!(f, "module_import_audit"),
            TheoremDependencyNodeKind::ProofCertificate => write!(f, "proof_certificate"),
            TheoremDependencyNodeKind::RewriteCertificate => write!(f, "rewrite_certificate"),
            TheoremDependencyNodeKind::SoundnessBoundaryLedger => write!(f, "soundness_boundary_ledger"),
            TheoremDependencyNodeKind::Unknown => write!(f, "unknown"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MetatheoryInventoryStatus {
    Verified,
    Open,
    Rejected,
}

impl fmt::Display for MetatheoryInventoryStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetatheoryInventoryStatus::Verified => write!(f, "verified"),
            MetatheoryInventoryStatus::Open => write!(f, "open"),
            MetatheoryInventoryStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TheoremDependencyNode {
    pub id: String,
    pub kind: TheoremDependencyNodeKind,
    pub ty: String,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub theory: String,
    pub history: Vec<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TheoremDependencyEdge {
    pub from: String,
    pub to: String,
    pub label: String,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct GlobalMetatheoryInventoryReport {
    pub subject: String,
    pub status: MetatheoryInventoryStatus,
    pub nodes: Vec<TheoremDependencyNode>,
    pub edges: Vec<TheoremDependencyEdge>,
    pub conservative_extension_fingerprints: Vec<String>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub inventory_fingerprint: String,
}

pub fn theorem_dependency_node_from_passport(
    id: impl Into<String>,
    kind: TheoremDependencyNodeKind,
    passport: &Passport,
    line: usize,
) -> Result<TheoremDependencyNode, Diagnostic> {
    let id = require_non_empty(id.into(), "dependency node id", line)?;
    validate_node_kind(kind, passport, line)?;

    let ty = passport.ty.to_string();
    let history = passport.history.events().to_vec();
    let mut parts = vec![
        "theorem-dependency-node:v1".to_string(),
        id.clone(),
        kind.to_string(),
        ty.clone(),
        format!("{:?}", passport.trust),
        format!("{:?}", passport.provenance),
        format!("{:?}", passport.validation),
        passport.theory.home.clone(),
    ];
    parts.extend(history.iter().cloned());
    let fingerprint = stable_fingerprint(&parts);

    Ok(TheoremDependencyNode {
        id,
        kind,
        ty,
        trust: passport.trust,
        provenance: passport.provenance,
        validation: passport.validation,
        theory: passport.theory.home.clone(),
        history,
        fingerprint,
    })
}

pub fn theorem_dependency_edge(
    from: impl Into<String>,
    to: impl Into<String>,
    label: impl Into<String>,
    line: usize,
) -> Result<TheoremDependencyEdge, Diagnostic> {
    let from = require_non_empty(from.into(), "dependency edge source", line)?;
    let to = require_non_empty(to.into(), "dependency edge target", line)?;
    let label = require_non_empty(label.into(), "dependency edge label", line)?;
    if from == to {
        return Err(theorem_dependency_error(
            line,
            format!("dependency edge `{from}` -> `{to}` is a self-edge"),
            "theorem dependency graphs must not hide circular justification behind a self-edge",
        ));
    }
    let fingerprint = stable_fingerprint(&[
        "theorem-dependency-edge:v1".to_string(),
        from.clone(),
        to.clone(),
        label.clone(),
    ]);
    Ok(TheoremDependencyEdge { from, to, label, fingerprint })
}

pub fn global_metatheory_inventory(
    subject: impl Into<String>,
    nodes: Vec<TheoremDependencyNode>,
    edges: Vec<TheoremDependencyEdge>,
    conservative_extensions: &[ConservativeExtensionAuditReport],
    line: usize,
) -> GlobalMetatheoryInventoryReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut ids = BTreeSet::new();
    let mut fingerprints = BTreeSet::new();
    let mut edge_fingerprints = BTreeSet::new();
    let mut node_by_id = BTreeMap::new();
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;

    if subject.trim().is_empty() {
        diagnostics.push(theorem_dependency_error(
            line,
            "global metatheory inventory subject must not be empty",
            "global inventory reports need a stable subject name for reproducible audit fingerprints",
        ));
    }

    if nodes.is_empty() {
        diagnostics.push(theorem_dependency_error(
            line,
            "global metatheory inventory has no nodes",
            "at least one theorem, closure, audit, certificate, or interface node is required",
        ));
    }

    for node in &nodes {
        if !ids.insert(node.id.clone()) {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("duplicate theorem dependency node id `{}`", node.id),
                "node identifiers must be unique so graph edges have stable semantics",
            ));
        }
        if !fingerprints.insert(node.fingerprint.clone()) {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("duplicate theorem dependency node fingerprint `{}`", node.fingerprint),
                "duplicate evidence must be represented once or explicitly distinguished by its dependency role",
            ));
        }
        max_trust = max_trust.max(node.trust);
        has_axiom_taint |= node.trust >= TrustLevel::Axiom;
        has_oracle_taint |= node.trust >= TrustLevel::Oracle || node.provenance == Provenance::OracleInput;
        has_unsafe_taint |= node.trust >= TrustLevel::Unsafe || node.provenance == Provenance::UnsafeExternal;
        node_by_id.insert(node.id.clone(), node.kind);
    }

    for edge in &edges {
        if !node_by_id.contains_key(&edge.from) {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("dependency edge source `{}` is not a node", edge.from),
                "every graph edge must point to explicit inventory nodes",
            ));
        }
        if !node_by_id.contains_key(&edge.to) {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("dependency edge target `{}` is not a node", edge.to),
                "theorem dependencies must not point to hidden or display-only evidence",
            ));
        }
        if edge.from == edge.to {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("dependency edge `{}` -> `{}` is a self-edge", edge.from, edge.to),
                "self-dependency is rejected because it would hide circular justification",
            ));
        }
        if !edge_fingerprints.insert(edge.fingerprint.clone()) {
            diagnostics.push(theorem_dependency_error(
                line,
                format!("duplicate dependency edge fingerprint `{}`", edge.fingerprint),
                "duplicate graph edges must be made explicit with distinct labels if they carry different meaning",
            ));
        }
    }

    let mut conservative_extension_fingerprints = Vec::new();
    let mut has_open_conservative_extension = false;
    for audit in conservative_extensions {
        conservative_extension_fingerprints.push(audit.audit_fingerprint.clone());
        max_trust = max_trust.max(audit.max_trust);
        has_axiom_taint |= audit.has_axiom_taint;
        has_oracle_taint |= audit.has_oracle_taint;
        has_unsafe_taint |= audit.has_unsafe_taint;
        match audit.status {
            ConservativeExtensionStatus::Verified => {}
            ConservativeExtensionStatus::Open => has_open_conservative_extension = true,
            ConservativeExtensionStatus::Rejected => diagnostics.push(theorem_dependency_error(
                line,
                format!(
                    "conservative extension audit `{} -> {}` is rejected",
                    audit.base_subject, audit.extension_subject
                ),
                "rejected conservative-extension evidence cannot close a global metatheory inventory",
            )),
        }
    }

    let has_open_node = nodes.iter().any(|node| {
        matches!(node.kind, TheoremDependencyNodeKind::ClosureReport | TheoremDependencyNodeKind::ConservativeExtensionAudit)
            && node.validation == ValidationState::ConstraintChecked
    });

    let status = if !diagnostics.is_empty() {
        MetatheoryInventoryStatus::Rejected
    } else if has_open_node || has_open_conservative_extension {
        MetatheoryInventoryStatus::Open
    } else {
        MetatheoryInventoryStatus::Verified
    };

    let inventory_fingerprint = compute_inventory_fingerprint(
        &subject,
        status,
        &nodes,
        &edges,
        &conservative_extension_fingerprints,
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    GlobalMetatheoryInventoryReport {
        subject,
        status,
        nodes,
        edges,
        conservative_extension_fingerprints,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        inventory_fingerprint,
    }
}

pub fn require_verified_global_metatheory_inventory(
    report: &GlobalMetatheoryInventoryReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == MetatheoryInventoryStatus::Verified {
        Ok(())
    } else {
        Err(theorem_dependency_error(
            line,
            format!("global metatheory inventory `{}` is {}", report.subject, report.status),
            "only verified inventories may serve as a closed global theorem dependency basis",
        ))
    }
}

pub fn global_metatheory_inventory_passport(
    theory: &str,
    report: &GlobalMetatheoryInventoryReport,
) -> Passport {
    let mut histories = Vec::new();
    for node in &report.nodes {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:inventory_node:{}:{}:{}",
            node.kind, node.id, node.fingerprint
        )));
    }
    for edge in &report.edges {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:inventory_edge:{}->{}:{}:{}",
            edge.from, edge.to, edge.label, edge.fingerprint
        )));
    }
    for audit in &report.conservative_extension_fingerprints {
        histories.push(HistoryChain::from_event(format!(
            "metatheory:inventory_conservative_extension:{audit}"
        )));
    }
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "metatheory:global_inventory:{}:{}:fingerprint={}",
            report.subject, report.status, report.inventory_fingerprint
        ),
    );

    Passport {
        ty: TypeKind::GlobalMetatheoryInventory {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: theorem_dependency_capabilities(),
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
        validation: if report.status == MetatheoryInventoryStatus::Verified {
            ValidationState::StaticChecked
        } else if report.status == MetatheoryInventoryStatus::Open {
            ValidationState::ConstraintChecked
        } else {
            ValidationState::Raw
        },
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_global_metatheory_inventory(report: &GlobalMetatheoryInventoryReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Global Metatheory Inventory v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("nodes: {}\n", report.nodes.len()));
    for node in &report.nodes {
        out.push_str(&format!(
            "- node {} kind={} type={} trust={:?} fingerprint={}\n",
            node.id, node.kind, node.ty, node.trust, node.fingerprint
        ));
    }
    out.push_str(&format!("edges: {}\n", report.edges.len()));
    for edge in &report.edges {
        out.push_str(&format!(
            "- edge {} -> {} label={} fingerprint={}\n",
            edge.from, edge.to, edge.label, edge.fingerprint
        ));
    }
    out.push_str(&format!(
        "conservative_extension_audits: {}\n",
        report.conservative_extension_fingerprints.len()
    ));
    for fingerprint in &report.conservative_extension_fingerprints {
        out.push_str(&format!("- {fingerprint}\n"));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out.push_str(&format!("inventory_fingerprint: {}\n", report.inventory_fingerprint));
    out
}

pub fn export_global_metatheory_inventory(report: &GlobalMetatheoryInventoryReport) -> String {
    render_global_metatheory_inventory(report)
}

fn validate_node_kind(
    kind: TheoremDependencyNodeKind,
    passport: &Passport,
    line: usize,
) -> Result<(), Diagnostic> {
    let ok = matches!(
        (kind, &passport.ty),
        (TheoremDependencyNodeKind::Theorem, TypeKind::Theorem { .. })
            | (TheoremDependencyNodeKind::DependencyAudit, TypeKind::MetatheoryDependencyAudit { .. })
            | (TheoremDependencyNodeKind::ClosureReport, TypeKind::MetatheoryClosureReport { .. })
            | (TheoremDependencyNodeKind::ConservativeExtensionAudit, TypeKind::ConservativeExtensionAudit { .. })
            | (TheoremDependencyNodeKind::ModuleInterface, TypeKind::ModuleInterface { .. })
            | (TheoremDependencyNodeKind::ModuleImportAudit, TypeKind::ModuleImportAudit { .. })
            | (TheoremDependencyNodeKind::RewriteCertificate, TypeKind::RewriteCertificate { .. })
            | (TheoremDependencyNodeKind::SoundnessBoundaryLedger, TypeKind::SoundnessBoundaryLedger { .. })
            | (TheoremDependencyNodeKind::Unknown, _)
    );
    if ok {
        Ok(())
    } else {
        Err(theorem_dependency_error(
            line,
            format!("node kind `{kind}` does not match passport type `{}`", passport.ty),
            "theorem dependency graph nodes must not mislabel statements, goals, runtime witnesses, or proof terms as verified graph evidence",
        ))
    }
}

fn compute_inventory_fingerprint(
    subject: &str,
    status: MetatheoryInventoryStatus,
    nodes: &[TheoremDependencyNode],
    edges: &[TheoremDependencyEdge],
    conservative_extension_fingerprints: &[String],
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "global-metatheory-inventory:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        format!("{max_trust:?}"),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    for node in nodes {
        parts.push(format!("node:{}:{}:{}", node.id, node.kind, node.fingerprint));
    }
    for edge in edges {
        parts.push(format!("edge:{}:{}:{}:{}", edge.from, edge.to, edge.label, edge.fingerprint));
    }
    for fingerprint in conservative_extension_fingerprints {
        parts.push(format!("conservative-extension:{fingerprint}"));
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
    format!("dlm-global-metatheory-inventory-v1-{hash:016x}")
}

fn theorem_dependency_capabilities() -> CapabilitySet {
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
        Err(theorem_dependency_error(
            line,
            format!("{label} must not be empty"),
            "theorem dependency inventories require stable non-empty identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn theorem_dependency_error(
    line: usize,
    message: impl Into<String>,
    help: impl Into<String>,
) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::TheoremDependencyError, Some(line), message).with_help(help)
}
