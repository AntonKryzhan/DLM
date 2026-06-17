use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::prelude_eval::{PreludeEvalStatus, PreludeEvalValue, PreludeEvaluationReport};
use crate::standard_prelude::PreludeOperationKind;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeLoweringTarget {
    AuditOnly,
    Interpreter,
    NativeScalar,
    NativeVector,
    GpuBatch,
    RemoteBatch,
}

impl fmt::Display for PreludeLoweringTarget {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeLoweringTarget::AuditOnly => write!(f, "audit_only"),
            PreludeLoweringTarget::Interpreter => write!(f, "interpreter"),
            PreludeLoweringTarget::NativeScalar => write!(f, "native_scalar"),
            PreludeLoweringTarget::NativeVector => write!(f, "native_vector"),
            PreludeLoweringTarget::GpuBatch => write!(f, "gpu_batch"),
            PreludeLoweringTarget::RemoteBatch => write!(f, "remote_batch"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeErasureMode {
    AuditOnly,
    ProofErased,
    PassportErasedWithDescriptor,
}

impl fmt::Display for PreludeErasureMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeErasureMode::AuditOnly => write!(f, "audit_only"),
            PreludeErasureMode::ProofErased => write!(f, "proof_erased"),
            PreludeErasureMode::PassportErasedWithDescriptor => write!(f, "passport_erased_with_descriptor"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeLoweringStatus {
    VerifiedErased,
    SymbolicLowered,
    DowngradedTainted,
    RejectedEvaluation,
    RejectedTarget,
    RejectedEvidenceBoundary,
}

impl fmt::Display for PreludeLoweringStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeLoweringStatus::VerifiedErased => write!(f, "verified_erased"),
            PreludeLoweringStatus::SymbolicLowered => write!(f, "symbolic_lowered"),
            PreludeLoweringStatus::DowngradedTainted => write!(f, "downgraded_tainted"),
            PreludeLoweringStatus::RejectedEvaluation => write!(f, "rejected_evaluation"),
            PreludeLoweringStatus::RejectedTarget => write!(f, "rejected_target"),
            PreludeLoweringStatus::RejectedEvidenceBoundary => write!(f, "rejected_evidence_boundary"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreludeLoweringReport {
    pub name: String,
    pub operation: PreludeOperationKind,
    pub target: PreludeLoweringTarget,
    pub erasure: PreludeErasureMode,
    pub input_type: String,
    pub output_type: String,
    pub evaluation: String,
    pub evaluation_status: PreludeEvalStatus,
    pub representation: String,
    pub status: PreludeLoweringStatus,
    pub proof_erased: bool,
    pub passport_erased: bool,
    pub descriptor: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn lower_prelude_evaluation(
    name: impl Into<String>,
    evaluation: &PreludeEvaluationReport,
    target: PreludeLoweringTarget,
    erasure: PreludeErasureMode,
    line: usize,
) -> Result<PreludeLoweringReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "prelude lowering name", line)?;

    let mut open_obligations = evaluation.open_obligations.clone();
    let mut status = match evaluation.status {
        PreludeEvalStatus::Evaluated => PreludeLoweringStatus::VerifiedErased,
        PreludeEvalStatus::SymbolicEvaluated => {
            open_obligations.push("lowering is symbolic: prelude operation was bounded, but arbitrary user function body was not executed".to_string());
            PreludeLoweringStatus::SymbolicLowered
        }
        PreludeEvalStatus::RejectedContract | PreludeEvalStatus::RejectedInput | PreludeEvalStatus::RejectedFuel => {
            open_obligations.push(format!(
                "prelude evaluation `{}` is {}, so lowering cannot preserve value semantics",
                evaluation.name, evaluation.status
            ));
            PreludeLoweringStatus::RejectedEvaluation
        }
    };

    if !target_accepts_operation(target, evaluation.operation) && !is_rejected(status) {
        open_obligations.push(format!(
            "target `{target}` is not valid for prelude operation `{}` without an explicit lowering bridge",
            evaluation.operation
        ));
        status = PreludeLoweringStatus::RejectedTarget;
    }

    if let Some(result) = &evaluation.result {
        if contains_evidence_boundary(result) && !is_rejected(status) {
            open_obligations.push("prelude result contains proof/theorem/truth/runtime evidence and cannot be erased into runtime data".to_string());
            status = PreludeLoweringStatus::RejectedEvidenceBoundary;
        }
    }

    if (evaluation.has_axiom_taint || evaluation.has_oracle_taint || evaluation.has_unsafe_taint)
        && !is_rejected(status)
    {
        open_obligations.push("lowered artifact preserves Axiom/Oracle/Unsafe taint and is not a clean checked runtime artifact".to_string());
        status = PreludeLoweringStatus::DowngradedTainted;
    }

    let proof_erased = erasure != PreludeErasureMode::AuditOnly;
    let passport_erased = erasure == PreludeErasureMode::PassportErasedWithDescriptor;
    let representation = representation_for(target, evaluation.operation, &evaluation.output_type, proof_erased, passport_erased);
    let descriptor = format!(
        "descriptor<name={},op={},target={},erasure={},input={},output={},eval_fp={}>",
        name, evaluation.operation, target, erasure, evaluation.input_type, evaluation.output_type, evaluation.fingerprint
    );

    open_obligations.sort();
    open_obligations.dedup();

    let fingerprint = stable_fingerprint(&[
        "prelude-lowering-v1".to_string(),
        name.clone(),
        evaluation.name.clone(),
        evaluation.operation.to_string(),
        target.to_string(),
        erasure.to_string(),
        representation.clone(),
        status.to_string(),
        format!("trust={:?}", evaluation.max_trust),
        evaluation.fingerprint.clone(),
    ]);

    Ok(PreludeLoweringReport {
        name,
        operation: evaluation.operation,
        target,
        erasure,
        input_type: evaluation.input_type.clone(),
        output_type: evaluation.output_type.clone(),
        evaluation: evaluation.name.clone(),
        evaluation_status: evaluation.status,
        representation,
        status,
        proof_erased,
        passport_erased,
        descriptor,
        open_obligations,
        max_trust: evaluation.max_trust,
        max_provenance: evaluation.max_provenance,
        has_axiom_taint: evaluation.has_axiom_taint,
        has_oracle_taint: evaluation.has_oracle_taint,
        has_unsafe_taint: evaluation.has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_lowering(report: &PreludeLoweringReport, line: usize) -> Result<(), Diagnostic> {
    match report.status {
        PreludeLoweringStatus::VerifiedErased => Ok(()),
        PreludeLoweringStatus::SymbolicLowered
        | PreludeLoweringStatus::DowngradedTainted
        | PreludeLoweringStatus::RejectedEvaluation
        | PreludeLoweringStatus::RejectedTarget
        | PreludeLoweringStatus::RejectedEvidenceBoundary => Err(prelude_lowering_error(
            line,
            format!("prelude lowering `{}` is {}, not verified_erased", report.name, report.status),
            "compiler/runtime lowering may consume only verified-erased prelude reports unless it explicitly accepts symbolic/tainted/rejected audit status",
        )),
    }
}

pub fn prelude_lowering_passport(theory: &str, report: &PreludeLoweringReport, sources: &[&Passport]) -> Passport {
    let (source_trust, source_provenance) = source_taint(sources);
    Passport {
        ty: TypeKind::PreludeLoweringReport {
            name: report.name.clone(),
            target: report.target.to_string(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompareByProof,
        ]),
        cost: CostClass::SmallFinite,
        trust: report.max_trust.max(source_trust).max(TrustLevel::Builtin),
        provenance: report.max_provenance.max(source_provenance).max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(
            sources,
            format!(
                "standard_prelude:lower:{}:{}:{}:{}:fingerprint={}",
                report.name, report.operation, report.target, report.status, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_prelude_lowering(report: &PreludeLoweringReport) -> String {
    let mut out = String::new();
    out.push_str("prelude_lowering: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("target: {}\n", report.target));
    out.push_str(&format!("erasure: {}\n", report.erasure));
    out.push_str(&format!("input_type: {}\n", report.input_type));
    out.push_str(&format!("output_type: {}\n", report.output_type));
    out.push_str(&format!("evaluation: {}\n", report.evaluation));
    out.push_str(&format!("evaluation_status: {}\n", report.evaluation_status));
    out.push_str(&format!("representation: {}\n", report.representation));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("proof_erased: {}\n", report.proof_erased));
    out.push_str(&format!("passport_erased: {}\n", report.passport_erased));
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

fn target_accepts_operation(target: PreludeLoweringTarget, operation: PreludeOperationKind) -> bool {
    match target {
        PreludeLoweringTarget::AuditOnly | PreludeLoweringTarget::Interpreter => true,
        PreludeLoweringTarget::NativeScalar => matches!(
            operation,
            PreludeOperationKind::NatAdd
                | PreludeOperationKind::NatEq
                | PreludeOperationKind::BoolAnd
                | PreludeOperationKind::BoolNot
                | PreludeOperationKind::ListLength
                | PreludeOperationKind::SequenceLength
                | PreludeOperationKind::SequenceIndex
        ),
        PreludeLoweringTarget::NativeVector => matches!(
            operation,
            PreludeOperationKind::ListLength
                | PreludeOperationKind::SequenceLength
                | PreludeOperationKind::ListMap
                | PreludeOperationKind::SequenceMap
                | PreludeOperationKind::ListFold
                | PreludeOperationKind::SequenceFold
        ),
        PreludeLoweringTarget::GpuBatch | PreludeLoweringTarget::RemoteBatch => matches!(
            operation,
            PreludeOperationKind::ListMap
                | PreludeOperationKind::SequenceMap
                | PreludeOperationKind::ListFold
                | PreludeOperationKind::SequenceFold
        ),
    }
}

fn representation_for(
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    output_type: &str,
    proof_erased: bool,
    passport_erased: bool,
) -> String {
    let layout = match target {
        PreludeLoweringTarget::AuditOnly => "audit-report-only",
        PreludeLoweringTarget::Interpreter => "interpreter-small-step",
        PreludeLoweringTarget::NativeScalar => "native-scalar-op",
        PreludeLoweringTarget::NativeVector => "native-vector-loop",
        PreludeLoweringTarget::GpuBatch => "gpu-batch-kernel-candidate",
        PreludeLoweringTarget::RemoteBatch => "remote-batch-call-candidate",
    };
    format!(
        "{layout}<op={operation},out={output_type},proof_erased={proof_erased},passport_erased={passport_erased}>"
    )
}

fn contains_evidence_boundary(value: &PreludeEvalValue) -> bool {
    match value {
        PreludeEvalValue::Evidence { kind, .. } => is_forbidden_evidence_kind(kind),
        PreludeEvalValue::Product(lhs, rhs) => contains_evidence_boundary(lhs) || contains_evidence_boundary(rhs),
        PreludeEvalValue::OptionSome { value, .. } => contains_evidence_boundary(value),
        PreludeEvalValue::ResultOk { value, .. } => contains_evidence_boundary(value),
        PreludeEvalValue::ResultErr { error, .. } => contains_evidence_boundary(error),
        PreludeEvalValue::List { items, .. } | PreludeEvalValue::Sequence { items, .. } => {
            items.iter().any(contains_evidence_boundary)
        }
        PreludeEvalValue::Nat(_)
        | PreludeEvalValue::Bool(_)
        | PreludeEvalValue::Text(_)
        | PreludeEvalValue::OptionNone { .. }
        | PreludeEvalValue::FunctionRef { .. }
        | PreludeEvalValue::Symbolic { .. } => false,
    }
}

fn is_forbidden_evidence_kind(kind: &str) -> bool {
    matches!(
        kind,
        "Proof" | "ProofTerm" | "StaticProof" | "Theorem" | "TruthClaim" | "RuntimeWitness" | "ProofCertificate" | "EqProof"
    )
}

fn is_rejected(status: PreludeLoweringStatus) -> bool {
    matches!(
        status,
        PreludeLoweringStatus::RejectedEvaluation
            | PreludeLoweringStatus::RejectedTarget
            | PreludeLoweringStatus::RejectedEvidenceBoundary
    )
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
        return Err(prelude_lowering_error(
            line,
            format!("{label} must not be empty"),
            "lowering artifacts require stable names for audit and cache keys",
        ));
    }
    if value.chars().any(|ch| ch.is_whitespace()) {
        return Err(prelude_lowering_error(
            line,
            format!("{label} `{value}` must not contain whitespace"),
            "use a stable identifier such as prelude_nat_add_native",
        ));
    }
    Ok(())
}

fn prelude_lowering_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::PreludeLoweringError, Some(line), message.into()).with_help(help.into())
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
    format!("pl{:016x}", hash)
}
