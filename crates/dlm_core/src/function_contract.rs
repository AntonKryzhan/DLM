use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum FunctionPurity {
    Pure,
    Effectful,
}

impl fmt::Display for FunctionPurity {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FunctionPurity::Pure => write!(f, "pure"),
            FunctionPurity::Effectful => write!(f, "effectful"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum FunctionTotality {
    Total,
    Partial,
    UnknownWithinBudget,
}

impl fmt::Display for FunctionTotality {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FunctionTotality::Total => write!(f, "total"),
            FunctionTotality::Partial => write!(f, "partial"),
            FunctionTotality::UnknownWithinBudget => write!(f, "unknown_within_budget"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum FunctionContractStatus {
    Verified,
    Downgraded,
    Open,
    Rejected,
}

impl fmt::Display for FunctionContractStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FunctionContractStatus::Verified => write!(f, "verified"),
            FunctionContractStatus::Open => write!(f, "open"),
            FunctionContractStatus::Downgraded => write!(f, "downgraded"),
            FunctionContractStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum FunctionEffectKind {
    Runtime,
    Io,
    Network,
    Filesystem,
    Clock,
    Randomness,
    Oracle,
    UnsafeExternal,
    GpuExecution,
    RemoteExecution,
}

impl fmt::Display for FunctionEffectKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FunctionEffectKind::Runtime => write!(f, "runtime"),
            FunctionEffectKind::Io => write!(f, "io"),
            FunctionEffectKind::Network => write!(f, "network"),
            FunctionEffectKind::Filesystem => write!(f, "filesystem"),
            FunctionEffectKind::Clock => write!(f, "clock"),
            FunctionEffectKind::Randomness => write!(f, "randomness"),
            FunctionEffectKind::Oracle => write!(f, "oracle"),
            FunctionEffectKind::UnsafeExternal => write!(f, "unsafe_external"),
            FunctionEffectKind::GpuExecution => write!(f, "gpu_execution"),
            FunctionEffectKind::RemoteExecution => write!(f, "remote_execution"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionEffect {
    pub kind: FunctionEffectKind,
    pub boundary: String,
}

impl fmt::Display for FunctionEffect {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}@{}", self.kind, self.boundary)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionContractReport {
    pub name: String,
    pub function: String,
    pub domain: String,
    pub codomain: String,
    pub purity: FunctionPurity,
    pub totality: FunctionTotality,
    pub effects: Vec<FunctionEffect>,
    pub status: FunctionContractStatus,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn function_effect(
    kind: FunctionEffectKind,
    boundary: impl Into<String>,
    line: usize,
) -> Result<FunctionEffect, Diagnostic> {
    let boundary = boundary.into();
    validate_text(&boundary, "effect boundary", line)?;
    Ok(FunctionEffect { kind, boundary })
}

pub fn function_contract(
    name: impl Into<String>,
    function: &Passport,
    purity: FunctionPurity,
    totality: FunctionTotality,
    effects: Vec<FunctionEffect>,
    evidence: &[&Passport],
    line: usize,
) -> Result<FunctionContractReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "function contract name", line)?;
    let descriptor = contract_subject_from_passport(function, line)?;
    let has_totality_evidence = evidence.iter().any(|p| is_static_totality_evidence(p));
    let mut open_obligations = Vec::new();

    let mut status = FunctionContractStatus::Verified;
    if purity == FunctionPurity::Pure && !effects.is_empty() {
        status = FunctionContractStatus::Rejected;
        open_obligations.push("pure contract carries explicit effects; use FunctionPurity::Effectful or remove the effects".to_string());
    }
    if purity == FunctionPurity::Effectful && effects.is_empty() {
        status = status.max(FunctionContractStatus::Open);
        open_obligations.push("effectful contract must name at least one explicit effect boundary".to_string());
    }
    if totality == FunctionTotality::Total && !has_totality_evidence {
        status = status.max(FunctionContractStatus::Open);
        open_obligations.push("totality is claimed but no StaticProof/Theorem evidence was supplied".to_string());
    }
    if totality != FunctionTotality::Total {
        open_obligations.push(format!("function is explicitly {totality}; maximum total guarantee is downgraded"));
    }
    if purity == FunctionPurity::Effectful {
        open_obligations.push("function is explicitly effectful; pure-core optimizer must not treat it as deterministic pure code".to_string());
    }

    let mut all_sources = vec![function];
    all_sources.extend_from_slice(evidence);
    let (mut max_trust, mut max_provenance, mut has_axiom_taint, mut has_oracle_taint, mut has_unsafe_taint) = taint_summary(&all_sources);
    for effect in &effects {
        match effect.kind {
            FunctionEffectKind::Oracle => {
                max_trust = max_trust.max(TrustLevel::Oracle);
                max_provenance = max_provenance.max(Provenance::OracleInput);
                has_oracle_taint = true;
                open_obligations.push("oracle effect is visible in the contract and cannot be erased as Checked".to_string());
            }
            FunctionEffectKind::UnsafeExternal => {
                max_trust = max_trust.max(TrustLevel::Unsafe);
                max_provenance = max_provenance.max(Provenance::UnsafeExternal);
                has_unsafe_taint = true;
                open_obligations.push("unsafe external effect is visible in the contract and forces lower assurance".to_string());
            }
            FunctionEffectKind::Runtime
            | FunctionEffectKind::Io
            | FunctionEffectKind::Network
            | FunctionEffectKind::Filesystem
            | FunctionEffectKind::Clock
            | FunctionEffectKind::Randomness
            | FunctionEffectKind::GpuExecution
            | FunctionEffectKind::RemoteExecution => {
                // Explicit boundary is enough for this MVP. The status is still downgraded below.
            }
        }
    }
    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        open_obligations.push("contract depends on Axiom/Oracle/Unsafe taint and is not a clean Checked contract".to_string());
    }
    if status != FunctionContractStatus::Rejected
        && (purity == FunctionPurity::Effectful
            || totality != FunctionTotality::Total
            || has_axiom_taint
            || has_oracle_taint
            || has_unsafe_taint)
    {
        status = status.max(FunctionContractStatus::Downgraded);
    }
    open_obligations.sort();
    open_obligations.dedup();

    let effect_parts: Vec<String> = effects.iter().map(|e| e.to_string()).collect();
    let fingerprint = stable_fingerprint(&[
        "function-contract-v1".to_string(),
        name.clone(),
        descriptor.function.clone(),
        descriptor.domain.clone(),
        descriptor.codomain.clone(),
        purity.to_string(),
        totality.to_string(),
        format!("effects={effect_parts:?}"),
        status.to_string(),
        format!("trust={max_trust:?}"),
    ]);

    Ok(FunctionContractReport {
        name,
        function: descriptor.function,
        domain: descriptor.domain,
        codomain: descriptor.codomain,
        purity,
        totality,
        effects,
        status,
        open_obligations,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_function_contract(report: &FunctionContractReport, line: usize) -> Result<(), Diagnostic> {
    if report.status == FunctionContractStatus::Verified {
        Ok(())
    } else {
        Err(contract_error(
            line,
            format!("function contract `{}` is {}, not verified", report.name, report.status),
            "only verified pure/total contracts may feed verified optimization or certified build assumptions",
        ))
    }
}

pub fn function_contract_passport(theory: &str, report: &FunctionContractReport, sources: &[&Passport]) -> Passport {
    let mut all_sources: Vec<&Passport> = Vec::new();
    all_sources.extend_from_slice(sources);
    let history = merge_history(
        &all_sources,
        format!(
            "function:contract:{}:{}:purity={}:totality={}:fingerprint={}",
            report.name, report.status, report.purity, report.totality, report.fingerprint
        ),
    );
    Passport {
        ty: TypeKind::FunctionContract {
            name: report.name.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
            Capability::CanCompareByProof,
        ]),
        cost: CostClass::ProofRequired,
        trust: report.max_trust.max(TrustLevel::Builtin),
        provenance: report.max_provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn export_function_contract(report: &FunctionContractReport) -> String {
    let mut out = String::new();
    out.push_str("function_contract_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("function: {}\n", report.function));
    out.push_str(&format!("domain: {}\n", report.domain));
    out.push_str(&format!("codomain: {}\n", report.codomain));
    out.push_str(&format!("purity: {}\n", report.purity));
    out.push_str(&format!("totality: {}\n", report.totality));
    out.push_str("effects:\n");
    for effect in &report.effects {
        out.push_str(&format!("  - {}\n", effect));
    }
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str("open_obligations:\n");
    for obligation in &report.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ContractSubjectDescriptor {
    function: String,
    domain: String,
    codomain: String,
}

fn contract_subject_from_passport(passport: &Passport, line: usize) -> Result<ContractSubjectDescriptor, Diagnostic> {
    match &passport.ty {
        TypeKind::FunctionType { domain, codomain } => Ok(ContractSubjectDescriptor {
            function: format!("fn:{}->{}", domain, codomain),
            domain: domain.clone(),
            codomain: codomain.clone(),
        }),
        TypeKind::LambdaTerm { parameter, domain, body } => Ok(ContractSubjectDescriptor {
            function: format!("lambda:{parameter}:{domain}. {body}"),
            domain: domain.clone(),
            codomain: "Unknown".to_string(),
        }),
        TypeKind::ApplicationTerm { .. }
        | TypeKind::FunctionContract { .. }
        | TypeKind::Theorem { .. }
        | TypeKind::Statement { .. }
        | TypeKind::Goal { .. }
        | TypeKind::Hypothesis { .. }
        | TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. } => Err(contract_error(
            line,
            format!("{} cannot be used as a function contract subject", passport.ty),
            "function contracts must attach to ordinary FunctionType/LambdaTerm objects, not theorem/proof/truth/runtime evidence",
        )),
        _ => Err(contract_error(
            line,
            format!("{} is not a function subject", passport.ty),
            "create FunctionType or LambdaTerm before attaching a function contract",
        )),
    }
}

fn is_static_totality_evidence(passport: &Passport) -> bool {
    matches!(&passport.ty, TypeKind::StaticProof(_) | TypeKind::Theorem { .. })
        && passport.validation == ValidationState::StaticChecked
}

fn taint_summary(sources: &[&Passport]) -> (TrustLevel, Provenance, bool, bool, bool) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalLiteral;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
        has_axiom_taint |= source.trust >= TrustLevel::Axiom;
        has_oracle_taint |= source.trust >= TrustLevel::Oracle || source.provenance == Provenance::OracleInput;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe || source.provenance == Provenance::UnsafeExternal;
    }
    (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint)
}

fn merge_history(sources: &[&Passport], event: impl Into<String>) -> HistoryChain {
    if sources.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(sources.iter().map(|source| &source.history), event)
    }
}

fn validate_identifier(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return Err(contract_error(line, format!("{label} is empty"), "function contract names must be explicit identifiers"));
    };
    if !(first.is_ascii_alphabetic() || first == '_') || !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(contract_error(line, format!("invalid {label} `{text}`"), "identifiers must use MVP ASCII identifier syntax"));
    }
    Ok(())
}

fn validate_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(contract_error(line, format!("{label} is empty"), "effect boundaries must be explicit audit keys"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(contract_error(line, format!("{label} contains a newline"), "effect boundary identities must remain stable single-line audit keys"));
    }
    Ok(())
}

fn stable_fingerprint(parts: &[String]) -> String {
    let mut h: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            h ^= u64::from(*byte);
            h = h.wrapping_mul(0x100000001b3);
        }
        h ^= 0xff;
        h = h.wrapping_mul(0x100000001b3);
    }
    format!("fnv64:{h:016x}")
}

fn contract_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::FunctionContractError, Some(line), message).with_help(help)
}
