use std::fmt;

use crate::backend_layout::{BackendLayoutReport, BackendLayoutStatus, LayoutContainerKind, LayoutMetadataPolicy};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::prelude_lowering::PreludeLoweringTarget;
use crate::standard_prelude::PreludeOperationKind;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum RuntimeRepresentationKind {
    ScalarValue,
    TaggedValue,
    DenseVector,
    SliceView,
    GpuRegion,
    RemoteRegion,
    AuditDescriptorOnly,
}

impl fmt::Display for RuntimeRepresentationKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RuntimeRepresentationKind::ScalarValue => write!(f, "scalar_value"),
            RuntimeRepresentationKind::TaggedValue => write!(f, "tagged_value"),
            RuntimeRepresentationKind::DenseVector => write!(f, "dense_vector"),
            RuntimeRepresentationKind::SliceView => write!(f, "slice_view"),
            RuntimeRepresentationKind::GpuRegion => write!(f, "gpu_region"),
            RuntimeRepresentationKind::RemoteRegion => write!(f, "remote_region"),
            RuntimeRepresentationKind::AuditDescriptorOnly => write!(f, "audit_descriptor_only"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum RuntimeOwnershipMode {
    OwnedUnique,
    BorrowedReadOnly,
    SharedImmutable,
    GpuResidentHandle,
    RemoteHandle,
    AuditOnly,
}

impl fmt::Display for RuntimeOwnershipMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RuntimeOwnershipMode::OwnedUnique => write!(f, "owned_unique"),
            RuntimeOwnershipMode::BorrowedReadOnly => write!(f, "borrowed_read_only"),
            RuntimeOwnershipMode::SharedImmutable => write!(f, "shared_immutable"),
            RuntimeOwnershipMode::GpuResidentHandle => write!(f, "gpu_resident_handle"),
            RuntimeOwnershipMode::RemoteHandle => write!(f, "remote_handle"),
            RuntimeOwnershipMode::AuditOnly => write!(f, "audit_only"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum DenseRuntimeStatus {
    VerifiedDense,
    SymbolicDense,
    DowngradedTainted,
    RejectedLayout,
    RejectedRepresentation,
    RejectedOwnership,
}

impl fmt::Display for DenseRuntimeStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DenseRuntimeStatus::VerifiedDense => write!(f, "verified_dense"),
            DenseRuntimeStatus::SymbolicDense => write!(f, "symbolic_dense"),
            DenseRuntimeStatus::DowngradedTainted => write!(f, "downgraded_tainted"),
            DenseRuntimeStatus::RejectedLayout => write!(f, "rejected_layout"),
            DenseRuntimeStatus::RejectedRepresentation => write!(f, "rejected_representation"),
            DenseRuntimeStatus::RejectedOwnership => write!(f, "rejected_ownership"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DenseRuntimeDescriptor {
    pub name: String,
    pub target: PreludeLoweringTarget,
    pub operation: PreludeOperationKind,
    pub representation: RuntimeRepresentationKind,
    pub ownership: RuntimeOwnershipMode,
    pub element_type: String,
    pub element_count: usize,
    pub element_size_bytes: usize,
    pub stride_bytes: usize,
    pub alignment_bytes: usize,
    pub status: DenseRuntimeStatus,
    pub open_obligations: Vec<String>,
    pub dense_descriptor: String,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DenseRuntimeReport {
    pub name: String,
    pub layout_report: String,
    pub runtime_descriptor: String,
    pub operation: PreludeOperationKind,
    pub target: PreludeLoweringTarget,
    pub layout_status: BackendLayoutStatus,
    pub descriptor_status: DenseRuntimeStatus,
    pub status: DenseRuntimeStatus,
    pub representation: RuntimeRepresentationKind,
    pub ownership: RuntimeOwnershipMode,
    pub element_type: String,
    pub element_count: usize,
    pub element_size_bytes: usize,
    pub stride_bytes: usize,
    pub alignment_bytes: usize,
    pub byte_len: usize,
    pub dense_descriptor: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn dense_runtime_descriptor(
    name: impl Into<String>,
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    representation: RuntimeRepresentationKind,
    ownership: RuntimeOwnershipMode,
    element_type: impl Into<String>,
    element_count: usize,
    element_size_bytes: usize,
    stride_bytes: usize,
    alignment_bytes: usize,
    line: usize,
) -> Result<DenseRuntimeDescriptor, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "dense runtime descriptor name", line)?;
    let element_type = element_type.into();
    validate_identifier(&element_type, "dense runtime element type", line)?;

    let mut open_obligations = Vec::new();
    let mut status = DenseRuntimeStatus::VerifiedDense;

    if element_count == 0 && representation != RuntimeRepresentationKind::AuditDescriptorOnly {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` has zero elements outside audit-only mode"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if element_size_bytes == 0 {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` has zero-sized element payload"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if stride_bytes < element_size_bytes {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` stride `{stride_bytes}` is smaller than element size `{element_size_bytes}`"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if alignment_bytes == 0 || !alignment_bytes.is_power_of_two() {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` alignment `{alignment_bytes}` is not a positive power-of-two"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if !representation_allowed_for_target(target, representation) {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` uses representation `{representation}` for target `{target}`"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if !ownership_allowed_for_target(target, ownership) {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` uses ownership `{ownership}` for target `{target}`"
        ));
        status = DenseRuntimeStatus::RejectedOwnership;
    }
    if scalar_representation(representation) && element_count != 1 {
        open_obligations.push(format!(
            "scalar runtime descriptor `{name}` must contain exactly one element, got `{element_count}`"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }
    if representation == RuntimeRepresentationKind::DenseVector && stride_bytes != element_size_bytes {
        open_obligations.push(format!(
            "dense vector `{name}` must have stride equal to element size; strided views must use slice_view"
        ));
        status = DenseRuntimeStatus::RejectedRepresentation;
    }

    if status == DenseRuntimeStatus::VerifiedDense && symbolic_operation(operation) {
        open_obligations.push(format!(
            "dense runtime descriptor `{name}` is symbolic for `{operation}`; arbitrary function bodies remain outside the hot payload"
        ));
        status = DenseRuntimeStatus::SymbolicDense;
    }

    open_obligations.sort();
    open_obligations.dedup();

    let byte_len = element_count.saturating_mul(stride_bytes);
    let dense_descriptor = format!(
        "dense_runtime_descriptor<name={name},target={target},op={operation},repr={representation},ownership={ownership},elem={element_type},count={element_count},size={element_size_bytes},stride={stride_bytes},align={alignment_bytes},bytes={byte_len}>"
    );
    let fingerprint = stable_fingerprint(&[
        "dense-runtime-descriptor-v1".to_string(),
        name.clone(),
        target.to_string(),
        operation.to_string(),
        representation.to_string(),
        ownership.to_string(),
        element_type.clone(),
        element_count.to_string(),
        element_size_bytes.to_string(),
        stride_bytes.to_string(),
        alignment_bytes.to_string(),
        status.to_string(),
    ]);

    Ok(DenseRuntimeDescriptor {
        name,
        target,
        operation,
        representation,
        ownership,
        element_type,
        element_count,
        element_size_bytes,
        stride_bytes,
        alignment_bytes,
        status,
        open_obligations,
        dense_descriptor,
        fingerprint,
    })
}

pub fn validate_dense_runtime(
    name: impl Into<String>,
    layout: &BackendLayoutReport,
    descriptor: &DenseRuntimeDescriptor,
    line: usize,
) -> Result<DenseRuntimeReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "dense runtime report name", line)?;

    let mut open_obligations = Vec::new();
    open_obligations.extend(layout.open_obligations.clone());
    open_obligations.extend(descriptor.open_obligations.clone());

    let mut status = if is_layout_rejected(layout.status) {
        open_obligations.push(format!(
            "backend layout `{}` is {}, so no dense runtime descriptor may certify it as executable payload",
            layout.name, layout.status
        ));
        DenseRuntimeStatus::RejectedLayout
    } else if layout.target != descriptor.target {
        open_obligations.push(format!(
            "layout `{}` targets `{}`, but dense descriptor `{}` targets `{}`",
            layout.name, layout.target, descriptor.name, descriptor.target
        ));
        DenseRuntimeStatus::RejectedLayout
    } else if layout.operation != descriptor.operation {
        open_obligations.push(format!(
            "layout `{}` is for `{}`, but dense descriptor `{}` is for `{}`",
            layout.name, layout.operation, descriptor.name, descriptor.operation
        ));
        DenseRuntimeStatus::RejectedLayout
    } else if is_runtime_rejected(descriptor.status) {
        descriptor.status
    } else if !representation_matches_layout(layout.container, descriptor.representation) {
        open_obligations.push(format!(
            "layout `{}` container `{}` cannot be represented as dense runtime `{}` without an explicit representation bridge",
            layout.name, layout.container, descriptor.representation
        ));
        DenseRuntimeStatus::RejectedRepresentation
    } else if layout.element_type != descriptor.element_type {
        open_obligations.push(format!(
            "layout `{}` element type `{}` differs from dense descriptor `{}` element type `{}`",
            layout.name, layout.element_type, descriptor.name, descriptor.element_type
        ));
        DenseRuntimeStatus::RejectedRepresentation
    } else if layout.element_size_bytes != descriptor.element_size_bytes
        || layout.alignment_bytes != descriptor.alignment_bytes
    {
        open_obligations.push(format!(
            "layout `{}` ABI size/alignment differs from dense descriptor `{}`",
            layout.name, descriptor.name
        ));
        DenseRuntimeStatus::RejectedRepresentation
    } else if layout.metadata_policy == LayoutMetadataPolicy::FullPassport
        || layout.metadata_policy == LayoutMetadataPolicy::InterleavedPerElementPassport
    {
        open_obligations.push(format!(
            "layout `{}` still carries full/per-element passport metadata and cannot be a dense hot runtime region",
            layout.name
        ));
        DenseRuntimeStatus::RejectedRepresentation
    } else if layout.status == BackendLayoutStatus::SymbolicLayout
        || descriptor.status == DenseRuntimeStatus::SymbolicDense
    {
        open_obligations.push(format!(
            "dense runtime `{}` preserves symbolic bounded payload from layout `{}` without executing arbitrary user code",
            descriptor.name, layout.name
        ));
        DenseRuntimeStatus::SymbolicDense
    } else {
        DenseRuntimeStatus::VerifiedDense
    };

    if (layout.has_axiom_taint || layout.has_oracle_taint || layout.has_unsafe_taint) && !is_runtime_rejected(status) {
        open_obligations.push(
            "dense runtime descriptor preserves Axiom/Oracle/Unsafe taint and is not a clean hot runtime artifact".to_string(),
        );
        status = DenseRuntimeStatus::DowngradedTainted;
    }

    open_obligations.sort();
    open_obligations.dedup();

    let byte_len = descriptor.element_count.saturating_mul(descriptor.stride_bytes);
    let dense_descriptor = format!(
        "dense_runtime_report<name={},layout={},runtime={},target={},op={},repr={},ownership={},elem={},count={},size={},stride={},align={},bytes={},layout_fp={},runtime_fp={}>",
        name,
        layout.name,
        descriptor.name,
        descriptor.target,
        descriptor.operation,
        descriptor.representation,
        descriptor.ownership,
        descriptor.element_type,
        descriptor.element_count,
        descriptor.element_size_bytes,
        descriptor.stride_bytes,
        descriptor.alignment_bytes,
        byte_len,
        layout.fingerprint,
        descriptor.fingerprint,
    );

    let fingerprint = stable_fingerprint(&[
        "dense-runtime-report-v1".to_string(),
        name.clone(),
        layout.name.clone(),
        descriptor.name.clone(),
        layout.operation.to_string(),
        layout.target.to_string(),
        layout.status.to_string(),
        descriptor.status.to_string(),
        status.to_string(),
        layout.fingerprint.clone(),
        descriptor.fingerprint.clone(),
    ]);

    Ok(DenseRuntimeReport {
        name,
        layout_report: layout.name.clone(),
        runtime_descriptor: descriptor.name.clone(),
        operation: layout.operation,
        target: layout.target,
        layout_status: layout.status,
        descriptor_status: descriptor.status,
        status,
        representation: descriptor.representation,
        ownership: descriptor.ownership,
        element_type: descriptor.element_type.clone(),
        element_count: descriptor.element_count,
        element_size_bytes: descriptor.element_size_bytes,
        stride_bytes: descriptor.stride_bytes,
        alignment_bytes: descriptor.alignment_bytes,
        byte_len,
        dense_descriptor,
        open_obligations,
        max_trust: layout.max_trust,
        max_provenance: layout.max_provenance,
        has_axiom_taint: layout.has_axiom_taint,
        has_oracle_taint: layout.has_oracle_taint,
        has_unsafe_taint: layout.has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_dense_runtime(report: &DenseRuntimeReport, line: usize) -> Result<(), Diagnostic> {
    match report.status {
        DenseRuntimeStatus::VerifiedDense => Ok(()),
        DenseRuntimeStatus::SymbolicDense
        | DenseRuntimeStatus::DowngradedTainted
        | DenseRuntimeStatus::RejectedLayout
        | DenseRuntimeStatus::RejectedRepresentation
        | DenseRuntimeStatus::RejectedOwnership => Err(dense_runtime_error(
            line,
            format!("dense runtime `{}` is {}, not verified_dense", report.name, report.status),
            "hot runtime/compiler execution may consume only verified dense runtime reports unless symbolic or tainted execution is explicitly accepted",
        )),
    }
}

pub fn dense_runtime_descriptor_passport(
    theory: &str,
    descriptor: &DenseRuntimeDescriptor,
    sources: &[&Passport],
) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::DenseRuntimeDescriptor {
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
                "runtime:dense_descriptor:{}:{}:{}:{}:fingerprint={}",
                descriptor.name, descriptor.operation, descriptor.target, descriptor.status, descriptor.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn dense_runtime_report_passport(theory: &str, report: &DenseRuntimeReport, sources: &[&Passport]) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::DenseRuntimeReport {
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
                "runtime:dense_report:{}:{}:{}:{}:bytes={}:fingerprint={}",
                report.name, report.operation, report.target, report.status, report.byte_len, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_dense_runtime_descriptor(descriptor: &DenseRuntimeDescriptor) -> String {
    let mut out = String::new();
    out.push_str("dense_runtime_descriptor: v1\n");
    out.push_str(&format!("name: {}\n", descriptor.name));
    out.push_str(&format!("target: {}\n", descriptor.target));
    out.push_str(&format!("operation: {}\n", descriptor.operation));
    out.push_str(&format!("representation: {}\n", descriptor.representation));
    out.push_str(&format!("ownership: {}\n", descriptor.ownership));
    out.push_str(&format!("element_type: {}\n", descriptor.element_type));
    out.push_str(&format!("element_count: {}\n", descriptor.element_count));
    out.push_str(&format!("element_size_bytes: {}\n", descriptor.element_size_bytes));
    out.push_str(&format!("stride_bytes: {}\n", descriptor.stride_bytes));
    out.push_str(&format!("alignment_bytes: {}\n", descriptor.alignment_bytes));
    out.push_str(&format!("status: {}\n", descriptor.status));
    out.push_str("open_obligations:\n");
    for obligation in &descriptor.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("dense_descriptor: {}\n", descriptor.dense_descriptor));
    out.push_str(&format!("fingerprint: {}\n", descriptor.fingerprint));
    out
}

pub fn export_dense_runtime_report(report: &DenseRuntimeReport) -> String {
    let mut out = String::new();
    out.push_str("dense_runtime_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("layout_report: {}\n", report.layout_report));
    out.push_str(&format!("runtime_descriptor: {}\n", report.runtime_descriptor));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("target: {}\n", report.target));
    out.push_str(&format!("layout_status: {}\n", report.layout_status));
    out.push_str(&format!("descriptor_status: {}\n", report.descriptor_status));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("representation: {}\n", report.representation));
    out.push_str(&format!("ownership: {}\n", report.ownership));
    out.push_str(&format!("element_type: {}\n", report.element_type));
    out.push_str(&format!("element_count: {}\n", report.element_count));
    out.push_str(&format!("element_size_bytes: {}\n", report.element_size_bytes));
    out.push_str(&format!("stride_bytes: {}\n", report.stride_bytes));
    out.push_str(&format!("alignment_bytes: {}\n", report.alignment_bytes));
    out.push_str(&format!("byte_len: {}\n", report.byte_len));
    out.push_str(&format!("dense_descriptor: {}\n", report.dense_descriptor));
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

fn representation_allowed_for_target(target: PreludeLoweringTarget, representation: RuntimeRepresentationKind) -> bool {
    match target {
        PreludeLoweringTarget::AuditOnly => representation == RuntimeRepresentationKind::AuditDescriptorOnly,
        PreludeLoweringTarget::Interpreter => matches!(representation, RuntimeRepresentationKind::ScalarValue | RuntimeRepresentationKind::TaggedValue),
        PreludeLoweringTarget::NativeScalar => representation == RuntimeRepresentationKind::ScalarValue,
        PreludeLoweringTarget::NativeVector => matches!(representation, RuntimeRepresentationKind::DenseVector | RuntimeRepresentationKind::SliceView),
        PreludeLoweringTarget::GpuBatch => representation == RuntimeRepresentationKind::GpuRegion,
        PreludeLoweringTarget::RemoteBatch => representation == RuntimeRepresentationKind::RemoteRegion,
    }
}

fn ownership_allowed_for_target(target: PreludeLoweringTarget, ownership: RuntimeOwnershipMode) -> bool {
    match target {
        PreludeLoweringTarget::AuditOnly => ownership == RuntimeOwnershipMode::AuditOnly,
        PreludeLoweringTarget::Interpreter | PreludeLoweringTarget::NativeScalar | PreludeLoweringTarget::NativeVector => {
            matches!(ownership, RuntimeOwnershipMode::OwnedUnique | RuntimeOwnershipMode::BorrowedReadOnly | RuntimeOwnershipMode::SharedImmutable)
        }
        PreludeLoweringTarget::GpuBatch => ownership == RuntimeOwnershipMode::GpuResidentHandle,
        PreludeLoweringTarget::RemoteBatch => ownership == RuntimeOwnershipMode::RemoteHandle,
    }
}

fn representation_matches_layout(container: LayoutContainerKind, representation: RuntimeRepresentationKind) -> bool {
    match container {
        LayoutContainerKind::Scalar => representation == RuntimeRepresentationKind::ScalarValue,
        LayoutContainerKind::TaggedUnion => representation == RuntimeRepresentationKind::TaggedValue,
        LayoutContainerKind::DenseArray => representation == RuntimeRepresentationKind::DenseVector,
        LayoutContainerKind::SliceView => representation == RuntimeRepresentationKind::SliceView,
        LayoutContainerKind::GpuBuffer => representation == RuntimeRepresentationKind::GpuRegion,
        LayoutContainerKind::RemoteBuffer => representation == RuntimeRepresentationKind::RemoteRegion,
        LayoutContainerKind::AuditOnlyDescriptor => representation == RuntimeRepresentationKind::AuditDescriptorOnly,
    }
}

fn scalar_representation(representation: RuntimeRepresentationKind) -> bool {
    matches!(representation, RuntimeRepresentationKind::ScalarValue | RuntimeRepresentationKind::TaggedValue)
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

fn is_layout_rejected(status: BackendLayoutStatus) -> bool {
    matches!(
        status,
        BackendLayoutStatus::RejectedBackend | BackendLayoutStatus::RejectedTarget | BackendLayoutStatus::RejectedAbi
    )
}

fn is_runtime_rejected(status: DenseRuntimeStatus) -> bool {
    matches!(
        status,
        DenseRuntimeStatus::RejectedLayout | DenseRuntimeStatus::RejectedRepresentation | DenseRuntimeStatus::RejectedOwnership
    )
}

fn validate_identifier(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(dense_runtime_error(
            line,
            format!("{label} must not be empty"),
            "dense runtime artifacts require stable names for cache keys, descriptors and audit paths",
        ));
    }
    if value.chars().any(|ch| ch.is_whitespace()) {
        return Err(dense_runtime_error(
            line,
            format!("{label} `{value}` must not contain whitespace"),
            "use a stable identifier such as nat64_dense_runtime",
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

fn dense_runtime_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::DenseRuntimeError, Some(line), message.into()).with_help(help.into())
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
    format!("dr{:016x}", hash)
}
