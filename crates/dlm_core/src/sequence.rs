use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SequenceIndexStatus {
    InBounds,
    OutOfBounds,
}

impl fmt::Display for SequenceIndexStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SequenceIndexStatus::InBounds => write!(f, "in_bounds"),
            SequenceIndexStatus::OutOfBounds => write!(f, "out_of_bounds"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ListTypeReport {
    pub item_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ListValueReport {
    pub item_type: String,
    pub len: usize,
    pub items: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SequenceTypeReport {
    pub item_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SequenceValueReport {
    pub item_type: String,
    pub len: usize,
    pub items: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SequenceIndexReport {
    pub sequence_type: String,
    pub index: usize,
    pub status: SequenceIndexStatus,
    pub result_type: String,
    pub value: Option<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn list_type(
    item_type: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<ListTypeReport, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "list item type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "list-type-v1".to_string(),
        item_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ListTypeReport {
        item_type,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn list_value(
    list: &ListTypeReport,
    items: &[&Passport],
    line: usize,
) -> Result<ListValueReport, Diagnostic> {
    let item_strings = checked_items(&list.item_type, items, "list", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        merge_with_report(
            list.max_trust,
            list.max_provenance,
            list.has_axiom_taint,
            list.has_oracle_taint,
            list.has_unsafe_taint,
            items,
        );
    let fingerprint = fingerprint_with_items(
        "list-value-v1",
        &list.item_type,
        item_strings.len(),
        &item_strings,
        max_trust,
    );
    Ok(ListValueReport {
        item_type: list.item_type.clone(),
        len: item_strings.len(),
        items: item_strings,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sequence_type(
    item_type: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<SequenceTypeReport, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "sequence item type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "sequence-type-v1".to_string(),
        item_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(SequenceTypeReport {
        item_type,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sequence_value(
    sequence: &SequenceTypeReport,
    items: &[&Passport],
    line: usize,
) -> Result<SequenceValueReport, Diagnostic> {
    let item_strings = checked_items(&sequence.item_type, items, "sequence", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        merge_with_report(
            sequence.max_trust,
            sequence.max_provenance,
            sequence.has_axiom_taint,
            sequence.has_oracle_taint,
            sequence.has_unsafe_taint,
            items,
        );
    let fingerprint = fingerprint_with_items(
        "sequence-value-v1",
        &sequence.item_type,
        item_strings.len(),
        &item_strings,
        max_trust,
    );
    Ok(SequenceValueReport {
        item_type: sequence.item_type.clone(),
        len: item_strings.len(),
        items: item_strings,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sequence_index(
    sequence: &SequenceValueReport,
    index: usize,
    line: usize,
) -> Result<SequenceIndexReport, Diagnostic> {
    validate_type_text(&sequence.item_type, "sequence index item type", line)?;
    let status = if index < sequence.len {
        SequenceIndexStatus::InBounds
    } else {
        SequenceIndexStatus::OutOfBounds
    };
    let value = if status == SequenceIndexStatus::InBounds {
        Some(sequence.items[index].clone())
    } else {
        None
    };
    let result_type = format!("Option<{}>", sequence.item_type);
    let fingerprint = stable_fingerprint(&[
        "sequence-index-v1".to_string(),
        sequence.item_type.clone(),
        sequence.len.to_string(),
        index.to_string(),
        status.to_string(),
        result_type.clone(),
        value.clone().unwrap_or_else(|| "<none>".to_string()),
        format!("trust={:?}", sequence.max_trust),
    ]);
    Ok(SequenceIndexReport {
        sequence_type: format!("Sequence<{}>", sequence.item_type),
        index,
        status,
        result_type,
        value,
        max_trust: sequence.max_trust,
        max_provenance: sequence.max_provenance,
        has_axiom_taint: sequence.has_axiom_taint,
        has_oracle_taint: sequence.has_oracle_taint,
        has_unsafe_taint: sequence.has_unsafe_taint,
        fingerprint,
    })
}

pub fn list_type_passport(theory: &str, report: &ListTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ListType { item: report.item_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: sequence_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "sequence:list_type"),
        location: LocationContext::local(),
    }
}

pub fn list_value_passport(theory: &str, report: &ListValueReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ListValue { item: report.item_type.clone(), len: report.len },
        construction: ConstructionMode::Definable,
        capabilities: sequence_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "sequence:list_value"),
        location: LocationContext::local(),
    }
}

pub fn sequence_type_passport(theory: &str, report: &SequenceTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::SequenceType { item: report.item_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: sequence_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "sequence:sequence_type"),
        location: LocationContext::local(),
    }
}

pub fn sequence_value_passport(theory: &str, report: &SequenceValueReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::SequenceValue { item: report.item_type.clone(), len: report.len },
        construction: ConstructionMode::Definable,
        capabilities: sequence_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "sequence:sequence_value"),
        location: LocationContext::local(),
    }
}

pub fn sequence_index_passport(theory: &str, report: &SequenceIndexReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::SequenceIndex {
            sequence: report.sequence_type.clone(),
            index: report.index,
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: sequence_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "sequence:index"),
        location: LocationContext::local(),
    }
}

pub fn export_list_type(report: &ListTypeReport) -> String {
    format!(
        "list_type_report: v1\nitem_type: {}\ntrust: {:?}\naxiom_taint: {}\noracle_taint: {}\nunsafe_taint: {}\nfingerprint: {}\n",
        report.item_type,
        report.max_trust,
        report.has_axiom_taint,
        report.has_oracle_taint,
        report.has_unsafe_taint,
        report.fingerprint
    )
}

pub fn export_list_value(report: &ListValueReport) -> String {
    format!(
        "list_value_report: v1\nitem_type: {}\nlen: {}\nitems: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.item_type,
        report.len,
        report.items.join(" | "),
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_sequence_type(report: &SequenceTypeReport) -> String {
    format!(
        "sequence_type_report: v1\nitem_type: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.item_type, report.max_trust, report.fingerprint
    )
}

pub fn export_sequence_value(report: &SequenceValueReport) -> String {
    format!(
        "sequence_value_report: v1\nitem_type: {}\nlen: {}\nitems: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.item_type,
        report.len,
        report.items.join(" | "),
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_sequence_index(report: &SequenceIndexReport) -> String {
    format!(
        "sequence_index_report: v1\nsequence_type: {}\nindex: {}\nstatus: {}\nresult_type: {}\nvalue: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.sequence_type,
        report.index,
        report.status,
        report.result_type,
        report.value.clone().unwrap_or_else(|| "<none>".to_string()),
        report.max_trust,
        report.fingerprint
    )
}

fn checked_items(
    expected_type: &str,
    items: &[&Passport],
    what: &str,
    line: usize,
) -> Result<Vec<String>, Diagnostic> {
    let mut item_strings = Vec::with_capacity(items.len());
    for (index, item) in items.iter().enumerate() {
        let actual = ordinary_sequence_value_type(item, line)?;
        if actual != expected_type {
            return Err(sequence_error(
                line,
                format!("{what} item #{index} type mismatch: expected `{expected_type}`, got `{actual}`"),
                "List/Sequence items must match exactly the declared item type; no implicit coercion or Any is inserted",
            ));
        }
        item_strings.push(item.ty.to_string());
    }
    Ok(item_strings)
}

fn ordinary_sequence_value_type(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
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
        | TypeKind::SequenceIndex { .. } => Ok(passport.ty.to_string()),
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
        | TypeKind::RewriteCertificate { .. } => Err(sequence_error(
            line,
            format!("{} is not an ordinary List/Sequence value", passport.ty),
            "finite collections must not silently consume proof, theorem, truth, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(sequence_error(
            line,
            format!("{} is not accepted as an ordinary List/Sequence value in this MVP", passport.ty),
            "extend the sequence whitelist explicitly when a new value class becomes safe for finite collections",
        )),
    }
}

fn validate_type_text(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(sequence_error(
            line,
            format!("{what} cannot be empty"),
            "List/Sequence type descriptors must be explicit; no implicit Any/Unknown collection type is inserted",
        ));
    }
    Ok(())
}

fn merge_with_report(
    trust: TrustLevel,
    provenance: Provenance,
    axiom: bool,
    oracle: bool,
    unsafe_taint: bool,
    sources: &[&Passport],
) -> (TrustLevel, Provenance, bool, bool, bool) {
    let (source_trust, source_provenance, source_axiom, source_oracle, source_unsafe) =
        taint_summary(sources);
    (
        trust.max(source_trust),
        provenance.max(source_provenance),
        axiom || source_axiom,
        oracle || source_oracle,
        unsafe_taint || source_unsafe,
    )
}

fn taint_summary(sources: &[&Passport]) -> (TrustLevel, Provenance, bool, bool, bool) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    let mut has_axiom_taint = false;
    let mut has_oracle_taint = false;
    let mut has_unsafe_taint = false;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
        has_axiom_taint |= source.trust >= TrustLevel::Axiom;
        has_oracle_taint |= source.trust >= TrustLevel::Oracle
            || source.provenance == Provenance::OracleInput;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe
            || source.provenance == Provenance::UnsafeExternal;
    }
    (
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
    )
}

fn sequence_caps() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanSerializeForMigration,
        Capability::CanCompilePortableCode,
    ])
}

fn merge_history(sources: &[&Passport], event: &str) -> HistoryChain {
    HistoryChain::merge_many(sources.iter().map(|p| &p.history), event)
}

fn fingerprint_with_items(
    prefix: &str,
    item_type: &str,
    len: usize,
    items: &[String],
    trust: TrustLevel,
) -> String {
    let mut parts = Vec::with_capacity(items.len() + 4);
    parts.push(prefix.to_string());
    parts.push(item_type.to_string());
    parts.push(format!("len={len}"));
    parts.extend(items.iter().cloned());
    parts.push(format!("trust={trust:?}"));
    stable_fingerprint(&parts)
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
    format!("sequence-{state:016x}")
}

fn sequence_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::SequenceTypeError, Some(line), message).with_help(help)
}
