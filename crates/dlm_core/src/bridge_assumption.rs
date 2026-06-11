use std::collections::BTreeSet;
use std::fmt;

use crate::bridge::BridgeProfile;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::theorem_dependency::{GlobalMetatheoryInventoryReport, MetatheoryInventoryStatus};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BoundaryAssumptionKind {
    SoundnessBridge,
    ReflectionBridge,
    ConsistencyAssumption,
    TruthLift,
    ConservativeExtension,
    UnsafeBridge,
    OracleDependency,
    AxiomDependency,
    Unknown,
}

impl fmt::Display for BoundaryAssumptionKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BoundaryAssumptionKind::SoundnessBridge => write!(f, "soundness_bridge"),
            BoundaryAssumptionKind::ReflectionBridge => write!(f, "reflection_bridge"),
            BoundaryAssumptionKind::ConsistencyAssumption => write!(f, "consistency_assumption"),
            BoundaryAssumptionKind::TruthLift => write!(f, "truth_lift"),
            BoundaryAssumptionKind::ConservativeExtension => write!(f, "conservative_extension"),
            BoundaryAssumptionKind::UnsafeBridge => write!(f, "unsafe_bridge"),
            BoundaryAssumptionKind::OracleDependency => write!(f, "oracle_dependency"),
            BoundaryAssumptionKind::AxiomDependency => write!(f, "axiom_dependency"),
            BoundaryAssumptionKind::Unknown => write!(f, "unknown"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SoundnessBoundaryStatus {
    Verified,
    Open,
    Rejected,
}

impl fmt::Display for SoundnessBoundaryStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SoundnessBoundaryStatus::Verified => write!(f, "verified"),
            SoundnessBoundaryStatus::Open => write!(f, "open"),
            SoundnessBoundaryStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundaryAssumptionEntry {
    pub id: String,
    pub kind: BoundaryAssumptionKind,
    pub source: String,
    pub target: String,
    pub role: String,
    pub requires_axiom: bool,
    pub preserves_syntax: bool,
    pub preserves_value: bool,
    pub preserves_proof: bool,
    pub preserves_truth: bool,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub history: Vec<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone)]
pub struct SoundnessBoundaryLedgerReport {
    pub subject: String,
    pub status: SoundnessBoundaryStatus,
    pub assumptions: Vec<BoundaryAssumptionEntry>,
    pub global_inventory_fingerprint: Option<String>,
    pub diagnostics: Vec<Diagnostic>,
    pub max_trust: TrustLevel,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub ledger_fingerprint: String,
}

pub fn boundary_assumption_from_bridge_profile(
    id: impl Into<String>,
    profile: &BridgeProfile,
    line: usize,
) -> Result<BoundaryAssumptionEntry, Diagnostic> {
    let id = require_non_empty(id.into(), "boundary assumption id", line)?;
    require_non_empty(profile.name.clone(), "bridge profile name", line)?;
    require_non_empty(profile.source.clone(), "bridge profile source", line)?;
    require_non_empty(profile.target.clone(), "bridge profile target", line)?;

    if !profile.requires_axiom && profile.taint < TrustLevel::Axiom && !profile.is_reflective {
        return Err(soundness_boundary_error(
            line,
            format!(
                "bridge profile `{}` is not a soundness-boundary assumption",
                profile.name
            ),
            "safe builtin bridges should stay in BridgeProfile; the soundness boundary ledger records axiom, reflection, oracle, and unsafe assumptions only",
        ));
    }

    let kind = if profile.kind == "soundness" {
        BoundaryAssumptionKind::SoundnessBridge
    } else if profile.kind == "reflection" || profile.is_reflective {
        BoundaryAssumptionKind::ReflectionBridge
    } else if profile.kind == "unsafe" {
        BoundaryAssumptionKind::UnsafeBridge
    } else if profile.taint >= TrustLevel::Unsafe {
        BoundaryAssumptionKind::UnsafeBridge
    } else if profile.taint >= TrustLevel::Axiom {
        BoundaryAssumptionKind::AxiomDependency
    } else {
        BoundaryAssumptionKind::Unknown
    };

    let provenance = provenance_for_taint(profile.taint);
    let history = vec![
        format!("bridge:name:{}", profile.name),
        format!("bridge:kind:{}", profile.kind),
        format!("bridge:source:{}", profile.source),
        format!("bridge:target:{}", profile.target),
        format!("bridge:requires_axiom:{}", profile.requires_axiom),
        format!("bridge:taint:{:?}", profile.taint),
        format!("bridge:role:{}", profile.role),
    ];
    let fingerprint = stable_fingerprint(&[
        "boundary-assumption:bridge:v1".to_string(),
        id.clone(),
        kind.to_string(),
        profile.name.clone(),
        profile.source.clone(),
        profile.target.clone(),
        profile.kind.clone(),
        profile.requires_axiom.to_string(),
        profile.preserves_syntax.to_string(),
        profile.preserves_value.to_string(),
        profile.preserves_proof.to_string(),
        profile.preserves_truth.to_string(),
        format!("{:?}", profile.taint),
        profile.role.to_string(),
    ]);

    Ok(BoundaryAssumptionEntry {
        id,
        kind,
        source: profile.source.clone(),
        target: profile.target.clone(),
        role: profile.role.to_string(),
        requires_axiom: profile.requires_axiom,
        preserves_syntax: profile.preserves_syntax,
        preserves_value: profile.preserves_value,
        preserves_proof: profile.preserves_proof,
        preserves_truth: profile.preserves_truth,
        trust: profile.taint,
        provenance,
        validation: ValidationState::StaticChecked,
        history,
        fingerprint,
    })
}

pub fn boundary_assumption_from_passport(
    id: impl Into<String>,
    kind: BoundaryAssumptionKind,
    passport: &Passport,
    line: usize,
) -> Result<BoundaryAssumptionEntry, Diagnostic> {
    let id = require_non_empty(id.into(), "boundary assumption id", line)?;
    validate_passport_boundary(kind, passport, line)?;

    let source = passport.theory.home.clone();
    let target = match &passport.ty {
        TypeKind::StaticProof(predicate) => predicate.clone(),
        TypeKind::ConservativeExtensionAudit { base, extension, .. } => format!("{base}->{extension}"),
        TypeKind::MetatheoryDependencyAudit { subject, .. }
        | TypeKind::MetatheoryClosureReport { subject, .. }
        | TypeKind::GlobalMetatheoryInventory { subject, .. }
        | TypeKind::SoundnessBoundaryLedger { subject, .. } => subject.clone(),
        _ => passport.ty.to_string(),
    };
    let role = format!("passport boundary evidence: {}", passport.ty);
    let history = passport.history.events().to_vec();
    let requires_axiom = kind_requires_axiom(kind) || passport.trust >= TrustLevel::Axiom;
    let fingerprint = stable_fingerprint(&[
        "boundary-assumption:passport:v1".to_string(),
        id.clone(),
        kind.to_string(),
        passport.ty.to_string(),
        format!("{:?}", passport.trust),
        format!("{:?}", passport.provenance),
        format!("{:?}", passport.validation),
        passport.theory.home.clone(),
        history.join(" -> "),
    ]);

    Ok(BoundaryAssumptionEntry {
        id,
        kind,
        source,
        target,
        role,
        requires_axiom,
        preserves_syntax: false,
        preserves_value: false,
        preserves_proof: matches!(kind, BoundaryAssumptionKind::ReflectionBridge),
        preserves_truth: matches!(kind, BoundaryAssumptionKind::SoundnessBridge | BoundaryAssumptionKind::TruthLift),
        trust: passport.trust,
        provenance: passport.provenance,
        validation: passport.validation,
        history,
        fingerprint,
    })
}

pub fn soundness_boundary_ledger(
    subject: impl Into<String>,
    assumptions: Vec<BoundaryAssumptionEntry>,
    global_inventory: Option<&GlobalMetatheoryInventoryReport>,
    line: usize,
) -> SoundnessBoundaryLedgerReport {
    let subject = subject.into();
    let mut diagnostics = Vec::new();
    let mut ids = BTreeSet::new();
    let mut fingerprints = BTreeSet::new();
    let mut max_trust = TrustLevel::Checked;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;
    let mut has_open_boundary = false;

    if subject.trim().is_empty() {
        diagnostics.push(soundness_boundary_error(
            line,
            "soundness boundary ledger subject must not be empty",
            "boundary ledgers need a stable subject name for reproducible audit fingerprints",
        ));
    }

    if assumptions.is_empty() {
        diagnostics.push(soundness_boundary_error(
            line,
            "soundness boundary ledger has no explicit assumptions",
            "a ledger without boundary entries would not account for soundness, reflection, consistency, oracle, or unsafe assumptions",
        ));
    }

    for assumption in &assumptions {
        if !ids.insert(assumption.id.clone()) {
            diagnostics.push(soundness_boundary_error(
                line,
                format!("duplicate boundary assumption id `{}`", assumption.id),
                "boundary assumptions must have unique stable ids so each soundness boundary is auditable",
            ));
        }
        if !fingerprints.insert(assumption.fingerprint.clone()) {
            diagnostics.push(soundness_boundary_error(
                line,
                format!("duplicate boundary assumption fingerprint `{}`", assumption.fingerprint),
                "duplicate evidence must be recorded once or made explicit with a distinct boundary role",
            ));
        }
        if assumption.requires_axiom && assumption.trust < TrustLevel::Axiom {
            diagnostics.push(soundness_boundary_error(
                line,
                format!("boundary assumption `{}` requires an axiom but has trust {:?}", assumption.id, assumption.trust),
                "axiom-requiring boundaries must remain Axiom/Oracle/Unsafe-tainted",
            ));
        }
        if assumption.kind == BoundaryAssumptionKind::Unknown {
            has_open_boundary = true;
        }
        max_trust = max_trust.max(assumption.trust);
        has_axiom_taint |= assumption.trust >= TrustLevel::Axiom;
        has_oracle_taint |= assumption.trust >= TrustLevel::Oracle || assumption.provenance == Provenance::OracleInput;
        has_unsafe_taint |= assumption.trust >= TrustLevel::Unsafe || assumption.provenance == Provenance::UnsafeExternal;
    }

    let global_inventory_fingerprint = global_inventory.map(|inventory| inventory.inventory_fingerprint.clone());
    if let Some(inventory) = global_inventory {
        max_trust = max_trust.max(inventory.max_trust);
        has_axiom_taint |= inventory.has_axiom_taint;
        has_oracle_taint |= inventory.has_oracle_taint;
        has_unsafe_taint |= inventory.has_unsafe_taint;
        match inventory.status {
            MetatheoryInventoryStatus::Verified => {}
            MetatheoryInventoryStatus::Open => has_open_boundary = true,
            MetatheoryInventoryStatus::Rejected => diagnostics.push(soundness_boundary_error(
                line,
                format!("global metatheory inventory `{}` is rejected", inventory.subject),
                "a rejected theorem dependency inventory cannot be used as a soundness boundary basis",
            )),
        }
    }

    let status = if !diagnostics.is_empty() {
        SoundnessBoundaryStatus::Rejected
    } else if has_open_boundary {
        SoundnessBoundaryStatus::Open
    } else {
        SoundnessBoundaryStatus::Verified
    };

    let ledger_fingerprint = compute_ledger_fingerprint(
        &subject,
        status,
        &assumptions,
        global_inventory_fingerprint.as_deref(),
        &diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    );

    SoundnessBoundaryLedgerReport {
        subject,
        status,
        assumptions,
        global_inventory_fingerprint,
        diagnostics,
        max_trust,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        ledger_fingerprint,
    }
}

pub fn require_verified_soundness_boundary_ledger(
    report: &SoundnessBoundaryLedgerReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == SoundnessBoundaryStatus::Verified {
        Ok(())
    } else {
        Err(soundness_boundary_error(
            line,
            format!("soundness boundary ledger `{}` is {}", report.subject, report.status),
            "only verified soundness boundary ledgers may serve as a closed global metatheory boundary account",
        ))
    }
}

pub fn soundness_boundary_ledger_passport(
    theory: &str,
    report: &SoundnessBoundaryLedgerReport,
) -> Passport {
    let mut histories = Vec::new();
    for assumption in &report.assumptions {
        histories.push(HistoryChain::from_event(format!(
            "soundness_boundary:assumption:{}:{}:{}",
            assumption.kind, assumption.id, assumption.fingerprint
        )));
    }
    if let Some(fingerprint) = &report.global_inventory_fingerprint {
        histories.push(HistoryChain::from_event(format!(
            "soundness_boundary:global_inventory:{fingerprint}"
        )));
    }
    let history = HistoryChain::merge_many(
        histories.iter(),
        format!(
            "soundness_boundary:ledger:{}:{}:fingerprint={}",
            report.subject, report.status, report.ledger_fingerprint
        ),
    );

    Passport {
        ty: TypeKind::SoundnessBoundaryLedger {
            subject: report.subject.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: soundness_boundary_capabilities(),
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
        validation: if report.status == SoundnessBoundaryStatus::Verified {
            ValidationState::StaticChecked
        } else if report.status == SoundnessBoundaryStatus::Open {
            ValidationState::ConstraintChecked
        } else {
            ValidationState::Raw
        },
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn render_soundness_boundary_ledger(report: &SoundnessBoundaryLedgerReport) -> String {
    let mut out = String::new();
    out.push_str("DLM Soundness Boundary Ledger v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    match &report.global_inventory_fingerprint {
        Some(fingerprint) => out.push_str(&format!("global_inventory_fingerprint: {fingerprint}\n")),
        None => out.push_str("global_inventory_fingerprint: <none>\n"),
    }
    out.push_str(&format!("assumptions: {}\n", report.assumptions.len()));
    for assumption in &report.assumptions {
        out.push_str(&format!(
            "- {} kind={} source={} target={} trust={:?} requires_axiom={} fingerprint={}\n",
            assumption.id,
            assumption.kind,
            assumption.source,
            assumption.target,
            assumption.trust,
            assumption.requires_axiom,
            assumption.fingerprint
        ));
    }
    if !report.diagnostics.is_empty() {
        out.push_str("diagnostics:\n");
        for diagnostic in &report.diagnostics {
            out.push_str(&format!("- {:?}: {}\n", diagnostic.kind, diagnostic.message));
        }
    }
    out.push_str(&format!("ledger_fingerprint: {}\n", report.ledger_fingerprint));
    out
}

pub fn export_soundness_boundary_ledger(report: &SoundnessBoundaryLedgerReport) -> String {
    render_soundness_boundary_ledger(report)
}

fn validate_passport_boundary(
    kind: BoundaryAssumptionKind,
    passport: &Passport,
    line: usize,
) -> Result<(), Diagnostic> {
    match kind {
        BoundaryAssumptionKind::ConsistencyAssumption => match &passport.ty {
            TypeKind::StaticProof(predicate) if predicate.starts_with("consistency_axiom:") => Ok(()),
            _ => Err(soundness_boundary_error(
                line,
                format!("passport `{}` is not a consistency axiom proof", passport.ty),
                "consistency boundary entries must come from explicit consistency_axiom evidence",
            )),
        },
        BoundaryAssumptionKind::TruthLift => match &passport.ty {
            TypeKind::StaticProof(predicate) if predicate.starts_with("truth_from_provable:") => Ok(()),
            _ => Err(soundness_boundary_error(
                line,
                format!("passport `{}` is not a truth lift proof", passport.ty),
                "truth boundary entries must come from explicit truth_from_provable axiom evidence",
            )),
        },
        BoundaryAssumptionKind::ReflectionBridge => match &passport.ty {
            TypeKind::StaticProof(predicate)
                if predicate.starts_with("reflection_axiom:")
                    || predicate.starts_with("self_reference_axiom:") => Ok(()),
            _ => Err(soundness_boundary_error(
                line,
                format!("passport `{}` is not a reflection/self-reference axiom proof", passport.ty),
                "reflection boundary entries must come from explicit axiom_reflection or axiom_self_reference evidence",
            )),
        },
        BoundaryAssumptionKind::ConservativeExtension => match &passport.ty {
            TypeKind::ConservativeExtensionAudit { .. } => Ok(()),
            _ => Err(soundness_boundary_error(
                line,
                format!("passport `{}` is not a conservative extension audit", passport.ty),
                "conservative extension boundary entries must point at explicit conservative-extension audit passports",
            )),
        },
        BoundaryAssumptionKind::OracleDependency => {
            if passport.trust >= TrustLevel::Oracle || passport.provenance == Provenance::OracleInput {
                Ok(())
            } else {
                Err(soundness_boundary_error(
                    line,
                    format!("passport `{}` is not oracle-tainted", passport.ty),
                    "oracle dependency entries must preserve Oracle trust/provenance taint",
                ))
            }
        }
        BoundaryAssumptionKind::UnsafeBridge => {
            if passport.trust >= TrustLevel::Unsafe || passport.provenance == Provenance::UnsafeExternal {
                Ok(())
            } else {
                Err(soundness_boundary_error(
                    line,
                    format!("passport `{}` is not unsafe-tainted", passport.ty),
                    "unsafe boundary entries must preserve Unsafe trust/provenance taint",
                ))
            }
        }
        BoundaryAssumptionKind::AxiomDependency | BoundaryAssumptionKind::Unknown => {
            if passport.trust >= TrustLevel::Axiom {
                Ok(())
            } else {
                Err(soundness_boundary_error(
                    line,
                    format!("passport `{}` has trust {:?}, below Axiom", passport.ty, passport.trust),
                    "generic boundary entries must be explicitly axiom/oracle/unsafe-tainted",
                ))
            }
        }
        BoundaryAssumptionKind::SoundnessBridge => match &passport.ty {
            TypeKind::StaticProof(predicate) if predicate.starts_with("truth_from_provable:") => Ok(()),
            _ => Err(soundness_boundary_error(
                line,
                format!("passport `{}` is not a soundness/truth bridge proof", passport.ty),
                "soundness boundary entries from passports must be explicit axiom truth lifts",
            )),
        },
    }
}

fn kind_requires_axiom(kind: BoundaryAssumptionKind) -> bool {
    matches!(
        kind,
        BoundaryAssumptionKind::SoundnessBridge
            | BoundaryAssumptionKind::ReflectionBridge
            | BoundaryAssumptionKind::ConsistencyAssumption
            | BoundaryAssumptionKind::TruthLift
            | BoundaryAssumptionKind::UnsafeBridge
            | BoundaryAssumptionKind::AxiomDependency
            | BoundaryAssumptionKind::Unknown
    )
}

fn compute_ledger_fingerprint(
    subject: &str,
    status: SoundnessBoundaryStatus,
    assumptions: &[BoundaryAssumptionEntry],
    global_inventory_fingerprint: Option<&str>,
    diagnostics: &[Diagnostic],
    max_trust: TrustLevel,
    has_axiom_taint: bool,
    has_oracle_taint: bool,
    has_unsafe_taint: bool,
) -> String {
    let mut parts = vec![
        "soundness-boundary-ledger:v1".to_string(),
        subject.to_string(),
        status.to_string(),
        format!("{max_trust:?}"),
        has_axiom_taint.to_string(),
        has_oracle_taint.to_string(),
        has_unsafe_taint.to_string(),
    ];
    if let Some(fingerprint) = global_inventory_fingerprint {
        parts.push(format!("global-inventory:{fingerprint}"));
    }
    for assumption in assumptions {
        parts.push(format!(
            "assumption:{}:{}:{}:{:?}",
            assumption.id, assumption.kind, assumption.fingerprint, assumption.trust
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
    format!("dlm-soundness-boundary-ledger-v1-{hash:016x}")
}

fn provenance_for_taint(trust: TrustLevel) -> Provenance {
    if trust >= TrustLevel::Unsafe {
        Provenance::UnsafeExternal
    } else if trust >= TrustLevel::Oracle {
        Provenance::OracleInput
    } else if trust >= TrustLevel::Axiom {
        Provenance::BuiltinKnown
    } else {
        Provenance::InternalDerived
    }
}

fn soundness_boundary_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanInspectAst,
        Capability::CanMetaLevelReason,
        Capability::CanPropositionReason,
        Capability::CanTruthBoundaryReason,
        Capability::CanConsistencyReason,
    ])
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(soundness_boundary_error(
            line,
            format!("{label} must not be empty"),
            "soundness boundary ledgers require stable non-empty identifiers",
        ))
    } else {
        Ok(value)
    }
}

fn soundness_boundary_error(
    line: usize,
    message: impl Into<String>,
    help: impl Into<String>,
) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::SoundnessBoundaryError, Some(line), message).with_help(help)
}
