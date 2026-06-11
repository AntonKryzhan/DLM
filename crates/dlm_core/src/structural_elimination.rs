use std::collections::BTreeSet;
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::structural::{ProductTermReport, RecordTermReport, SumInjectionReport, SumInjectionSide};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProductEliminationReport {
    pub product_type: String,
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
pub struct SumEliminationReport {
    pub sum_type: String,
    pub selected_side: SumInjectionSide,
    pub left_case: String,
    pub right_case: String,
    pub result_type: String,
    pub selected_result: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordPatternBinding {
    pub field: String,
    pub result_type: String,
    pub result_value: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordPatternReport {
    pub record: String,
    pub fields: Vec<RecordPatternBinding>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn product_elimination(
    product: &ProductTermReport,
    source: &Passport,
    line: usize,
) -> Result<ProductEliminationReport, Diagnostic> {
    ensure_structural_subject(source, "product elimination", line)?;
    if product.product_type.trim().is_empty() {
        return Err(elimination_error(
            line,
            "product elimination requires a non-empty product type",
            "product destructuring must reference the checked product construction report",
        ));
    }
    let mut max_trust = product.max_trust.max(source.trust);
    let max_provenance = product.max_provenance.max(source.provenance);
    if source.trust > max_trust {
        max_trust = source.trust;
    }
    let has_axiom_taint = product.has_axiom_taint || source.trust >= TrustLevel::Axiom;
    let has_oracle_taint = product.has_oracle_taint || source.trust >= TrustLevel::Oracle;
    let has_unsafe_taint = product.has_unsafe_taint || source.trust >= TrustLevel::Unsafe;
    let fingerprint = stable_fingerprint(&[
        "product-elimination-v1".to_string(),
        product.product_type.clone(),
        product.lhs.clone(),
        product.rhs.clone(),
        source.ty.to_string(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ProductEliminationReport {
        product_type: product.product_type.clone(),
        lhs: product.lhs.clone(),
        rhs: product.rhs.clone(),
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn sum_elimination(
    injection: &SumInjectionReport,
    source: &Passport,
    left_case_result: &Passport,
    right_case_result: &Passport,
    line: usize,
) -> Result<SumEliminationReport, Diagnostic> {
    ensure_structural_subject(source, "sum elimination", line)?;
    let left_case = ordinary_elimination_result_type(left_case_result, line)?;
    let right_case = ordinary_elimination_result_type(right_case_result, line)?;
    if left_case != right_case {
        return Err(elimination_error(
            line,
            format!("sum elimination branch result mismatch: left `{left_case}`, right `{right_case}`"),
            "both branches of sum/case elimination must produce the same result type before the result can be treated as ordinary value",
        ));
    }
    let selected_result = match injection.side {
        SumInjectionSide::Left => left_case_result.ty.to_string(),
        SumInjectionSide::Right => right_case_result.ty.to_string(),
    };
    let sources = [source, left_case_result, right_case_result];
    let (source_trust, source_provenance, source_axiom, source_oracle, source_unsafe) = taint_summary(&sources);
    let max_trust = injection.max_trust.max(source_trust);
    let max_provenance = injection.max_provenance.max(source_provenance);
    let has_axiom_taint = injection.has_axiom_taint || source_axiom;
    let has_oracle_taint = injection.has_oracle_taint || source_oracle;
    let has_unsafe_taint = injection.has_unsafe_taint || source_unsafe;
    let fingerprint = stable_fingerprint(&[
        "sum-elimination-v1".to_string(),
        injection.sum_type.clone(),
        injection.side.to_string(),
        left_case.clone(),
        right_case.clone(),
        selected_result.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(SumEliminationReport {
        sum_type: injection.sum_type.clone(),
        selected_side: injection.side,
        left_case,
        right_case: right_case.clone(),
        result_type: right_case,
        selected_result,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn record_pattern(
    record: &RecordTermReport,
    source: &Passport,
    fields: &[&str],
    line: usize,
) -> Result<RecordPatternReport, Diagnostic> {
    ensure_structural_subject(source, "record pattern", line)?;
    if fields.is_empty() {
        return Err(elimination_error(
            line,
            "record pattern must bind at least one field",
            "empty record pattern support should be introduced explicitly if needed",
        ));
    }
    let mut seen = BTreeSet::new();
    let mut bindings = Vec::new();
    for field in fields {
        let field = field.trim();
        validate_identifier(field, "record pattern field", line)?;
        if !seen.insert(field.to_string()) {
            return Err(elimination_error(
                line,
                format!("duplicate record pattern field `{field}`"),
                "record pattern field bindings must be unique and layout-stable",
            ));
        }
        let value = record.fields.iter().find(|candidate| candidate.name == field).ok_or_else(|| {
            elimination_error(
                line,
                format!("record pattern field `{field}` does not exist on `{}`", record.name),
                "record elimination must only bind fields declared by the checked record term",
            )
        })?;
        bindings.push(RecordPatternBinding {
            field: field.to_string(),
            result_type: value.ty.clone(),
            result_value: value.value.clone(),
        });
    }
    let has_axiom_taint = record.has_axiom_taint || source.trust >= TrustLevel::Axiom;
    let has_oracle_taint = record.has_oracle_taint || source.trust >= TrustLevel::Oracle;
    let has_unsafe_taint = record.has_unsafe_taint || source.trust >= TrustLevel::Unsafe;
    let max_trust = record.max_trust.max(source.trust);
    let max_provenance = record.max_provenance.max(source.provenance);
    let fingerprint = stable_fingerprint(&[
        "record-pattern-v1".to_string(),
        record.name.clone(),
        render_pattern_bindings(&bindings),
        source.ty.to_string(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(RecordPatternReport {
        record: record.name.clone(),
        fields: bindings,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn product_elimination_passport(
    theory: &str,
    report: &ProductEliminationReport,
    sources: &[&Passport],
) -> Passport {
    Passport {
        ty: TypeKind::ProductElimination {
            product_type: report.product_type.clone(),
            lhs: report.lhs.clone(),
            rhs: report.rhs.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: elimination_caps(),
        cost: CostClass::Trivial,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:product_elimination"),
        location: LocationContext::local(),
    }
}

pub fn sum_elimination_passport(
    theory: &str,
    report: &SumEliminationReport,
    sources: &[&Passport],
) -> Passport {
    Passport {
        ty: TypeKind::SumElimination {
            sum_type: report.sum_type.clone(),
            side: report.selected_side.to_string(),
            result: report.result_type.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: elimination_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:sum_elimination"),
        location: LocationContext::local(),
    }
}

pub fn record_pattern_passport(
    theory: &str,
    report: &RecordPatternReport,
    sources: &[&Passport],
) -> Passport {
    Passport {
        ty: TypeKind::RecordPattern {
            record: report.record.clone(),
            fields: render_pattern_bindings(&report.fields),
        },
        construction: ConstructionMode::Definable,
        capabilities: elimination_caps(),
        cost: CostClass::Trivial,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "structural:record_pattern"),
        location: LocationContext::local(),
    }
}

pub fn export_product_elimination(report: &ProductEliminationReport) -> String {
    format!(
        "product_elimination_report: v1\nproduct_type: {}\nlhs: {}\nrhs: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.product_type, report.lhs, report.rhs, report.max_trust, report.fingerprint
    )
}

pub fn export_sum_elimination(report: &SumEliminationReport) -> String {
    format!(
        "sum_elimination_report: v1\nsum_type: {}\nselected_side: {}\nresult_type: {}\nselected_result: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.sum_type,
        report.selected_side,
        report.result_type,
        report.selected_result,
        report.max_trust,
        report.fingerprint
    )
}

pub fn export_record_pattern(report: &RecordPatternReport) -> String {
    format!(
        "record_pattern_report: v1\nrecord: {}\nfields: {}\ntrust: {:?}\nfingerprint: {}\n",
        report.record,
        render_pattern_bindings(&report.fields),
        report.max_trust,
        report.fingerprint
    )
}

fn ensure_structural_subject(passport: &Passport, what: &str, line: usize) -> Result<(), Diagnostic> {
    match &passport.ty {
        TypeKind::ProductTerm { .. }
        | TypeKind::SumInjection { .. }
        | TypeKind::RecordTerm { .. }
        | TypeKind::RecordProjection { .. } => Ok(()),
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
        | TypeKind::RewriteCertificate { .. } => Err(elimination_error(
            line,
            format!("{} cannot consume {} as structural subject", what, passport.ty),
            "structural elimination must not silently destruct proof, truth, theorem, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(elimination_error(
            line,
            format!("{} requires a product/sum/record structural subject, got {}", what, passport.ty),
            "construct the structural object first and then eliminate it through an explicit elimination boundary",
        )),
    }
}

fn ordinary_elimination_result_type(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
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
        | TypeKind::RecordPattern { .. } => Ok(passport.ty.to_string()),
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
        | TypeKind::RewriteCertificate { .. } => Err(elimination_error(
            line,
            format!("{} is not an ordinary structural elimination result", passport.ty),
            "case branches and pattern bindings must not silently produce proof, truth, theorem, runtime witness, equality proof or certificate objects",
        )),
        _ => Err(elimination_error(
            line,
            format!("{} is not accepted as an elimination result in this MVP", passport.ty),
            "extend the result whitelist explicitly when a new ordinary value class becomes safe",
        )),
    }
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
        has_oracle_taint |= source.trust >= TrustLevel::Oracle;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe;
    }
    (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint)
}

fn merge_history(sources: &[&Passport], event: &str) -> HistoryChain {
    let mut history = HistoryChain::empty();
    for source in sources {
        for entry in source.history.events() {
            history.push(entry.clone());
        }
    }
    history.push(event);
    history
}

fn elimination_caps() -> CapabilitySet {
    CapabilitySet::from([Capability::CanSymbolicPrint, Capability::CanSerializeForMigration])
}

fn validate_identifier(value: &str, what: &str, line: usize) -> Result<(), Diagnostic> {
    let value = value.trim();
    if value.is_empty() {
        return Err(elimination_error(
            line,
            format!("{what} cannot be empty"),
            "elimination identifiers become part of stable fingerprints and future layout/pattern contracts",
        ));
    }
    if value.chars().any(|c| c.is_whitespace() || matches!(c, ':' | ',' | '{' | '}')) {
        return Err(elimination_error(
            line,
            format!("{what} `{value}` contains unsupported structural punctuation"),
            "use simple identifier-like names for this MVP; richer names should pass through resolver IDs later",
        ));
    }
    Ok(())
}

fn render_pattern_bindings(fields: &[RecordPatternBinding]) -> String {
    fields
        .iter()
        .map(|field| format!("{}:{}={}", field.field, field.result_type, field.result_value))
        .collect::<Vec<_>>()
        .join(",")
}

fn stable_fingerprint(parts: &[String]) -> String {
    let joined = parts.join("\u{1f}");
    let mut hash: u64 = 0xcbf29ce484222325;
    for byte in joined.as_bytes() {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    format!("struct-elim:{hash:016x}")
}

fn elimination_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::StructuralEliminationError, Some(line), message).with_help(help)
}
