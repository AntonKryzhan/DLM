use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::function_contract::{
    FunctionContractReport, FunctionContractStatus, FunctionPurity, FunctionTotality,
};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::sequence::{ListValueReport, SequenceValueReport};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TraversalStatus {
    VerifiedBounded,
    Downgraded,
    Open,
    RejectedFuelExceeded,
    RejectedContract,
}

impl fmt::Display for TraversalStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TraversalStatus::VerifiedBounded => write!(f, "verified_bounded"),
            TraversalStatus::Downgraded => write!(f, "downgraded"),
            TraversalStatus::Open => write!(f, "open"),
            TraversalStatus::RejectedFuelExceeded => write!(f, "rejected_fuel_exceeded"),
            TraversalStatus::RejectedContract => write!(f, "rejected_contract"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MapTraversalReport {
    pub source_kind: String,
    pub input_item_type: String,
    pub output_item_type: String,
    pub len: usize,
    pub fuel: usize,
    pub function_contract: String,
    pub status: TraversalStatus,
    pub result_collection_type: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FoldTraversalReport {
    pub source_kind: String,
    pub item_type: String,
    pub accumulator_type: String,
    pub len: usize,
    pub fuel: usize,
    pub step_contract: String,
    pub status: TraversalStatus,
    pub result_type: String,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn map_sequence(
    sequence: &SequenceValueReport,
    contract: &FunctionContractReport,
    output_item_type: impl Into<String>,
    fuel: usize,
    line: usize,
) -> Result<MapTraversalReport, Diagnostic> {
    map_collection(
        "Sequence",
        &sequence.item_type,
        sequence.len,
        sequence.max_trust,
        sequence.max_provenance,
        sequence.has_axiom_taint,
        sequence.has_oracle_taint,
        sequence.has_unsafe_taint,
        contract,
        output_item_type,
        fuel,
        line,
    )
}

pub fn map_list(
    list: &ListValueReport,
    contract: &FunctionContractReport,
    output_item_type: impl Into<String>,
    fuel: usize,
    line: usize,
) -> Result<MapTraversalReport, Diagnostic> {
    map_collection(
        "List",
        &list.item_type,
        list.len,
        list.max_trust,
        list.max_provenance,
        list.has_axiom_taint,
        list.has_oracle_taint,
        list.has_unsafe_taint,
        contract,
        output_item_type,
        fuel,
        line,
    )
}

pub fn fold_sequence(
    sequence: &SequenceValueReport,
    initial_accumulator: &Passport,
    accumulator_type: impl Into<String>,
    step_contract: &FunctionContractReport,
    fuel: usize,
    line: usize,
) -> Result<FoldTraversalReport, Diagnostic> {
    fold_collection(
        "Sequence",
        &sequence.item_type,
        sequence.len,
        sequence.max_trust,
        sequence.max_provenance,
        sequence.has_axiom_taint,
        sequence.has_oracle_taint,
        sequence.has_unsafe_taint,
        initial_accumulator,
        accumulator_type,
        step_contract,
        fuel,
        line,
    )
}

pub fn fold_list(
    list: &ListValueReport,
    initial_accumulator: &Passport,
    accumulator_type: impl Into<String>,
    step_contract: &FunctionContractReport,
    fuel: usize,
    line: usize,
) -> Result<FoldTraversalReport, Diagnostic> {
    fold_collection(
        "List",
        &list.item_type,
        list.len,
        list.max_trust,
        list.max_provenance,
        list.has_axiom_taint,
        list.has_oracle_taint,
        list.has_unsafe_taint,
        initial_accumulator,
        accumulator_type,
        step_contract,
        fuel,
        line,
    )
}

pub fn require_verified_bounded_map(report: &MapTraversalReport, line: usize) -> Result<(), Diagnostic> {
    if report.status == TraversalStatus::VerifiedBounded {
        Ok(())
    } else {
        Err(traversal_error(
            line,
            format!("map traversal is {}, not verified_bounded", report.status),
            "only verified_bounded traversals may be used as certified optimizer assumptions",
        ))
    }
}

pub fn require_verified_bounded_fold(report: &FoldTraversalReport, line: usize) -> Result<(), Diagnostic> {
    if report.status == TraversalStatus::VerifiedBounded {
        Ok(())
    } else {
        Err(traversal_error(
            line,
            format!("fold traversal is {}, not verified_bounded", report.status),
            "only verified_bounded folds may be used as certified total traversal assumptions",
        ))
    }
}

pub fn map_traversal_passport(theory: &str, report: &MapTraversalReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::MapTraversal {
            source: report.source_kind.clone(),
            function: report.function_contract.clone(),
            result: report.result_collection_type.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: traversal_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "traversal:map"),
        location: LocationContext::local(),
    }
}

pub fn fold_traversal_passport(theory: &str, report: &FoldTraversalReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::FoldTraversal {
            source: report.source_kind.clone(),
            step: report.step_contract.clone(),
            result: report.result_type.clone(),
            fuel: report.fuel,
        },
        construction: ConstructionMode::Definable,
        capabilities: traversal_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "traversal:fold"),
        location: LocationContext::local(),
    }
}

pub fn traversal_report_passport(theory: &str, subject: impl Into<String>, status: TraversalStatus, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::TraversalReport {
            subject: subject.into(),
            status: status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: traversal_caps(),
        cost: CostClass::SmallFinite,
        trust: taint_summary(sources).0,
        provenance: taint_summary(sources).1,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "traversal:report"),
        location: LocationContext::local(),
    }
}

pub fn export_map_traversal(report: &MapTraversalReport) -> String {
    let mut out = String::new();
    out.push_str("map_traversal_report: v1\n");
    out.push_str(&format!("source_kind: {}\n", report.source_kind));
    out.push_str(&format!("input_item_type: {}\n", report.input_item_type));
    out.push_str(&format!("output_item_type: {}\n", report.output_item_type));
    out.push_str(&format!("len: {}\n", report.len));
    out.push_str(&format!("fuel: {}\n", report.fuel));
    out.push_str(&format!("function_contract: {}\n", report.function_contract));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("result_collection_type: {}\n", report.result_collection_type));
    out.push_str("open_obligations:\n");
    for obligation in &report.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("trust: {:?}\n", report.max_trust));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

pub fn export_fold_traversal(report: &FoldTraversalReport) -> String {
    let mut out = String::new();
    out.push_str("fold_traversal_report: v1\n");
    out.push_str(&format!("source_kind: {}\n", report.source_kind));
    out.push_str(&format!("item_type: {}\n", report.item_type));
    out.push_str(&format!("accumulator_type: {}\n", report.accumulator_type));
    out.push_str(&format!("len: {}\n", report.len));
    out.push_str(&format!("fuel: {}\n", report.fuel));
    out.push_str(&format!("step_contract: {}\n", report.step_contract));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("result_type: {}\n", report.result_type));
    out.push_str("open_obligations:\n");
    for obligation in &report.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("trust: {:?}\n", report.max_trust));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

#[allow(clippy::too_many_arguments)]
fn map_collection(
    source_kind: &str,
    input_item_type: &str,
    len: usize,
    source_trust: TrustLevel,
    source_provenance: Provenance,
    source_axiom: bool,
    source_oracle: bool,
    source_unsafe: bool,
    contract: &FunctionContractReport,
    output_item_type: impl Into<String>,
    fuel: usize,
    line: usize,
) -> Result<MapTraversalReport, Diagnostic> {
    let output_item_type = output_item_type.into();
    validate_type_text(&output_item_type, "map output item type", line)?;
    validate_contract_domain(contract, input_item_type, "map", line)?;
    if contract.codomain != output_item_type {
        return Err(traversal_error(
            line,
            format!(
                "map output type mismatch: contract `{}` returns `{}`, requested `{}`",
                contract.name, contract.codomain, output_item_type
            ),
            "map traversal output must match the function contract codomain exactly",
        ));
    }

    let mut obligations = Vec::new();
    let status = traversal_status_from_contract(contract, fuel, len, "map", &mut obligations);
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = merge_taint(
        source_trust,
        source_provenance,
        source_axiom,
        source_oracle,
        source_unsafe,
        contract,
    );
    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        obligations.push("traversal depends on Axiom/Oracle/Unsafe taint and cannot be treated as clean Checked traversal".to_string());
    }
    obligations.sort();
    obligations.dedup();

    let result_collection_type = format!("{source_kind}<{}>", output_item_type);
    let fingerprint = stable_fingerprint(&[
        "map-traversal-v1".to_string(),
        source_kind.to_string(),
        input_item_type.to_string(),
        output_item_type.clone(),
        format!("len={len}"),
        format!("fuel={fuel}"),
        contract.name.clone(),
        status.to_string(),
        format!("trust={max_trust:?}"),
    ]);

    Ok(MapTraversalReport {
        source_kind: source_kind.to_string(),
        input_item_type: input_item_type.to_string(),
        output_item_type,
        len,
        fuel,
        function_contract: contract.name.clone(),
        status,
        result_collection_type,
        open_obligations: obligations,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

#[allow(clippy::too_many_arguments)]
fn fold_collection(
    source_kind: &str,
    item_type: &str,
    len: usize,
    source_trust: TrustLevel,
    source_provenance: Provenance,
    source_axiom: bool,
    source_oracle: bool,
    source_unsafe: bool,
    initial_accumulator: &Passport,
    accumulator_type: impl Into<String>,
    step_contract: &FunctionContractReport,
    fuel: usize,
    line: usize,
) -> Result<FoldTraversalReport, Diagnostic> {
    let accumulator_type = accumulator_type.into();
    validate_type_text(&accumulator_type, "fold accumulator type", line)?;
    let actual_accumulator = ordinary_traversal_value_type(initial_accumulator, line)?;
    if actual_accumulator != accumulator_type {
        return Err(traversal_error(
            line,
            format!("fold accumulator mismatch: expected `{accumulator_type}`, got `{actual_accumulator}`"),
            "fold initial accumulator must match the declared accumulator type exactly",
        ));
    }
    let expected_domain = format!("ProductType<{}*{}>", accumulator_type, item_type);
    validate_contract_domain(step_contract, &expected_domain, "fold", line)?;
    if step_contract.codomain != accumulator_type {
        return Err(traversal_error(
            line,
            format!(
                "fold step codomain mismatch: expected `{accumulator_type}`, got `{}`",
                step_contract.codomain
            ),
            "fold step must return the accumulator type so traversal remains structurally bounded",
        ));
    }

    let mut obligations = Vec::new();
    let status = traversal_status_from_contract(step_contract, fuel, len, "fold", &mut obligations);
    let (mut max_trust, mut max_provenance, mut has_axiom_taint, mut has_oracle_taint, mut has_unsafe_taint) = merge_taint(
        source_trust,
        source_provenance,
        source_axiom,
        source_oracle,
        source_unsafe,
        step_contract,
    );
    max_trust = max_trust.max(initial_accumulator.trust);
    max_provenance = max_provenance.max(initial_accumulator.provenance);
    has_axiom_taint |= initial_accumulator.trust >= TrustLevel::Axiom;
    has_oracle_taint |= initial_accumulator.trust >= TrustLevel::Oracle
        || initial_accumulator.provenance == Provenance::OracleInput;
    has_unsafe_taint |= initial_accumulator.trust >= TrustLevel::Unsafe
        || initial_accumulator.provenance == Provenance::UnsafeExternal;
    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        obligations.push("traversal depends on Axiom/Oracle/Unsafe taint and cannot be treated as clean Checked traversal".to_string());
    }
    obligations.sort();
    obligations.dedup();

    let fingerprint = stable_fingerprint(&[
        "fold-traversal-v1".to_string(),
        source_kind.to_string(),
        item_type.to_string(),
        accumulator_type.clone(),
        format!("len={len}"),
        format!("fuel={fuel}"),
        step_contract.name.clone(),
        status.to_string(),
        format!("trust={max_trust:?}"),
    ]);

    Ok(FoldTraversalReport {
        source_kind: source_kind.to_string(),
        item_type: item_type.to_string(),
        accumulator_type: accumulator_type.clone(),
        len,
        fuel,
        step_contract: step_contract.name.clone(),
        status,
        result_type: accumulator_type,
        open_obligations: obligations,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

fn traversal_status_from_contract(
    contract: &FunctionContractReport,
    fuel: usize,
    len: usize,
    what: &str,
    obligations: &mut Vec<String>,
) -> TraversalStatus {
    if fuel < len {
        obligations.push(format!(
            "{what} traversal has fuel {fuel}, but source length is {len}; no implicit unbounded recursion is allowed"
        ));
        return TraversalStatus::RejectedFuelExceeded;
    }
    match contract.status {
        FunctionContractStatus::Verified => {
            if contract.purity == FunctionPurity::Pure
                && contract.totality == FunctionTotality::Total
                && contract.effects.is_empty()
                && !contract.has_axiom_taint
                && !contract.has_oracle_taint
                && !contract.has_unsafe_taint
            {
                TraversalStatus::VerifiedBounded
            } else {
                obligations.push("function contract is verified but carries non-clean purity/totality/taint details; traversal is downgraded".to_string());
                TraversalStatus::Downgraded
            }
        }
        FunctionContractStatus::Downgraded => {
            obligations.push("function contract is downgraded; traversal cannot be promoted to verified_bounded".to_string());
            TraversalStatus::Downgraded
        }
        FunctionContractStatus::Open => {
            obligations.push("function contract is open; traversal remains open until its obligations close".to_string());
            TraversalStatus::Open
        }
        FunctionContractStatus::Rejected => {
            obligations.push("function contract is rejected; traversal is rejected without executing hidden fallback logic".to_string());
            TraversalStatus::RejectedContract
        }
    }
}

fn validate_contract_domain(
    contract: &FunctionContractReport,
    expected_domain: &str,
    what: &str,
    line: usize,
) -> Result<(), Diagnostic> {
    if contract.domain == expected_domain {
        Ok(())
    } else {
        Err(traversal_error(
            line,
            format!(
                "{what} contract domain mismatch: expected `{expected_domain}`, got `{}`",
                contract.domain
            ),
            "traversal does not insert implicit coercions; the function contract domain must match the collection item/accumulator product type exactly",
        ))
    }
}

fn ordinary_traversal_value_type(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::Nat
        | TypeKind::Bool
        | TypeKind::Bytes
        | TypeKind::Text
        | TypeKind::Infinity { .. }
        | TypeKind::Universe { .. }
        | TypeKind::Set { .. }
        | TypeKind::Class { .. }
        | TypeKind::Language { .. }
        | TypeKind::Encoding { .. }
        | TypeKind::MetaLevel { .. }
        | TypeKind::BigNat { .. }
        | TypeKind::FunctionType { .. }
        | TypeKind::LambdaTerm { .. }
        | TypeKind::ApplicationTerm { .. }
        | TypeKind::ProductTerm { .. }
        | TypeKind::SumInjection { .. }
        | TypeKind::RecordTerm { .. }
        | TypeKind::RecordProjection { .. }
        | TypeKind::ProductElimination { .. }
        | TypeKind::SumElimination { .. }
        | TypeKind::RecordPattern { .. }
        | TypeKind::OptionValue { .. }
        | TypeKind::ResultValue { .. }
        | TypeKind::ListValue { .. }
        | TypeKind::SequenceValue { .. }
        | TypeKind::SequenceIndex { .. }
        | TypeKind::MapTraversal { .. }
        | TypeKind::FoldTraversal { .. }
        | TypeKind::TraversalReport { .. } => Ok(passport.ty.to_string()),
        TypeKind::Theorem { .. }
        | TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. }
        | TypeKind::EqProof { .. }
        | TypeKind::RewriteRule { .. }
        | TypeKind::RewriteCertificate { .. } => Err(traversal_error(
            line,
            format!("{} is not an ordinary traversal value", passport.ty),
            "traversal must not silently consume proof, theorem, truth, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(traversal_error(
            line,
            format!("{} is not accepted as an ordinary traversal value in this MVP", passport.ty),
            "extend traversal value classification explicitly when a new value class becomes safe for fold/map",
        )),
    }
}

fn merge_taint(
    source_trust: TrustLevel,
    source_provenance: Provenance,
    source_axiom: bool,
    source_oracle: bool,
    source_unsafe: bool,
    contract: &FunctionContractReport,
) -> (TrustLevel, Provenance, bool, bool, bool) {
    (
        source_trust.max(contract.max_trust),
        source_provenance.max(contract.max_provenance),
        source_axiom || contract.has_axiom_taint,
        source_oracle || contract.has_oracle_taint,
        source_unsafe || contract.has_unsafe_taint,
    )
}

fn taint_summary(sources: &[&Passport]) -> (TrustLevel, Provenance) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
    }
    (max_trust, max_provenance)
}

fn validate_type_text(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(traversal_error(
            line,
            format!("{what} cannot be empty"),
            "traversal types must be explicit; no implicit Any/Unknown traversal type is inserted",
        ));
    }
    Ok(())
}

fn traversal_caps() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanSerializeForMigration,
        Capability::CanCompilePortableCode,
    ])
}

fn merge_history(sources: &[&Passport], event: &str) -> HistoryChain {
    HistoryChain::merge_many(sources.iter().map(|p| &p.history), event)
}

fn stable_fingerprint(parts: &[String]) -> String {
    let mut state: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            state ^= *byte as u64;
            state = state.wrapping_mul(0x100000001b3);
        }
        state ^= 0xff;
        state = state.wrapping_mul(0x100000001b3);
    }
    format!("traversal-{state:016x}")
}

fn traversal_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::TraversalError, Some(line), message).with_help(help)
}
