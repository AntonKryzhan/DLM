use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SumInjectionSide {
    Left,
    Right,
}

impl fmt::Display for SumInjectionSide {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SumInjectionSide::Left => write!(f, "left"),
            SumInjectionSide::Right => write!(f, "right"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProductTypeReport {
    pub lhs: String,
    pub rhs: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProductTermReport {
    pub lhs: String,
    pub rhs: String,
    pub product_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SumTypeReport {
    pub left: String,
    pub right: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SumInjectionReport {
    pub side: SumInjectionSide,
    pub value: String,
    pub value_type: String,
    pub sum_type: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordFieldDecl {
    pub name: String,
    pub ty: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordFieldValue {
    pub name: String,
    pub ty: String,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordTypeReport {
    pub name: String,
    pub fields: Vec<RecordFieldDecl>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordTermReport {
    pub name: String,
    pub fields: Vec<RecordFieldValue>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordProjectionReport {
    pub record: String,
    pub field: String,
    pub result_type: String,
    pub result_value: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn record_field_decl(
    name: impl Into<String>,
    ty: impl Into<String>,
    line: usize,
) -> Result<RecordFieldDecl, Diagnostic> {
    let name = name.into();
    let ty = ty.into();
    validate_identifier(&name, "record field name", line)?;
    validate_type_text(&ty, "record field type", line)?;
    Ok(RecordFieldDecl { name, ty })
}

pub fn record_field_value(
    name: impl Into<String>,
    value: &Passport,
    line: usize,
) -> Result<RecordFieldValue, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "record field name", line)?;
    let ty = structural_value_type(value, line)?;
    Ok(RecordFieldValue {
        name,
        ty,
        value: value.ty.to_string(),
    })
}

pub fn product_type(
    lhs: impl Into<String>,
    rhs: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<ProductTypeReport, Diagnostic> {
    let lhs = lhs.into();
    let rhs = rhs.into();
    validate_type_text(&lhs, "product lhs type", line)?;
    validate_type_text(&rhs, "product rhs type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "product-type-v1".to_string(),
        lhs.clone(),
        rhs.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ProductTypeReport {
        lhs,
        rhs,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn product_term(
    expected: &ProductTypeReport,
    lhs: &Passport,
    rhs: &Passport,
    line: usize,
) -> Result<ProductTermReport, Diagnostic> {
    let lhs_type = structural_value_type(lhs, line)?;
    let rhs_type = structural_value_type(rhs, line)?;
    if lhs_type != expected.lhs {
        return Err(structural_error(
            line,
            format!("product lhs type mismatch: expected `{}`, got `{lhs_type}`", expected.lhs),
            "product construction must preserve the declared left component type",
        ));
    }
    if rhs_type != expected.rhs {
        return Err(structural_error(
            line,
            format!("product rhs type mismatch: expected `{}`, got `{rhs_type}`", expected.rhs),
            "product construction must preserve the declared right component type",
        ));
    }
    let sources = [lhs, rhs];
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(&sources);
    let max_trust = max_trust.max(expected.max_trust);
    let max_provenance = max_provenance.max(expected.max_provenance);
    let has_axiom_taint = has_axiom_taint || expected.has_axiom_taint;
    let has_oracle_taint = has_oracle_taint || expected.has_oracle_taint;
    let has_unsafe_taint = has_unsafe_taint || expected.has_unsafe_taint;
    let product_type = format!("{}*{}", expected.lhs, expected.rhs);
    let fingerprint = stable_fingerprint(&[
        "product-term-v1".to_string(),
        product_type.clone(),
        lhs.ty.to_string(),
        rhs.ty.to_string(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ProductTermReport {
        lhs: lhs.ty.to_string(),
        rhs: rhs.ty.to_string(),
        product_type,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sum_type(
    left: impl Into<String>,
    right: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<SumTypeReport, Diagnostic> {
    let left = left.into();
    let right = right.into();
    validate_type_text(&left, "sum left type", line)?;
    validate_type_text(&right, "sum right type", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "sum-type-v1".to_string(),
        left.clone(),
        right.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(SumTypeReport {
        left,
        right,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sum_injection(
    sum: &SumTypeReport,
    side: SumInjectionSide,
    value: &Passport,
    line: usize,
) -> Result<SumInjectionReport, Diagnostic> {
    let value_type = structural_value_type(value, line)?;
    let expected = match side {
        SumInjectionSide::Left => &sum.left,
        SumInjectionSide::Right => &sum.right,
    };
    if &value_type != expected {
        return Err(structural_error(
            line,
            format!("sum {side} injection type mismatch: expected `{expected}`, got `{value_type}`"),
            "sum injection must use a value of the selected side type",
        ));
    }
    let sources = [value];
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(&sources);
    let max_trust = max_trust.max(sum.max_trust);
    let max_provenance = max_provenance.max(sum.max_provenance);
    let has_axiom_taint = has_axiom_taint || sum.has_axiom_taint;
    let has_oracle_taint = has_oracle_taint || sum.has_oracle_taint;
    let has_unsafe_taint = has_unsafe_taint || sum.has_unsafe_taint;
    let sum_type = format!("{}+{}", sum.left, sum.right);
    let fingerprint = stable_fingerprint(&[
        "sum-injection-v1".to_string(),
        side.to_string(),
        value.ty.to_string(),
        sum_type.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(SumInjectionReport {
        side,
        value: value.ty.to_string(),
        value_type,
        sum_type,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn record_type(
    name: impl Into<String>,
    fields: Vec<RecordFieldDecl>,
    sources: &[&Passport],
    line: usize,
) -> Result<RecordTypeReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "record type name", line)?;
    validate_unique_field_decls(&fields, line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let field_parts: Vec<String> = fields.iter().map(|field| format!("{}:{}", field.name, field.ty)).collect();
    let mut parts = vec!["record-type-v1".to_string(), name.clone(), format!("trust={max_trust:?}")];
    parts.extend(field_parts.iter().cloned());
    let fingerprint = stable_fingerprint(&parts);
    Ok(RecordTypeReport {
        name,
        fields,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn record_term(
    record_type: &RecordTypeReport,
    fields: Vec<RecordFieldValue>,
    sources: &[&Passport],
    line: usize,
) -> Result<RecordTermReport, Diagnostic> {
    validate_unique_field_values(&fields, line)?;
    if fields.len() != record_type.fields.len() {
        return Err(structural_error(
            line,
            format!("record `{}` expects {} fields, got {}", record_type.name, record_type.fields.len(), fields.len()),
            "record construction must supply exactly the declared fields; missing fields are not filled implicitly",
        ));
    }
    for expected in &record_type.fields {
        let Some(actual) = fields.iter().find(|field| field.name == expected.name) else {
            return Err(structural_error(
                line,
                format!("record `{}` is missing field `{}`", record_type.name, expected.name),
                "record construction must preserve the declared field set",
            ));
        };
        if actual.ty != expected.ty {
            return Err(structural_error(
                line,
                format!("record field `{}` type mismatch: expected `{}`, got `{}`", expected.name, expected.ty, actual.ty),
                "record field values must match their declared field type",
            ));
        }
    }
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let max_trust = max_trust.max(record_type.max_trust);
    let max_provenance = max_provenance.max(record_type.max_provenance);
    let has_axiom_taint = has_axiom_taint || record_type.has_axiom_taint;
    let has_oracle_taint = has_oracle_taint || record_type.has_oracle_taint;
    let has_unsafe_taint = has_unsafe_taint || record_type.has_unsafe_taint;
    let field_parts: Vec<String> = fields.iter().map(|field| format!("{}:{}={}", field.name, field.ty, field.value)).collect();
    let mut parts = vec!["record-term-v1".to_string(), record_type.name.clone(), format!("trust={max_trust:?}")];
    parts.extend(field_parts.iter().cloned());
    let fingerprint = stable_fingerprint(&parts);
    Ok(RecordTermReport {
        name: record_type.name.clone(),
        fields,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn record_projection(
    record: &RecordTermReport,
    field: impl Into<String>,
    line: usize,
) -> Result<RecordProjectionReport, Diagnostic> {
    let field = field.into();
    validate_identifier(&field, "projection field", line)?;
    let Some(found) = record.fields.iter().find(|candidate| candidate.name == field) else {
        return Err(structural_error(
            line,
            format!("record `{}` has no field `{field}`", record.name),
            "record projection must name an existing field; no dynamic field lookup is inserted implicitly",
        ));
    };
    let fingerprint = stable_fingerprint(&[
        "record-projection-v1".to_string(),
        record.name.clone(),
        field.clone(),
        found.ty.clone(),
        found.value.clone(),
        format!("trust={:?}", record.max_trust),
    ]);
    Ok(RecordProjectionReport {
        record: record.name.clone(),
        field,
        result_type: found.ty.clone(),
        result_value: found.value.clone(),
        max_trust: record.max_trust,
        max_provenance: record.max_provenance,
        has_axiom_taint: record.has_axiom_taint,
        has_oracle_taint: record.has_oracle_taint,
        has_unsafe_taint: record.has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_product_type(report: &ProductTypeReport, line: usize) -> Result<(), Diagnostic> {
    validate_type_text(&report.lhs, "product lhs type", line)?;
    validate_type_text(&report.rhs, "product rhs type", line)
}

pub fn require_record_field(report: &RecordTypeReport, field: &str, line: usize) -> Result<RecordFieldDecl, Diagnostic> {
    validate_identifier(field, "required record field", line)?;
    report
        .fields
        .iter()
        .find(|candidate| candidate.name == field)
        .cloned()
        .ok_or_else(|| structural_error(line, format!("record `{}` does not export field `{field}`", report.name), "record field access requires an explicit declared field"))
}

pub fn product_type_passport(theory: &str, report: &ProductTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ProductType {
            lhs: report.lhs.clone(),
            rhs: report.rhs.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:product_type"),
        location: LocationContext::local(),
    }
}

pub fn product_term_passport(theory: &str, report: &ProductTermReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::ProductTerm {
            lhs: report.lhs.clone(),
            rhs: report.rhs.clone(),
            product_type: report.product_type.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:product_term"),
        location: LocationContext::local(),
    }
}

pub fn sum_type_passport(theory: &str, report: &SumTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::SumType {
            left: report.left.clone(),
            right: report.right.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:sum_type"),
        location: LocationContext::local(),
    }
}

pub fn sum_injection_passport(theory: &str, report: &SumInjectionReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::SumInjection {
            side: report.side.to_string(),
            value: report.value.clone(),
            sum_type: report.sum_type.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:sum_injection"),
        location: LocationContext::local(),
    }
}

pub fn record_type_passport(theory: &str, report: &RecordTypeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::RecordType {
            name: report.name.clone(),
            fields: render_field_decls(&report.fields),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:record_type"),
        location: LocationContext::local(),
    }
}

pub fn record_term_passport(theory: &str, report: &RecordTermReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::RecordTerm {
            name: report.name.clone(),
            fields: render_field_values(&report.fields),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:record_term"),
        location: LocationContext::local(),
    }
}

pub fn record_projection_passport(theory: &str, report: &RecordProjectionReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::RecordProjection {
            record: report.record.clone(),
            field: report.field.clone(),
            result: report.result_type.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: structural_caps(),
        cost: CostClass::Trivial,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:record_projection"),
        location: LocationContext::local(),
    }
}

pub fn export_product_type(report: &ProductTypeReport) -> String {
    format!(
        "product_type_report: v1\nlhs: {}\nrhs: {}\ntrust: {:?}\naxiom_taint: {}\noracle_taint: {}\nunsafe_taint: {}\nfingerprint: {}\n",
        report.lhs,
        report.rhs,
        report.max_trust,
        report.has_axiom_taint,
        report.has_oracle_taint,
        report.has_unsafe_taint,
        report.fingerprint
    )
}

pub fn export_product_term(report: &ProductTermReport) -> String {
    format!(
        "product_term_report: v1\nproduct_type: {}\nlhs: {}\nrhs: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.product_type, report.lhs, report.rhs, report.max_trust, report.fingerprint
    )
}

pub fn export_sum_type(report: &SumTypeReport) -> String {
    format!(
        "sum_type_report: v1\nleft: {}\nright: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.left, report.right, report.max_trust, report.fingerprint
    )
}

pub fn export_sum_injection(report: &SumInjectionReport) -> String {
    format!(
        "sum_injection_report: v1\nside: {}\nvalue: {}\nvalue_type: {}\nsum_type: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.side, report.value, report.value_type, report.sum_type, report.max_trust, report.fingerprint
    )
}

pub fn export_record_type(report: &RecordTypeReport) -> String {
    format!(
        "record_type_report: v1\nname: {}\nfields: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.name,
        render_field_decls(&report.fields),
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_record_term(report: &RecordTermReport) -> String {
    format!(
        "record_term_report: v1\nname: {}\nfields: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.name,
        render_field_values(&report.fields),
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_record_projection(report: &RecordProjectionReport) -> String {
    format!(
        "record_projection_report: v1\nrecord: {}\nfield: {}\nresult_type: {}\nresult_value: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.record, report.field, report.result_type, report.result_value, report.max_trust, report.fingerprint
    )
}

fn structural_value_type(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
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
        | TypeKind::ProductTerm { .. }
        | TypeKind::SumInjection { .. }
        | TypeKind::RecordTerm { .. }
        | TypeKind::RecordProjection { .. }
        | TypeKind::OptionValue { .. }
        | TypeKind::ResultValue { .. } => Ok(passport.ty.to_string()),
        TypeKind::FunctionType { .. }
        | TypeKind::LambdaTerm { .. }
        | TypeKind::ApplicationTerm { .. }
        | TypeKind::FunctionContract { .. }
        | TypeKind::ProductType { .. }
        | TypeKind::SumType { .. }
        | TypeKind::RecordType { .. }
        | TypeKind::OptionType { .. }
        | TypeKind::ResultTypeValue { .. } => Ok(passport.ty.to_string()),
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
        | TypeKind::RewriteCertificate { .. } => Err(structural_error(
            line,
            format!("{} is not an ordinary structural value", passport.ty),
            "product/sum/record construction must not silently consume proof, truth, theorem, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(structural_error(
            line,
            format!("{} is not accepted as an ordinary structural value in this MVP", passport.ty),
            "extend the structural whitelist explicitly when a new value class becomes safe for products/sums/records",
        )),
    }
}

fn validate_unique_field_decls(fields: &[RecordFieldDecl], line: usize) -> Result<(), Diagnostic> {
    if fields.is_empty() {
        return Err(structural_error(
            line,
            "record type must declare at least one field",
            "empty record support should be introduced explicitly if needed; this MVP keeps record fingerprints non-empty",
        ));
    }
    let mut seen = BTreeSet::new();
    for field in fields {
        validate_identifier(&field.name, "record field name", line)?;
        validate_type_text(&field.ty, "record field type", line)?;
        if !seen.insert(field.name.clone()) {
            return Err(structural_error(
                line,
                format!("duplicate record field `{}`", field.name),
                "field names are semantic IDs inside a record and must be unique",
            ));
        }
    }
    Ok(())
}

fn validate_unique_field_values(fields: &[RecordFieldValue], line: usize) -> Result<(), Diagnostic> {
    if fields.is_empty() {
        return Err(structural_error(
            line,
            "record term must carry at least one field",
            "record construction must not produce an untyped empty structural object",
        ));
    }
    let mut seen = BTreeSet::new();
    for field in fields {
        validate_identifier(&field.name, "record field name", line)?;
        validate_type_text(&field.ty, "record field type", line)?;
        if !seen.insert(field.name.clone()) {
            return Err(structural_error(
                line,
                format!("duplicate record value field `{}`", field.name),
                "record term field names must be unique before projection or layout lowering",
            ));
        }
    }
    Ok(())
}

fn validate_identifier(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    let value = value.trim();
    if value.is_empty() {
        return Err(structural_error(
            line,
            format!("{what} cannot be empty"),
            "structural names become part of stable fingerprints and future layout contracts",
        ));
    }
    if value.chars().any(|c| c.is_whitespace() || matches!(c, ':' | ',' | '{' | '}')) {
        return Err(structural_error(
            line,
            format!("{what} `{value}` contains unsupported structural punctuation"),
            "use simple identifier-like names for this MVP; richer names should pass through resolver IDs later",
        ));
    }
    Ok(())
}

fn validate_type_text(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    let value = value.trim();
    if value.is_empty() {
        return Err(structural_error(
            line,
            format!("{what} cannot be empty"),
            "structural type descriptors must be explicit; no Any/Unknown type is inserted implicitly",
        ));
    }
    Ok(())
}

fn render_field_decls(fields: &[RecordFieldDecl]) -> String {
    fields
        .iter()
        .map(|field| format!("{}:{}", field.name, field.ty))
        .collect::<Vec<_>>()
        .join(",")
}

fn render_field_values(fields: &[RecordFieldValue]) -> String {
    fields
        .iter()
        .map(|field| format!("{}:{}={}", field.name, field.ty, field.value))
        .collect::<Vec<_>>()
        .join(",")
}

fn structural_caps() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanSerializeForMigration,
        Capability::CanCompilePortableCode,
        Capability::CanCompileGpuKernel,
    ])
}

fn merge_history(sources: &[&Passport], event: &str) -> HistoryChain {
    HistoryChain::merge_many(sources.iter().map(|p| &p.history), event)
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
    format!("structural-{state:016x}")
}

fn structural_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::StructuralTypeError, Some(line), message).with_help(help)
}
