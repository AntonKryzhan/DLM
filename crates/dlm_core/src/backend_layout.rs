use std::fmt;

use crate::backend_capability::{BackendCapability, BackendLoweringReport, BackendLoweringStatus};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::prelude_lowering::PreludeLoweringTarget;
use crate::standard_prelude::PreludeOperationKind;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum AbiScalarKind {
    Bool1,
    Nat64,
    TaggedUnion64,
    PointerSized,
    OpaqueSymbolic,
}

impl fmt::Display for AbiScalarKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AbiScalarKind::Bool1 => write!(f, "bool1"),
            AbiScalarKind::Nat64 => write!(f, "nat64"),
            AbiScalarKind::TaggedUnion64 => write!(f, "tagged_union64"),
            AbiScalarKind::PointerSized => write!(f, "pointer_sized"),
            AbiScalarKind::OpaqueSymbolic => write!(f, "opaque_symbolic"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum LayoutContainerKind {
    Scalar,
    TaggedUnion,
    DenseArray,
    SliceView,
    GpuBuffer,
    RemoteBuffer,
    AuditOnlyDescriptor,
}

impl fmt::Display for LayoutContainerKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LayoutContainerKind::Scalar => write!(f, "scalar"),
            LayoutContainerKind::TaggedUnion => write!(f, "tagged_union"),
            LayoutContainerKind::DenseArray => write!(f, "dense_array"),
            LayoutContainerKind::SliceView => write!(f, "slice_view"),
            LayoutContainerKind::GpuBuffer => write!(f, "gpu_buffer"),
            LayoutContainerKind::RemoteBuffer => write!(f, "remote_buffer"),
            LayoutContainerKind::AuditOnlyDescriptor => write!(f, "audit_only_descriptor"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum LayoutMetadataPolicy {
    None,
    CompactDescriptor,
    ErasedWithAuditFingerprint,
    FullPassport,
    InterleavedPerElementPassport,
}

impl fmt::Display for LayoutMetadataPolicy {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LayoutMetadataPolicy::None => write!(f, "none"),
            LayoutMetadataPolicy::CompactDescriptor => write!(f, "compact_descriptor"),
            LayoutMetadataPolicy::ErasedWithAuditFingerprint => write!(f, "erased_with_audit_fingerprint"),
            LayoutMetadataPolicy::FullPassport => write!(f, "full_passport"),
            LayoutMetadataPolicy::InterleavedPerElementPassport => write!(f, "interleaved_per_element_passport"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BackendLayoutStatus {
    VerifiedLayout,
    SymbolicLayout,
    DowngradedTainted,
    RejectedBackend,
    RejectedTarget,
    RejectedAbi,
}

impl fmt::Display for BackendLayoutStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BackendLayoutStatus::VerifiedLayout => write!(f, "verified_layout"),
            BackendLayoutStatus::SymbolicLayout => write!(f, "symbolic_layout"),
            BackendLayoutStatus::DowngradedTainted => write!(f, "downgraded_tainted"),
            BackendLayoutStatus::RejectedBackend => write!(f, "rejected_backend"),
            BackendLayoutStatus::RejectedTarget => write!(f, "rejected_target"),
            BackendLayoutStatus::RejectedAbi => write!(f, "rejected_abi"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BackendLayoutDescriptor {
    pub name: String,
    pub target: PreludeLoweringTarget,
    pub operation: PreludeOperationKind,
    pub scalar: AbiScalarKind,
    pub container: LayoutContainerKind,
    pub element_type: String,
    pub element_size_bytes: usize,
    pub alignment_bytes: usize,
    pub metadata_policy: LayoutMetadataPolicy,
    pub status: BackendLayoutStatus,
    pub open_obligations: Vec<String>,
    pub stable_abi_hash: String,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BackendLayoutReport {
    pub name: String,
    pub backend_plan: String,
    pub layout_descriptor: String,
    pub operation: PreludeOperationKind,
    pub target: PreludeLoweringTarget,
    pub backend_status: BackendLoweringStatus,
    pub layout_status: BackendLayoutStatus,
    pub status: BackendLayoutStatus,
    pub scalar: AbiScalarKind,
    pub container: LayoutContainerKind,
    pub element_type: String,
    pub element_size_bytes: usize,
    pub alignment_bytes: usize,
    pub metadata_policy: LayoutMetadataPolicy,
    pub stable_abi_hash: String,
    pub runtime_descriptor: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn backend_layout_descriptor(
    name: impl Into<String>,
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    scalar: AbiScalarKind,
    container: LayoutContainerKind,
    element_type: impl Into<String>,
    element_size_bytes: usize,
    alignment_bytes: usize,
    metadata_policy: LayoutMetadataPolicy,
    line: usize,
) -> Result<BackendLayoutDescriptor, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "backend layout descriptor name", line)?;
    let element_type = element_type.into();
    validate_identifier(&element_type, "backend layout element type", line)?;

    let mut open_obligations = Vec::new();
    let mut status = BackendLayoutStatus::VerifiedLayout;

    if element_size_bytes == 0 {
        open_obligations.push(format!("layout `{name}` has zero-sized element representation"));
        status = BackendLayoutStatus::RejectedAbi;
    }
    if alignment_bytes == 0 || !alignment_bytes.is_power_of_two() {
        open_obligations.push(format!(
            "layout `{name}` alignment `{alignment_bytes}` is not a positive power-of-two ABI alignment"
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }
    if !container_allowed_for_target(target, container) {
        open_obligations.push(format!(
            "layout `{name}` uses container `{container}` for target `{target}`, which violates the target layout boundary"
        ));
        status = BackendLayoutStatus::RejectedTarget;
    }
    if !container_allowed_for_operation(operation, container) {
        open_obligations.push(format!(
            "layout `{name}` uses container `{container}` for operation `{operation}`, which would change representation class silently"
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }
    if !scalar_allowed_for_operation(operation, scalar) {
        open_obligations.push(format!(
            "layout `{name}` uses scalar ABI `{scalar}` for operation `{operation}` without a checked representation bridge"
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }
    if runtime_forbidden_metadata(target, metadata_policy) {
        open_obligations.push(format!(
            "layout `{name}` carries metadata policy `{metadata_policy}` in runtime/hot layout; use compact descriptor or audit fingerprint instead"
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }

    if status == BackendLayoutStatus::VerifiedLayout && symbolic_operation(operation) {
        open_obligations.push(format!(
            "layout `{name}` is a symbolic bounded layout for `{operation}`; arbitrary user function bodies remain outside the erased runtime artifact"
        ));
        status = BackendLayoutStatus::SymbolicLayout;
    }

    open_obligations.sort();
    open_obligations.dedup();

    let stable_abi_hash = stable_fingerprint(&[
        "backend-layout-abi-v1".to_string(),
        target.to_string(),
        operation.to_string(),
        scalar.to_string(),
        container.to_string(),
        element_type.clone(),
        element_size_bytes.to_string(),
        alignment_bytes.to_string(),
        metadata_policy.to_string(),
    ]);

    let fingerprint = stable_fingerprint(&[
        "backend-layout-descriptor-v1".to_string(),
        name.clone(),
        stable_abi_hash.clone(),
        status.to_string(),
    ]);

    Ok(BackendLayoutDescriptor {
        name,
        target,
        operation,
        scalar,
        container,
        element_type,
        element_size_bytes,
        alignment_bytes,
        metadata_policy,
        status,
        open_obligations,
        stable_abi_hash,
        fingerprint,
    })
}

pub fn validate_backend_layout(
    name: impl Into<String>,
    backend: &BackendLoweringReport,
    descriptor: &BackendLayoutDescriptor,
    line: usize,
) -> Result<BackendLayoutReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "backend layout report name", line)?;

    let mut open_obligations = Vec::new();
    open_obligations.extend(backend.open_obligations.clone());
    open_obligations.extend(descriptor.open_obligations.clone());

    let mut status = if is_backend_rejected(backend.status) {
        open_obligations.push(format!(
            "backend lowering `{}` is {}, so no ABI/layout descriptor may certify it as executable runtime data",
            backend.name, backend.status
        ));
        BackendLayoutStatus::RejectedBackend
    } else if backend.target != descriptor.target {
        open_obligations.push(format!(
            "backend lowering `{}` targets `{}`, but layout `{}` targets `{}`",
            backend.name, backend.target, descriptor.name, descriptor.target
        ));
        BackendLayoutStatus::RejectedTarget
    } else if backend.operation != descriptor.operation {
        open_obligations.push(format!(
            "backend lowering `{}` is for `{}`, but layout `{}` is for `{}`",
            backend.name, backend.operation, descriptor.name, descriptor.operation
        ));
        BackendLayoutStatus::RejectedTarget
    } else if is_layout_rejected(descriptor.status) {
        BackendLayoutStatus::RejectedAbi
    } else if backend.status == BackendLoweringStatus::SymbolicAccepted
        || descriptor.status == BackendLayoutStatus::SymbolicLayout
    {
        open_obligations.push(format!(
            "layout `{}` preserves symbolic bounded backend plan `{}` without pretending it is a fully concrete value layout",
            descriptor.name, backend.name
        ));
        BackendLayoutStatus::SymbolicLayout
    } else {
        BackendLayoutStatus::VerifiedLayout
    };

    if backend.accepted_capabilities.contains(&BackendCapability::NoAlloc)
        && runtime_forbidden_metadata(descriptor.target, descriptor.metadata_policy)
        && !is_layout_rejected(status)
    {
        open_obligations.push(format!(
            "backend `{}` declares no_alloc but layout `{}` still embeds non-compact metadata",
            backend.backend, descriptor.name
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }

    if descriptor.target == PreludeLoweringTarget::GpuBatch
        && !backend.accepted_capabilities.contains(&BackendCapability::GpuResident)
        && !is_layout_rejected(status)
    {
        open_obligations.push(format!(
            "gpu layout `{}` requires a gpu_resident backend plan",
            descriptor.name
        ));
        status = BackendLayoutStatus::RejectedAbi;
    }

    if (backend.has_axiom_taint || backend.has_oracle_taint || backend.has_unsafe_taint) && !is_layout_rejected(status) {
        open_obligations.push(
            "layout descriptor preserves Axiom/Oracle/Unsafe taint and is not a clean runtime ABI artifact".to_string(),
        );
        status = BackendLayoutStatus::DowngradedTainted;
    }

    open_obligations.sort();
    open_obligations.dedup();

    let runtime_descriptor = format!(
        "runtime_descriptor<name={},backend={},layout={},target={},op={},abi={},container={},elem={},size={},align={},metadata={},abi_hash={},backend_fp={},layout_fp={}>",
        name,
        backend.name,
        descriptor.name,
        descriptor.target,
        descriptor.operation,
        descriptor.scalar,
        descriptor.container,
        descriptor.element_type,
        descriptor.element_size_bytes,
        descriptor.alignment_bytes,
        descriptor.metadata_policy,
        descriptor.stable_abi_hash,
        backend.fingerprint,
        descriptor.fingerprint,
    );

    let fingerprint = stable_fingerprint(&[
        "backend-layout-report-v1".to_string(),
        name.clone(),
        backend.name.clone(),
        descriptor.name.clone(),
        backend.operation.to_string(),
        backend.target.to_string(),
        backend.status.to_string(),
        descriptor.status.to_string(),
        status.to_string(),
        descriptor.stable_abi_hash.clone(),
        backend.fingerprint.clone(),
        descriptor.fingerprint.clone(),
    ]);

    Ok(BackendLayoutReport {
        name,
        backend_plan: backend.name.clone(),
        layout_descriptor: descriptor.name.clone(),
        operation: backend.operation,
        target: backend.target,
        backend_status: backend.status,
        layout_status: descriptor.status,
        status,
        scalar: descriptor.scalar,
        container: descriptor.container,
        element_type: descriptor.element_type.clone(),
        element_size_bytes: descriptor.element_size_bytes,
        alignment_bytes: descriptor.alignment_bytes,
        metadata_policy: descriptor.metadata_policy,
        stable_abi_hash: descriptor.stable_abi_hash.clone(),
        runtime_descriptor,
        open_obligations,
        max_trust: backend.max_trust,
        max_provenance: backend.max_provenance,
        has_axiom_taint: backend.has_axiom_taint,
        has_oracle_taint: backend.has_oracle_taint,
        has_unsafe_taint: backend.has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_backend_layout(report: &BackendLayoutReport, line: usize) -> Result<(), Diagnostic> {
    match report.status {
        BackendLayoutStatus::VerifiedLayout => Ok(()),
        BackendLayoutStatus::SymbolicLayout
        | BackendLayoutStatus::DowngradedTainted
        | BackendLayoutStatus::RejectedBackend
        | BackendLayoutStatus::RejectedTarget
        | BackendLayoutStatus::RejectedAbi => Err(backend_layout_error(
            line,
            format!("backend layout `{}` is {}, not verified_layout", report.name, report.status),
            "runtime/compiler execution may consume only verified layout reports unless symbolic or tainted execution is explicitly accepted",
        )),
    }
}

pub fn backend_layout_descriptor_passport(
    theory: &str,
    descriptor: &BackendLayoutDescriptor,
    sources: &[&Passport],
) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::BackendLayoutDescriptor {
            name: descriptor.name.clone(),
            target: descriptor.target.to_string(),
            status: descriptor.status.to_string(),
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
                "backend:layout_descriptor:{}:{}:{}:{}:abi_hash={}:fingerprint={}",
                descriptor.name,
                descriptor.operation,
                descriptor.target,
                descriptor.status,
                descriptor.stable_abi_hash,
                descriptor.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn backend_layout_report_passport(theory: &str, report: &BackendLayoutReport, sources: &[&Passport]) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::BackendLayoutReport {
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
                "backend:layout_report:{}:{}:{}:{}:abi_hash={}:fingerprint={}",
                report.name, report.operation, report.target, report.status, report.stable_abi_hash, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_backend_layout_descriptor(descriptor: &BackendLayoutDescriptor) -> String {
    let mut out = String::new();
    out.push_str("backend_layout_descriptor: v1\n");
    out.push_str(&format!("name: {}\n", descriptor.name));
    out.push_str(&format!("target: {}\n", descriptor.target));
    out.push_str(&format!("operation: {}\n", descriptor.operation));
    out.push_str(&format!("scalar: {}\n", descriptor.scalar));
    out.push_str(&format!("container: {}\n", descriptor.container));
    out.push_str(&format!("element_type: {}\n", descriptor.element_type));
    out.push_str(&format!("element_size_bytes: {}\n", descriptor.element_size_bytes));
    out.push_str(&format!("alignment_bytes: {}\n", descriptor.alignment_bytes));
    out.push_str(&format!("metadata_policy: {}\n", descriptor.metadata_policy));
    out.push_str(&format!("status: {}\n", descriptor.status));
    out.push_str("open_obligations:\n");
    for obligation in &descriptor.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("stable_abi_hash: {}\n", descriptor.stable_abi_hash));
    out.push_str(&format!("fingerprint: {}\n", descriptor.fingerprint));
    out
}

pub fn export_backend_layout_report(report: &BackendLayoutReport) -> String {
    let mut out = String::new();
    out.push_str("backend_layout_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("backend_plan: {}\n", report.backend_plan));
    out.push_str(&format!("layout_descriptor: {}\n", report.layout_descriptor));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("target: {}\n", report.target));
    out.push_str(&format!("backend_status: {}\n", report.backend_status));
    out.push_str(&format!("layout_status: {}\n", report.layout_status));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("scalar: {}\n", report.scalar));
    out.push_str(&format!("container: {}\n", report.container));
    out.push_str(&format!("element_type: {}\n", report.element_type));
    out.push_str(&format!("element_size_bytes: {}\n", report.element_size_bytes));
    out.push_str(&format!("alignment_bytes: {}\n", report.alignment_bytes));
    out.push_str(&format!("metadata_policy: {}\n", report.metadata_policy));
    out.push_str(&format!("stable_abi_hash: {}\n", report.stable_abi_hash));
    out.push_str(&format!("runtime_descriptor: {}\n", report.runtime_descriptor));
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

fn container_allowed_for_target(target: PreludeLoweringTarget, container: LayoutContainerKind) -> bool {
    match target {
        PreludeLoweringTarget::AuditOnly => container == LayoutContainerKind::AuditOnlyDescriptor,
        PreludeLoweringTarget::Interpreter => matches!(container, LayoutContainerKind::TaggedUnion | LayoutContainerKind::Scalar),
        PreludeLoweringTarget::NativeScalar => container == LayoutContainerKind::Scalar,
        PreludeLoweringTarget::NativeVector => matches!(container, LayoutContainerKind::DenseArray | LayoutContainerKind::SliceView),
        PreludeLoweringTarget::GpuBatch => container == LayoutContainerKind::GpuBuffer,
        PreludeLoweringTarget::RemoteBatch => container == LayoutContainerKind::RemoteBuffer,
    }
}

fn container_allowed_for_operation(operation: PreludeOperationKind, container: LayoutContainerKind) -> bool {
    if scalar_operation(operation) {
        matches!(container, LayoutContainerKind::Scalar | LayoutContainerKind::TaggedUnion | LayoutContainerKind::AuditOnlyDescriptor)
    } else {
        matches!(
            container,
            LayoutContainerKind::DenseArray
                | LayoutContainerKind::SliceView
                | LayoutContainerKind::GpuBuffer
                | LayoutContainerKind::RemoteBuffer
                | LayoutContainerKind::AuditOnlyDescriptor
        )
    }
}

fn scalar_allowed_for_operation(operation: PreludeOperationKind, scalar: AbiScalarKind) -> bool {
    match operation {
        PreludeOperationKind::NatAdd => scalar == AbiScalarKind::Nat64,
        PreludeOperationKind::NatEq | PreludeOperationKind::BoolAnd | PreludeOperationKind::BoolNot => {
            matches!(scalar, AbiScalarKind::Bool1 | AbiScalarKind::Nat64)
        }
        PreludeOperationKind::OptionMap
        | PreludeOperationKind::ResultMap
        | PreludeOperationKind::SequenceIndex => matches!(scalar, AbiScalarKind::TaggedUnion64 | AbiScalarKind::OpaqueSymbolic),
        PreludeOperationKind::ListLength | PreludeOperationKind::SequenceLength => {
            matches!(scalar, AbiScalarKind::Nat64 | AbiScalarKind::PointerSized)
        }
        PreludeOperationKind::ListMap
        | PreludeOperationKind::SequenceMap
        | PreludeOperationKind::ListFold
        | PreludeOperationKind::SequenceFold => matches!(scalar, AbiScalarKind::Nat64 | AbiScalarKind::OpaqueSymbolic),
    }
}

fn scalar_operation(operation: PreludeOperationKind) -> bool {
    matches!(
        operation,
        PreludeOperationKind::NatAdd
            | PreludeOperationKind::NatEq
            | PreludeOperationKind::BoolAnd
            | PreludeOperationKind::BoolNot
            | PreludeOperationKind::OptionMap
            | PreludeOperationKind::ResultMap
            | PreludeOperationKind::SequenceIndex
    )
}

fn symbolic_operation(operation: PreludeOperationKind) -> bool {
    matches!(
        operation,
        PreludeOperationKind::OptionMap
            | PreludeOperationKind::ResultMap
            | PreludeOperationKind::ListMap
            | PreludeOperationKind::SequenceMap
            | PreludeOperationKind::ListFold
            | PreludeOperationKind::SequenceFold
    )
}

fn runtime_forbidden_metadata(target: PreludeLoweringTarget, metadata: LayoutMetadataPolicy) -> bool {
    if target == PreludeLoweringTarget::AuditOnly {
        return false;
    }
    matches!(
        metadata,
        LayoutMetadataPolicy::FullPassport | LayoutMetadataPolicy::InterleavedPerElementPassport
    )
}

fn is_backend_rejected(status: BackendLoweringStatus) -> bool {
    matches!(
        status,
        BackendLoweringStatus::RejectedLowering
            | BackendLoweringStatus::RejectedTarget
            | BackendLoweringStatus::RejectedCapability
    )
}

fn is_layout_rejected(status: BackendLayoutStatus) -> bool {
    matches!(
        status,
        BackendLayoutStatus::RejectedBackend | BackendLayoutStatus::RejectedTarget | BackendLayoutStatus::RejectedAbi
    )
}

fn validate_identifier(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(backend_layout_error(
            line,
            format!("{label} must not be empty"),
            "layout and ABI artifacts require stable names for cache keys and audit descriptors",
        ));
    }
    if value.chars().any(|ch| ch.is_whitespace()) {
        return Err(backend_layout_error(
            line,
            format!("{label} `{value}` must not contain whitespace"),
            "use a stable identifier such as nat64_scalar_abi",
        ));
    }
    Ok(())
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

fn backend_layout_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::BackendLayoutError, Some(line), message.into()).with_help(help.into())
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
    format!("bl{:016x}", hash)
}
