use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum AxiomKind {
    Logical,
    Mathematical,
    Soundness,
    Reflection,
    Consistency,
    Domain,
    UnsafeExternal,
}

impl fmt::Display for AxiomKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AxiomKind::Logical => write!(f, "logical"),
            AxiomKind::Mathematical => write!(f, "mathematical"),
            AxiomKind::Soundness => write!(f, "soundness"),
            AxiomKind::Reflection => write!(f, "reflection"),
            AxiomKind::Consistency => write!(f, "consistency"),
            AxiomKind::Domain => write!(f, "domain"),
            AxiomKind::UnsafeExternal => write!(f, "unsafe_external"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum DependencyUseKind {
    Axiom,
    Theorem,
    ProofCertificate,
    RewriteRule,
    ModuleInterface,
    ImportAudit,
    RuntimeWitness,
    Unknown,
}

impl fmt::Display for DependencyUseKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DependencyUseKind::Axiom => write!(f, "axiom"),
            DependencyUseKind::Theorem => write!(f, "theorem"),
            DependencyUseKind::ProofCertificate => write!(f, "proof_certificate"),
            DependencyUseKind::RewriteRule => write!(f, "rewrite_rule"),
            DependencyUseKind::ModuleInterface => write!(f, "module_interface"),
            DependencyUseKind::ImportAudit => write!(f, "import_audit"),
            DependencyUseKind::RuntimeWitness => write!(f, "runtime_witness"),
            DependencyUseKind::Unknown => write!(f, "unknown"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum DependencyAuditStatus {
    Verified,
    Rejected,
}

impl fmt::Display for DependencyAuditStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DependencyAuditStatus::Verified => write!(f, "verified"),
            DependencyAuditStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AxiomDecl {
    pub theory: String,
    pub name: String,
    pub proposition: String,
    pub kind: AxiomKind,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub reason: String,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AxiomRegistry {
    pub theory: String,
    pub axioms: Vec<AxiomDecl>,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DependencyEntry {
    pub label: String,
    pub kind: DependencyUseKind,
    pub ty: String,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub history: Vec<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct MetatheoryDependencyAuditReport {
    pub subject: String,
    pub entries: Vec<DependencyEntry>,
    pub diagnostics: Vec<Diagnostic>,
    pub status: DependencyAuditStatus,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub registry_fingerprint: Option<String>,
    pub audit_fingerprint: String,
}

pub fn axiom_decl(
    theory: impl Into<String>,
    name: impl Into<String>,
    proposition: impl Into<String>,
    kind: AxiomKind,
    reason: impl Into<String>,
    line: usize,
) -> Result<AxiomDecl, Diagnostic> {
    let theory = require_non_empty(theory.into(), "axiom theory", line)?;
    let name = require_non_empty(name.into(), "axiom name", line)?;
    let proposition = require_non_empty(proposition.into(), "axiom proposition", line)?;
    let reason = require_non_empty(reason.into(), "axiom reason", line)?;
    let (trust, provenance) = match kind {
        AxiomKind::UnsafeExternal => (TrustLevel::Unsafe, Provenance::UnsafeExternal),
        _ => (TrustLevel::Axiom, Provenance::BuiltinKnown),
    };
    let fingerprint = stable_fingerprint(&[
        "axiom:v1".to_string(),
        theory.clone(),
        name.clone(),
        proposition.clone(),
        kind.to_string(),
        format!("{trust:?}"),
        format!("{provenance:?}"),
        reason.clone(),
    ]);
    Ok(AxiomDecl { theory, name, proposition, kind, trust, provenance, reason, fingerprint })
}

pub fn axiom_registry(
    theory: impl Into<String>,
    axioms: Vec<AxiomDecl>,
    line: usize,
) -> Result<AxiomRegistry, Diagnostic> {
    let theory = require_non_empty(theory.into(), "registry theory", line)?;
    let mut seen = BTreeSet::new();
    let mut sorted = axioms;
    for axiom in &sorted {
        if axiom.theory != theory {
            return Err(meta_dep_error(
                line,
                format!(
                    "axiom `{}` belongs to theory `{}`, not registry theory `{theory}`",
                    axiom.name, axiom.theory
                ),
                "axiom registries are per-theory contracts; cross-theory assumptions need explicit bridges/imports",
            ));
        }
        if !seen.insert(axiom.name.clone()) {
            return Err(meta_dep_error(
                line,
                format!("duplicate axiom `{}` in registry `{theory}`", axiom.name),
                "each axiom name must resolve to one stable declaration and fingerprint",
            ));
        }
    }
    sorted.sort_by(|lhs, rhs| lhs.name.cmp(&rhs.name));
    let mut parts = vec!["axiom-registry:v1".to_string(), theory.clone()];
    for axiom in &sorted {
        parts.push(axiom.fingerprint.clone());
    }
    let fingerprint = stable_fingerprint(&parts);
    Ok(AxiomRegistry { theory, axioms: sorted, fingerprint })
}

pub fn require_declared_axiom(
    registry: &AxiomRegistry,
    name: &str,
    line: usize,
) -> Result<AxiomDecl, Diagnostic> {
    let name = require_non_empty(name.to_string(), "axiom name", line)?;
    registry
        .axioms
        .iter()
        .find(|axiom| axiom.name == name)
        .cloned()
        .ok_or_else(|| meta_dep_error(
            line,
            format!("axiom `{name}` is not declared in registry `{}`", registry.theory),
            "metatheory audits must not use undeclared assumptions or display-only axiom labels",
        ))
}

pub fn dependency_entry_from_passport(
    label: impl Into<String>,
    kind: DependencyUseKind,
    passport: &Passport,
    line: usize,
) -> Result<DependencyEntry, Diagnostic> {
    let label = require_non_empty(label.into(), "dependency label", line)?;
    let history = passport.history.events().to_vec();
    let ty = passport.ty.to_string();
    let mut parts = vec![
        "dependency-entry:v1".to_string(),
        label.clone(),
        kind.to_string(),
        ty.clone(),
        format!("{:?}", passport.trust),
        format!("{:?}", passport.provenance),
        format!("{:?}", passport.validation),
        passport.theory.home.clone(),
    ];
    parts.extend(history.iter().cloned());
    let fingerprint = stable_fingerprint(&parts);
    Ok(DependencyEntry {
        label,
        kind,
        ty,
        trust: passport.trust,
        provenance: passport.provenance,
        validation: passport.validation,
        history,
        fingerprint,
    })
}

pub fn dependency_entry_from_axiom(axiom: &AxiomDecl) -> DependencyEntry {
    DependencyEntry {
        label: axiom.name.clone(),
        kind: DependencyUseKind::Axiom,
        ty: format!("Axiom<{}:{}>", axiom.theory, axiom.proposition),
        trust: axiom.trust,
        provenance: axiom.provenance,
        validation: ValidationState::Assumed,
        history: vec![format!("axiom:declared:{}:{}", axiom.theory, axiom.name)],
        fingerprint: axiom.fingerprint.clone(),
    }
}

pub fn audit_metatheory_dependencies(
    subject: impl Into<String>,
    entries: Vec<DependencyEntry>,
    registry: Option<&AxiomRegistry>,
    line: usize,
) -> MetatheoryDependencyAuditReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut seen = BTreeSet::new();
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;

    if subject.trim().is_empty() {
        diagnostics.push(meta_dep_error(
            line,
            "dependency audit subject must not be empty",
            "audits need stable subject identity before they can be exported or compared",
        ));
    }

    if entries.is_empty() {
        diagnostics.push(meta_dep_error(
            line,
            format!("dependency audit `{subject}` has no entries"),
            "empty dependency audits are not useful as metatheory closure evidence",
        ));
    }

    for entry in &entries {
        if entry.label.trim().is_empty() {
            diagnostics.push(meta_dep_error(
                line,
                "dependency entry label must not be empty",
                "dependency labels are semantic identifiers, not display placeholders",
            ));
        }
        if !seen.insert(entry.fingerprint.clone()) {
            diagnostics.push(meta_dep_error(
                line,
                format!("duplicate dependency fingerprint `{}`", entry.fingerprint),
                "dependency audits preserve multiplicity by label, but repeated identical fingerprints must be intentional and explicit in a later weighted audit layer",
            ));
        }
        max_trust = max_trust.max(entry.trust);
        has_axiom_taint |= entry.trust >= TrustLevel::Axiom;
        has_oracle_taint |= entry.trust >= TrustLevel::Oracle;
        has_unsafe_taint |= entry.trust >= TrustLevel::Unsafe || entry.provenance == Provenance::UnsafeExternal;

        if entry.kind == DependencyUseKind::Axiom {
            match registry {
                Some(registry) if registry.axioms.iter().any(|axiom| axiom.fingerprint == entry.fingerprint) => {}
                Some(registry) => diagnostics.push(meta_dep_error(
                    line,
                    format!(
                        "axiom dependency `{}` is not present in registry `{}`",
                        entry.label, registry.theory
                    ),
                    "axiom-tainted dependencies must be backed by a declared registry entry",
                )),
                None => diagnostics.push(meta_dep_error(
                    line,
                    format!("axiom dependency `{}` has no registry", entry.label),
                    "metatheory closure audits must carry an explicit axiom registry when axioms are used",
                )),
            }
        }
    }

    let status = if diagnostics.is_empty() {
        DependencyAuditStatus::Verified
    } else {
        DependencyAuditStatus::Rejected
    };
    let registry_fingerprint = registry.map(|registry| registry.fingerprint.clone());
    let audit_fingerprint = compute_dependency_audit_fingerprint(
        &subject,
        status,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        registry_fingerprint.as_deref(),
        &entries,
        &diagnostics,
    );

    MetatheoryDependencyAuditReport {
        subject,
        entries,
        diagnostics,
        status,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        registry_fingerprint,
        audit_fingerprint,
    }
}

pub fn require_verified_metatheory_dependency_audit(
    report: &MetatheoryDependencyAuditReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == DependencyAuditStatus::Verified {
        Ok(())
    } else {
        Err(meta_dep_error(
            line,
            format!("metatheory dependency audit `{}` is {}", report.subject, report.status),
            "only verified dependency audits may serve as closure evidence for later proof/kernel layers",
        ))
    }
}

pub fn axiom_registry_passport(theory: &str, registry: &AxiomRegistry) -> Passport {
    Passport {
        ty: TypeKind::AxiomRegistry { theory: registry.theory.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: metatheory_capabilities(),
        cost: CostClass::ProofRequired,
        trust: registry
            .axioms
            .iter()
            .fold(TrustLevel::Checked, |acc, axiom| acc.max(axiom.trust)),
        provenance: if registry.axioms.iter().any(|axiom| axiom.provenance == Provenance::UnsafeExternal) {
            Provenance::UnsafeExternal
        } else {
            Provenance::BuiltinKnown
        },
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!(
            "metatheory:axiom_registry:{}:axioms={}:fingerprint={}",
            registry.theory,
            registry.axioms.len(),
            registry.fingerprint
        )),
        location: LocationContext::local(),
    }
}

pub fn metatheory_dependency_audit_passport(
    theory: &str,
    report: &MetatheoryDependencyAuditReport,
) -> Passport {
    let histories = report
        .entries
        .iter()
        .map(|entry| HistoryChain::from_event(format!("dependency:{}:{}", entry.kind, entry.fingerprint)))
        .collect::<Vec<_>>();
    let mut history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "metatheory:dependency_audit:{}:{}:fingerprint={}",
            report.subject, report.status, report.audit_fingerprint
        ),
    );
    if let Some(registry_fingerprint) = &report.registry_fingerprint {
        history.push(format!("metatheory:registry:fingerprint={registry_fingerprint}"));
    }
    Passport {
        ty: TypeKind::MetatheoryDependencyAudit {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: metatheory_capabilities(),
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
        validation: if report.status == DependencyAuditStatus::Verified {
            ValidationState::StaticChecked
        } else {
            ValidationState::Raw
        },
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn export_axiom_registry_text(registry: &AxiomRegistry) -> String {
    let mut out = String::new();
    out.push_str("DLM Axiom Registry v1\n");
    out.push_str(&format!("theory: {}\n", registry.theory));
    out.push_str(&format!("fingerprint: {}\n", registry.fingerprint));
    out.push_str(&format!("axioms: {}\n", registry.axioms.len()));
    for axiom in &registry.axioms {
        out.push_str(&format!(
            "- {} [{}] trust={:?} provenance={:?} proposition={} fingerprint={} reason={}\n",
            axiom.name,
            axiom.kind,
            axiom.trust,
            axiom.provenance,
            axiom.proposition,
            axiom.fingerprint,
            axiom.reason
        ));
    }
    out
}

pub fn render_metatheory_dependency_audit_report(report: &MetatheoryDependencyAuditReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Metatheory Dependency Audit v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    if let Some(registry_fingerprint) = &report.registry_fingerprint {
        out.push_str(&format!("registry_fingerprint: {registry_fingerprint}\n"));
    }
    out.push_str(&format!("audit_fingerprint: {}\n", report.audit_fingerprint));
    out.push_str(&format!("entries: {}\n", report.entries.len()));
    for entry in &report.entries {
        out.push_str(&format!(
            "- {} kind={} type={} trust={:?} provenance={:?} validation={:?} fingerprint={}\n",
            entry.label,
            entry.kind,
            entry.ty,
            entry.trust,
            entry.provenance,
            entry.validation,
            entry.fingerprint
        ));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out
}

fn compute_dependency_audit_fingerprint(
    subject: &str,
    status: DependencyAuditStatus,
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
    registry_fingerprint: Option<&str>,
    entries: &[DependencyEntry],
    diagnostics: &[Diagnostic],
) -> String {
    let mut parts = vec![
        "dependency-audit:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        format!("{max_trust:?}"),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
        registry_fingerprint.unwrap_or("no-registry").to_string(),
    ];
    for entry in entries {
        parts.push(entry.fingerprint.clone());
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
    format!("dlm-meta-dep-v1-{hash:016x}")
}

fn metatheory_capabilities() -> CapabilitySet {
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
        Err(meta_dep_error(
            line,
            format!("{label} must not be empty"),
            "metatheory dependency objects need stable semantic identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn meta_dep_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::MetatheoryDependencyError, Some(line), message).with_help(help)
}
