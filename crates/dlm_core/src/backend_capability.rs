use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::prelude_lowering::{PreludeLoweringReport, PreludeLoweringStatus, PreludeLoweringTarget};
use crate::standard_prelude::PreludeOperationKind;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BackendCapability {
    Deterministic,
    Pure,
    NoAlloc,
    NoAlias,
    Vectorizable,
    Batchable,
    GpuResident,
    RemoteSerializable,
    ValuePreserving,
    DescriptorPreserving,
}

impl fmt::Display for BackendCapability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BackendCapability::Deterministic => write!(f, "deterministic"),
            BackendCapability::Pure => write!(f, "pure"),
            BackendCapability::NoAlloc => write!(f, "no_alloc"),
            BackendCapability::NoAlias => write!(f, "no_alias"),
            BackendCapability::Vectorizable => write!(f, "vectorizable"),
            BackendCapability::Batchable => write!(f, "batchable"),
            BackendCapability::GpuResident => write!(f, "gpu_resident"),
            BackendCapability::RemoteSerializable => write!(f, "remote_serializable"),
            BackendCapability::ValuePreserving => write!(f, "value_preserving"),
            BackendCapability::DescriptorPreserving => write!(f, "descriptor_preserving"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BackendCapabilityStatus {
    Verified,
    RejectedCapability,
}

impl fmt::Display for BackendCapabilityStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BackendCapabilityStatus::Verified => write!(f, "verified"),
            BackendCapabilityStatus::RejectedCapability => write!(f, "rejected_capability"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BackendLoweringStatus {
    VerifiedAccepted,
    SymbolicAccepted,
    DowngradedTainted,
    RejectedLowering,
    RejectedTarget,
    RejectedCapability,
}

impl fmt::Display for BackendLoweringStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BackendLoweringStatus::VerifiedAccepted => write!(f, "verified_accepted"),
            BackendLoweringStatus::SymbolicAccepted => write!(f, "symbolic_accepted"),
            BackendLoweringStatus::DowngradedTainted => write!(f, "downgraded_tainted"),
            BackendLoweringStatus::RejectedLowering => write!(f, "rejected_lowering"),
            BackendLoweringStatus::RejectedTarget => write!(f, "rejected_target"),
            BackendLoweringStatus::RejectedCapability => write!(f, "rejected_capability"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BackendCapabilityContract {
    pub name: String,
    pub target: PreludeLoweringTarget,
    pub capabilities: BTreeSet<BackendCapability>,
    pub required_capabilities: BTreeSet<BackendCapability>,
    pub missing_capabilities: Vec<BackendCapability>,
    pub status: BackendCapabilityStatus,
    pub open_obligations: Vec<String>,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BackendLoweringReport {
    pub name: String,
    pub backend: String,
    pub lowering: String,
    pub operation: PreludeOperationKind,
    pub target: PreludeLoweringTarget,
    pub lowering_status: PreludeLoweringStatus,
    pub backend_status: BackendCapabilityStatus,
    pub status: BackendLoweringStatus,
    pub accepted_capabilities: BTreeSet<BackendCapability>,
    pub representation: String,
    pub descriptor: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn required_backend_capabilities(target: PreludeLoweringTarget) -> BTreeSet<BackendCapability> {
    use BackendCapability::*;
    let caps = match target {
        PreludeLoweringTarget::AuditOnly => vec![Deterministic, DescriptorPreserving],
        PreludeLoweringTarget::Interpreter => vec![Deterministic, Pure, ValuePreserving],
        PreludeLoweringTarget::NativeScalar => vec![Deterministic, Pure, NoAlloc, ValuePreserving],
        PreludeLoweringTarget::NativeVector => vec![Deterministic, Pure, NoAlloc, NoAlias, Vectorizable, ValuePreserving],
        PreludeLoweringTarget::GpuBatch => vec![Deterministic, Pure, NoAlloc, NoAlias, Batchable, GpuResident, DescriptorPreserving],
        PreludeLoweringTarget::RemoteBatch => vec![Deterministic, Pure, NoAlloc, Batchable, RemoteSerializable, DescriptorPreserving],
    };
    caps.into_iter().collect()
}

pub fn backend_capability_contract<I>(
    name: impl Into<String>,
    target: PreludeLoweringTarget,
    capabilities: I,
    line: usize,
) -> Result<BackendCapabilityContract, Diagnostic>
where
    I: IntoIterator<Item = BackendCapability>,
{
    let name = name.into();
    validate_identifier(&name, "backend capability contract name", line)?;

    let capabilities: BTreeSet<BackendCapability> = capabilities.into_iter().collect();
    let required_capabilities = required_backend_capabilities(target);
    let missing_capabilities: Vec<BackendCapability> = required_capabilities
        .iter()
        .copied()
        .filter(|cap| !capabilities.contains(cap))
        .collect();

    let mut open_obligations = Vec::new();
    for cap in &missing_capabilities {
        open_obligations.push(format!(
            "backend `{name}` for target `{target}` is missing required capability `{cap}`"
        ));
    }

    let status = if missing_capabilities.is_empty() {
        BackendCapabilityStatus::Verified
    } else {
        BackendCapabilityStatus::RejectedCapability
    };

    let fingerprint = stable_fingerprint(&[
        "backend-capability-contract-v1".to_string(),
        name.clone(),
        target.to_string(),
        render_capabilities(&capabilities),
        render_capabilities(&required_capabilities),
        status.to_string(),
    ]);

    Ok(BackendCapabilityContract {
        name,
        target,
        capabilities,
        required_capabilities,
        missing_capabilities,
        status,
        open_obligations,
        fingerprint,
    })
}

pub fn validate_backend_lowering(
    name: impl Into<String>,
    lowering: &PreludeLoweringReport,
    backend: &BackendCapabilityContract,
    line: usize,
) -> Result<BackendLoweringReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "backend lowering report name", line)?;

    let mut open_obligations = Vec::new();
    open_obligations.extend(lowering.open_obligations.clone());
    open_obligations.extend(backend.open_obligations.clone());

    let mut status = if backend.status != BackendCapabilityStatus::Verified {
        BackendLoweringStatus::RejectedCapability
    } else if backend.target != lowering.target {
        open_obligations.push(format!(
            "backend `{}` targets `{}`, but lowering `{}` targets `{}`",
            backend.name, backend.target, lowering.name, lowering.target
        ));
        BackendLoweringStatus::RejectedTarget
    } else {
        match lowering.status {
            PreludeLoweringStatus::VerifiedErased => BackendLoweringStatus::VerifiedAccepted,
            PreludeLoweringStatus::SymbolicLowered => {
                if target_accepts_symbolic(lowering.target) && backend.capabilities.contains(&BackendCapability::Batchable) {
                    open_obligations.push(format!(
                        "backend `{}` accepts symbolic bounded lowering for operation `{}`; arbitrary user code is still not executed implicitly",
                        backend.name, lowering.operation
                    ));
                    BackendLoweringStatus::SymbolicAccepted
                } else {
                    open_obligations.push(format!(
                        "backend `{}` cannot consume symbolic lowering `{}` without explicit batch capability",
                        backend.name, lowering.name
                    ));
                    BackendLoweringStatus::RejectedLowering
                }
            }
            PreludeLoweringStatus::DowngradedTainted => BackendLoweringStatus::DowngradedTainted,
            PreludeLoweringStatus::RejectedEvaluation
            | PreludeLoweringStatus::RejectedTarget
            | PreludeLoweringStatus::RejectedEvidenceBoundary => BackendLoweringStatus::RejectedLowering,
        }
    };

    if (lowering.has_axiom_taint || lowering.has_oracle_taint || lowering.has_unsafe_taint) && !is_rejected(status) {
        open_obligations.push("backend plan preserves Axiom/Oracle/Unsafe taint and is not a clean verified backend artifact".to_string());
        status = BackendLoweringStatus::DowngradedTainted;
    }

    let representation = backend_representation(lowering.target, lowering.operation, status, &backend.capabilities);
    let descriptor = format!(
        "backend_descriptor<name={},backend={},lowering={},target={},operation={},status={},lower_fp={},backend_fp={}>",
        name, backend.name, lowering.name, lowering.target, lowering.operation, status, lowering.fingerprint, backend.fingerprint
    );

    open_obligations.sort();
    open_obligations.dedup();

    let fingerprint = stable_fingerprint(&[
        "backend-lowering-report-v1".to_string(),
        name.clone(),
        backend.name.clone(),
        lowering.name.clone(),
        lowering.operation.to_string(),
        lowering.target.to_string(),
        lowering.status.to_string(),
        backend.status.to_string(),
        status.to_string(),
        render_capabilities(&backend.capabilities),
        lowering.fingerprint.clone(),
        backend.fingerprint.clone(),
    ]);

    Ok(BackendLoweringReport {
        name,
        backend: backend.name.clone(),
        lowering: lowering.name.clone(),
        operation: lowering.operation,
        target: lowering.target,
        lowering_status: lowering.status,
        backend_status: backend.status,
        status,
        accepted_capabilities: backend.capabilities.clone(),
        representation,
        descriptor,
        open_obligations,
        max_trust: lowering.max_trust,
        max_provenance: lowering.max_provenance,
        has_axiom_taint: lowering.has_axiom_taint,
        has_oracle_taint: lowering.has_oracle_taint,
        has_unsafe_taint: lowering.has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_backend_lowering(report: &BackendLoweringReport, line: usize) -> Result<(), Diagnostic> {
    match report.status {
        BackendLoweringStatus::VerifiedAccepted => Ok(()),
        BackendLoweringStatus::SymbolicAccepted
        | BackendLoweringStatus::DowngradedTainted
        | BackendLoweringStatus::RejectedLowering
        | BackendLoweringStatus::RejectedTarget
        | BackendLoweringStatus::RejectedCapability => Err(backend_capability_error(
            line,
            format!("backend lowering `{}` is {}, not verified_accepted", report.name, report.status),
            "runtime/compiler execution may consume only verified backend lowering reports unless it explicitly accepts symbolic or tainted audit status",
        )),
    }
}

pub fn backend_capability_contract_passport(theory: &str, contract: &BackendCapabilityContract, sources: &[&Passport]) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::BackendCapabilityContract {
            name: contract.name.clone(),
            target: contract.target.to_string(),
            status: contract.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompilePortableCode,
            Capability::CanDeployPortableCode,
            Capability::CanScheduleRuntime,
        ]),
        cost: CostClass::SmallFinite,
        trust: source_trust.max(TrustLevel::Builtin),
        provenance: source_provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(
            sources,
            format!(
                "backend:capability_contract:{}:{}:{}:fingerprint={}",
                contract.name, contract.target, contract.status, contract.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn backend_lowering_report_passport(theory: &str, report: &BackendLoweringReport, sources: &[&Passport]) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::BackendLoweringReport {
            name: report.name.clone(),
            target: report.target.to_string(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompilePortableCode,
            Capability::CanDeployPortableCode,
            Capability::CanScheduleRuntime,
        ]),
        cost: CostClass::SmallFinite,
        trust: report.max_trust.max(source_trust).max(TrustLevel::Builtin),
        provenance: report.max_provenance.max(source_provenance).max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(
            sources,
            format!(
                "backend:lowering_plan:{}:{}:{}:{}:fingerprint={}",
                report.name, report.operation, report.target, report.status, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_backend_capability_contract(contract: &BackendCapabilityContract) -> String {
    let mut out = String::new();
    out.push_str("backend_capability_contract: v1\n");
    out.push_str(&format!("name: {}\n", contract.name));
    out.push_str(&format!("target: {}\n", contract.target));
    out.push_str(&format!("status: {}\n", contract.status));
    out.push_str(&format!("capabilities: {}\n", render_capabilities(&contract.capabilities)));
    out.push_str(&format!("required_capabilities: {}\n", render_capabilities(&contract.required_capabilities)));
    out.push_str("missing_capabilities:\n");
    for cap in &contract.missing_capabilities {
        out.push_str(&format!("  - {}\n", cap));
    }
    out.push_str("open_obligations:\n");
    for obligation in &contract.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("fingerprint: {}\n", contract.fingerprint));
    out
}

pub fn export_backend_lowering_report(report: &BackendLoweringReport) -> String {
    let mut out = String::new();
    out.push_str("backend_lowering_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("backend: {}\n", report.backend));
    out.push_str(&format!("lowering: {}\n", report.lowering));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("target: {}\n", report.target));
    out.push_str(&format!("lowering_status: {}\n", report.lowering_status));
    out.push_str(&format!("backend_status: {}\n", report.backend_status));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("accepted_capabilities: {}\n", render_capabilities(&report.accepted_capabilities)));
    out.push_str(&format!("representation: {}\n", report.representation));
    out.push_str(&format!("descriptor: {}\n", report.descriptor));
    out.push_str("open_obligations:\n");
    for obligation in &report.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("max_provenance: {:?}\n", report.max_provenance));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

fn target_accepts_symbolic(target: PreludeLoweringTarget) -> bool {
    matches!(
        target,
        PreludeLoweringTarget::NativeVector | PreludeLoweringTarget::GpuBatch | PreludeLoweringTarget::RemoteBatch
    )
}

fn backend_representation(
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    status: BackendLoweringStatus,
    capabilities: &BTreeSet<BackendCapability>,
) -> String {
    format!(
        "backend-plan<target={},op={},status={},caps={}>",
        target,
        operation,
        status,
        render_capabilities(capabilities)
    )
}

fn is_rejected(status: BackendLoweringStatus) -> bool {
    matches!(
        status,
        BackendLoweringStatus::RejectedLowering
            | BackendLoweringStatus::RejectedTarget
            | BackendLoweringStatus::RejectedCapability
    )
}

fn render_capabilities(caps: &BTreeSet<BackendCapability>) -> String {
    caps.iter().map(ToString::to_string).collect::<Vec<_>>().join(",")
}

fn source_taint(sources: &[&Passport]) -> (TrustLevel, Provenance) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
    }
    (max_trust, max_provenance)
}

fn merge_history(sources: &[&Passport], event: impl Into<String>) -> HistoryChain {
    if sources.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(sources.iter().map(|source| &source.history), event)
    }
}

fn validate_identifier(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(backend_capability_error(
            line,
            format!("{label} must not be empty"),
            "backend capability artifacts require stable names for audit and cache keys",
        ));
    }
    if value.chars().any(|ch| ch.is_whitespace()) {
        return Err(backend_capability_error(
            line,
            format!("{label} `{value}` must not contain whitespace"),
            "use a stable identifier such as native_scalar_x86_64",
        ));
    }
    Ok(())
}

fn backend_capability_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::BackendCapabilityError, Some(line), message.into()).with_help(help.into())
}

fn stable_fingerprint(parts: &[String]) -> String {
    const FNV_OFFSET: u64 = 0xcbf29ce484222325;
    const FNV_PRIME: u64 = 0x100000001b3;
    let mut hash = FNV_OFFSET;
    for part in parts {
        for byte in part.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(FNV_PRIME);
        }
        hash ^= 0xff;
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    format!("bc{:016x}", hash)
}
