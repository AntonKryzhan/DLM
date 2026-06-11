use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum OptionValueKind {
    Some,
    None,
}

impl fmt::Display for OptionValueKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            OptionValueKind::Some => write!(f, "some"),
            OptionValueKind::None => write!(f, "none"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ResultValueKind {
    Ok,
    Err,
}

impl fmt::Display for ResultValueKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ResultValueKind::Ok => write!(f, "ok"),
            ResultValueKind::Err => write!(f, "err"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PartialityStatus {
    Total,
    Optional,
    ErrorCarrying,
    Open,
    Rejected,
}

impl fmt::Display for PartialityStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PartialityStatus::Total => write!(f, "total"),
            PartialityStatus::Optional => write!(f, "optional"),
            PartialityStatus::ErrorCarrying => write!(f, "error_carrying"),
            PartialityStatus::Open => write!(f, "open"),
            PartialityStatus::Rejected => write!(f, "rejected"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OptionTypeReport {
    pub item_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OptionValueReport {
    pub kind: OptionValueKind,
    pub item_type: String,
    pub value: Option<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResultTypeReport {
    pub ok_type: String,
    pub err_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResultValueReport {
    pub kind: ResultValueKind,
    pub value_type: String,
    pub value: String,
    pub result_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PartialityReport {
    pub subject: String,
    pub status: PartialityStatus,
    pub explanation: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn option_type(
    item_type: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<OptionTypeReport, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "option item type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "option-type-v1".to_string(),
        item_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(OptionTypeReport { item_type, max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint, fingerprint })
}

pub fn option_some(
    option: &OptionTypeReport,
    value: &Passport,
    line: usize,
) -> Result<OptionValueReport, Diagnostic> {
    let value_type = ordinary_partiality_value_type(value, line)?;
    if value_type != option.item_type {
        return Err(partiality_error(
            line,
            format!("Option::some type mismatch: expected `{}`, got `{value_type}`", option.item_type),
            "some(value) must carry a value of exactly the declared option item type",
        ));
    }
    let sources = [value];
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = merge_with_report(option.max_trust, option.max_provenance, option.has_axiom_taint, option.has_oracle_taint, option.has_unsafe_taint, &sources);
    let fingerprint = stable_fingerprint(&[
        "option-some-v1".to_string(),
        option.item_type.clone(),
        value.ty.to_string(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(OptionValueReport { kind: OptionValueKind::Some, item_type: option.item_type.clone(), value: Some(value.ty.to_string()), max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint, fingerprint })
}

pub fn option_none(option: &OptionTypeReport, line: usize) -> Result<OptionValueReport, Diagnostic> {
    validate_type_text(&option.item_type, "option none item type", line)?;
    let fingerprint = stable_fingerprint(&[
        "option-none-v1".to_string(),
        option.item_type.clone(),
        format!("trust={:?}", option.max_trust),
    ]);
    Ok(OptionValueReport {
        kind: OptionValueKind::None,
        item_type: option.item_type.clone(),
        value: None,
        max_trust: option.max_trust,
        max_provenance: option.max_provenance,
        has_axiom_taint: option.has_axiom_taint,
        has_oracle_taint: option.has_oracle_taint,
        has_unsafe_taint: option.has_unsafe_taint,
        fingerprint,
    })
}

pub fn result_type(
    ok_type: impl Into<String>,
    err_type: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<ResultTypeReport, Diagnostic> {
    let ok_type = ok_type.into();
    let err_type = err_type.into();
    validate_type_text(&ok_type, "result ok type", line)?;
    validate_type_text(&err_type, "result err type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "result-type-v1".to_string(),
        ok_type.clone(),
        err_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ResultTypeReport { ok_type, err_type, max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint, fingerprint })
}

pub fn result_ok(result: &ResultTypeReport, value: &Passport, line: usize) -> Result<ResultValueReport, Diagnostic> {
    result_value(result, ResultValueKind::Ok, value, line)
}

pub fn result_err(result: &ResultTypeReport, value: &Passport, line: usize) -> Result<ResultValueReport, Diagnostic> {
    result_value(result, ResultValueKind::Err, value, line)
}

pub fn partiality_report(
    subject: &Passport,
    status: PartialityStatus,
    explanation: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<PartialityReport, Diagnostic> {
    let explanation = explanation.into();
    if explanation.trim().is_empty() {
        return Err(partiality_error(
            line,
            "partiality report requires an explanation",
            "partial/open/error-carrying computation must explain why it is not represented as a total value",
        ));
    }
    reject_proof_like(subject, "partiality subject", line)?;
    let mut all_sources = Vec::with_capacity(sources.len() + 1);
    all_sources.push(subject);
    all_sources.extend_from_slice(sources);
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(&all_sources);
    let fingerprint = stable_fingerprint(&[
        "partiality-report-v1".to_string(),
        subject.ty.to_string(),
        status.to_string(),
        explanation.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(PartialityReport { subject: subject.ty.to_string(), status, explanation, max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint, fingerprint })
}

pub fn option_type_passport(theory: &str, report: &OptionTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::OptionType { item: report.item_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: partiality_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "partiality:option_type"),
        location: LocationContext::local(),
    }
}

pub fn option_value_passport(theory: &str, report: &OptionValueReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::OptionValue { kind: report.kind.to_string(), item: report.item_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: partiality_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "partiality:option_value"),
        location: LocationContext::local(),
    }
}

pub fn result_type_passport(theory: &str, report: &ResultTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ResultTypeValue { ok: report.ok_type.clone(), err: report.err_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: partiality_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "partiality:result_type"),
        location: LocationContext::local(),
    }
}

pub fn result_value_passport(theory: &str, report: &ResultValueReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ResultValue { kind: report.kind.to_string(), value: report.value.clone(), result_type: report.result_type.clone() },
        construction: ConstructionMode::Definable,
        capabilities: partiality_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "partiality:result_value"),
        location: LocationContext::local(),
    }
}

pub fn partiality_report_passport(theory: &str, report: &PartialityReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::PartialityReport { subject: report.subject.clone(), status: report.status.to_string() },
        construction: ConstructionMode::Definable,
        capabilities: partiality_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "partiality:report"),
        location: LocationContext::local(),
    }
}

pub fn export_option_type(report: &OptionTypeReport) -> String {
    format!(
        "option_type_report: v1\nitem_type: {}\ntrust: {:?}\naxiom_taint: {}\noracle_taint: {}\nunsafe_taint: {}\nfingerprint: {}\n",
        report.item_type, report.max_trust, report.has_axiom_taint, report.has_oracle_taint, report.has_unsafe_taint, report.fingerprint
    )
}

pub fn export_option_value(report: &OptionValueReport) -> String {
    format!(
        "option_value_report: v1\nkind: {}\nitem_type: {}\nvalue: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.kind,
        report.item_type,
        report.value.clone().unwrap_or_else(|| "<none>".to_string()),
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_result_type(report: &ResultTypeReport) -> String {
    format!(
        "result_type_report: v1\nok_type: {}\nerr_type: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.ok_type, report.err_type, report.max_trust, report.fingerprint
    )
}

pub fn export_result_value(report: &ResultValueReport) -> String {
    format!(
        "result_value_report: v1\nkind: {}\nvalue_type: {}\nvalue: {}\nresult_type: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.kind, report.value_type, report.value, report.result_type, report.max_trust, report.fingerprint
    )
}

pub fn export_partiality_report(report: &PartialityReport) -> String {
    format!(
        "partiality_report: v1\nsubject: {}\nstatus: {}\nexplanation: {}\ntrust: {:?}\naxiom_taint: {}\noracle_taint: {}\nunsafe_taint: {}\nfingerprint: {}\n",
        report.subject,
        report.status,
        report.explanation,
        report.max_trust,
        report.has_axiom_taint,
        report.has_oracle_taint,
        report.has_unsafe_taint,
        report.fingerprint
    )
}

fn result_value(result: &ResultTypeReport, kind: ResultValueKind, value: &Passport, line: usize) -> Result<ResultValueReport, Diagnostic> {
    let value_type = ordinary_partiality_value_type(value, line)?;
    let expected = match kind {
        ResultValueKind::Ok => &result.ok_type,
        ResultValueKind::Err => &result.err_type,
    };
    if &value_type != expected {
        return Err(partiality_error(
            line,
            format!("Result::{kind} type mismatch: expected `{expected}`, got `{value_type}`"),
            "result branch values must match exactly the declared ok/err type",
        ));
    }
    let sources = [value];
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = merge_with_report(result.max_trust, result.max_provenance, result.has_axiom_taint, result.has_oracle_taint, result.has_unsafe_taint, &sources);
    let result_type = format!("{},{}", result.ok_type, result.err_type);
    let fingerprint = stable_fingerprint(&[
        "result-value-v1".to_string(),
        kind.to_string(),
        value_type.clone(),
        value.ty.to_string(),
        result_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ResultValueReport { kind, value_type, value: value.ty.to_string(), result_type, max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint, fingerprint })
}

fn ordinary_partiality_value_type(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
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
        | TypeKind::ResultValue { .. } => Ok(passport.ty.to_string()),
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
        | TypeKind::RewriteCertificate { .. } => Err(partiality_error(
            line,
            format!("{} is not an ordinary Option/Result value", passport.ty),
            "partiality values must not silently consume proof, theorem, truth, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(partiality_error(
            line,
            format!("{} is not accepted as an ordinary Option/Result value in this MVP", passport.ty),
            "extend the partiality whitelist explicitly when a new value class becomes safe for Option/Result",
        )),
    }
}

fn reject_proof_like(passport: &Passport, what: &str, line: usize) -> Result<(), Diagnostic> {
    match passport.ty {
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
        | TypeKind::RewriteCertificate { .. } => Err(partiality_error(
            line,
            format!("{what} `{}` is proof/truth/runtime-like and cannot be reported as ordinary partial value", passport.ty),
            "partiality reports classify ordinary computation boundaries, not proof/truth evidence",
        )),
        _ => Ok(()),
    }
}

fn validate_type_text(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    let value = value.trim();
    if value.is_empty() {
        return Err(partiality_error(
            line,
            format!("{what} cannot be empty"),
            "Option/Result type descriptors must be explicit; no implicit Any/Unknown partiality type is inserted",
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
    let (source_trust, source_provenance, source_axiom, source_oracle, source_unsafe) = taint_summary(sources);
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
        has_oracle_taint |= source.trust >= TrustLevel::Oracle || source.provenance == Provenance::OracleInput;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe || source.provenance == Provenance::UnsafeExternal;
    }
    (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint)
}

fn partiality_caps() -> CapabilitySet {
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
    format!("partiality-{state:016x}")
}

fn partiality_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::PartialityTypeError, Some(line), message).with_help(help)
}
