use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::function_contract::{FunctionContractReport, FunctionContractStatus, FunctionPurity, FunctionTotality};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::termination_budget::{TerminationBudgetReport, TerminationBudgetStatus};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeOperationKind {
    NatAdd,
    NatEq,
    BoolAnd,
    BoolNot,
    OptionMap,
    ResultMap,
    ListLength,
    SequenceLength,
    SequenceIndex,
    ListMap,
    SequenceMap,
    ListFold,
    SequenceFold,
}

impl fmt::Display for PreludeOperationKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeOperationKind::NatAdd => write!(f, "nat.add"),
            PreludeOperationKind::NatEq => write!(f, "nat.eq"),
            PreludeOperationKind::BoolAnd => write!(f, "bool.and"),
            PreludeOperationKind::BoolNot => write!(f, "bool.not"),
            PreludeOperationKind::OptionMap => write!(f, "option.map"),
            PreludeOperationKind::ResultMap => write!(f, "result.map"),
            PreludeOperationKind::ListLength => write!(f, "list.length"),
            PreludeOperationKind::SequenceLength => write!(f, "sequence.length"),
            PreludeOperationKind::SequenceIndex => write!(f, "sequence.index"),
            PreludeOperationKind::ListMap => write!(f, "list.map"),
            PreludeOperationKind::SequenceMap => write!(f, "sequence.map"),
            PreludeOperationKind::ListFold => write!(f, "list.fold"),
            PreludeOperationKind::SequenceFold => write!(f, "sequence.fold"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeContractStatus {
    VerifiedChecked,
    Downgraded,
    Open,
    RejectedSignature,
    RejectedBudget,
}

impl fmt::Display for PreludeContractStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeContractStatus::VerifiedChecked => write!(f, "verified_checked"),
            PreludeContractStatus::Downgraded => write!(f, "downgraded"),
            PreludeContractStatus::Open => write!(f, "open"),
            PreludeContractStatus::RejectedSignature => write!(f, "rejected_signature"),
            PreludeContractStatus::RejectedBudget => write!(f, "rejected_budget"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreludeSignatureParams {
    pub item_type: String,
    pub output_type: String,
    pub error_type: String,
    pub accumulator_type: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreludeOperationSignature {
    pub operation: PreludeOperationKind,
    pub domain: String,
    pub codomain: String,
    pub requires_budget: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StandardPreludeContractReport {
    pub name: String,
    pub operation: PreludeOperationKind,
    pub domain: String,
    pub codomain: String,
    pub function_contract: String,
    pub budget_name: Option<String>,
    pub status: PreludeContractStatus,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn prelude_signature_params(
    item_type: impl Into<String>,
    output_type: impl Into<String>,
    error_type: impl Into<String>,
    accumulator_type: impl Into<String>,
    line: usize,
) -> Result<PreludeSignatureParams, Diagnostic> {
    let item_type = item_type.into();
    let output_type = output_type.into();
    let error_type = error_type.into();
    let accumulator_type = accumulator_type.into();
    validate_type_text(&item_type, "prelude item type", line)?;
    validate_type_text(&output_type, "prelude output type", line)?;
    validate_type_text(&error_type, "prelude error type", line)?;
    validate_type_text(&accumulator_type, "prelude accumulator type", line)?;
    Ok(PreludeSignatureParams {
        item_type,
        output_type,
        error_type,
        accumulator_type,
    })
}

pub fn prelude_operation_signature(
    operation: PreludeOperationKind,
    params: &PreludeSignatureParams,
    line: usize,
) -> Result<PreludeOperationSignature, Diagnostic> {
    validate_type_text(&params.item_type, "prelude item type", line)?;
    validate_type_text(&params.output_type, "prelude output type", line)?;
    validate_type_text(&params.error_type, "prelude error type", line)?;
    validate_type_text(&params.accumulator_type, "prelude accumulator type", line)?;

    let item = &params.item_type;
    let out = &params.output_type;
    let err = &params.error_type;
    let acc = &params.accumulator_type;
    let (domain, codomain, requires_budget) = match operation {
        PreludeOperationKind::NatAdd => (product_type("Nat", "Nat"), "Nat".to_string(), false),
        PreludeOperationKind::NatEq => (product_type("Nat", "Nat"), "Bool".to_string(), false),
        PreludeOperationKind::BoolAnd => (product_type("Bool", "Bool"), "Bool".to_string(), false),
        PreludeOperationKind::BoolNot => ("Bool".to_string(), "Bool".to_string(), false),
        PreludeOperationKind::OptionMap => (
            product_type(&option_type(item), &function_type(item, out)),
            option_type(out),
            false,
        ),
        PreludeOperationKind::ResultMap => (
            product_type(&result_type(item, err), &function_type(item, out)),
            result_type(out, err),
            false,
        ),
        PreludeOperationKind::ListLength => (list_type(item), "Nat".to_string(), false),
        PreludeOperationKind::SequenceLength => (sequence_type(item), "Nat".to_string(), false),
        PreludeOperationKind::SequenceIndex => (
            product_type(&sequence_type(item), "Nat"),
            option_type(item),
            false,
        ),
        PreludeOperationKind::ListMap => (
            product_type(&list_type(item), &function_type(item, out)),
            list_type(out),
            true,
        ),
        PreludeOperationKind::SequenceMap => (
            product_type(&sequence_type(item), &function_type(item, out)),
            sequence_type(out),
            true,
        ),
        PreludeOperationKind::ListFold => (
            product_type(&list_type(item), &product_type(acc, &function_type(&product_type(acc, item), acc))),
            acc.to_string(),
            true,
        ),
        PreludeOperationKind::SequenceFold => (
            product_type(&sequence_type(item), &product_type(acc, &function_type(&product_type(acc, item), acc))),
            acc.to_string(),
            true,
        ),
    };
    let fingerprint = stable_fingerprint(&[
        "prelude-signature-v1".to_string(),
        operation.to_string(),
        domain.clone(),
        codomain.clone(),
        format!("requires_budget={requires_budget}"),
    ]);
    Ok(PreludeOperationSignature {
        operation,
        domain,
        codomain,
        requires_budget,
        fingerprint,
    })
}

pub fn standard_prelude_contract(
    name: impl Into<String>,
    signature: &PreludeOperationSignature,
    function_contract: &FunctionContractReport,
    budget: Option<&TerminationBudgetReport>,
    sources: &[&Passport],
    line: usize,
) -> Result<StandardPreludeContractReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "standard prelude contract name", line)?;

    let mut status = PreludeContractStatus::VerifiedChecked;
    let mut open_obligations = Vec::new();

    if function_contract.domain != signature.domain {
        status = PreludeContractStatus::RejectedSignature;
        open_obligations.push(format!(
            "domain mismatch: canonical {} expects `{}`, function contract `{}` has `{}`",
            signature.operation, signature.domain, function_contract.name, function_contract.domain
        ));
    }
    if function_contract.codomain != signature.codomain {
        status = PreludeContractStatus::RejectedSignature;
        open_obligations.push(format!(
            "codomain mismatch: canonical {} returns `{}`, function contract `{}` returns `{}`",
            signature.operation, signature.codomain, function_contract.name, function_contract.codomain
        ));
    }
    if function_contract.status != FunctionContractStatus::Verified {
        status = status.max(status_from_function_contract(function_contract.status));
        open_obligations.push(format!(
            "function contract `{}` is {}, not verified",
            function_contract.name, function_contract.status
        ));
    }
    if function_contract.purity != FunctionPurity::Pure {
        status = status.max(PreludeContractStatus::Downgraded);
        open_obligations.push(format!(
            "function contract `{}` is {}, not pure",
            function_contract.name, function_contract.purity
        ));
    }
    if function_contract.totality != FunctionTotality::Total {
        status = status.max(PreludeContractStatus::Downgraded);
        open_obligations.push(format!(
            "function contract `{}` is {}, not total",
            function_contract.name, function_contract.totality
        ));
    }
    if !function_contract.effects.is_empty() {
        status = status.max(PreludeContractStatus::Downgraded);
        open_obligations.push(format!(
            "function contract `{}` has explicit effects; standard prelude operation is not clean pure checked code",
            function_contract.name
        ));
    }
    for obligation in &function_contract.open_obligations {
        open_obligations.push(format!(
            "function contract `{}` obligation: {obligation}",
            function_contract.name
        ));
    }

    let mut max_trust = function_contract.max_trust;
    let mut max_provenance = function_contract.max_provenance;
    let mut has_axiom_taint = function_contract.has_axiom_taint;
    let mut has_oracle_taint = function_contract.has_oracle_taint;
    let mut has_unsafe_taint = function_contract.has_unsafe_taint;
    let mut budget_name = None;

    if signature.requires_budget {
        match budget {
            Some(report) => {
                budget_name = Some(report.name.clone());
                max_trust = max_trust.max(report.max_trust);
                max_provenance = max_provenance.max(report.max_provenance);
                has_axiom_taint |= report.has_axiom_taint;
                has_oracle_taint |= report.has_oracle_taint;
                has_unsafe_taint |= report.has_unsafe_taint;
                if report.status != TerminationBudgetStatus::VerifiedUnified {
                    status = status.max(status_from_budget(report.status));
                    open_obligations.push(format!(
                        "termination budget `{}` is {}, not verified_unified",
                        report.name, report.status
                    ));
                }
                for obligation in &report.open_obligations {
                    open_obligations.push(format!(
                        "termination budget `{}` obligation: {obligation}",
                        report.name
                    ));
                }
            }
            None => {
                status = status.max(PreludeContractStatus::Open);
                open_obligations.push(format!(
                    "{} requires an explicit verified_unified termination budget",
                    signature.operation
                ));
            }
        }
    }

    let (source_trust, source_provenance, source_axiom, source_oracle, source_unsafe) = source_taint(sources);
    max_trust = max_trust.max(source_trust);
    max_provenance = max_provenance.max(source_provenance);
    has_axiom_taint |= source_axiom;
    has_oracle_taint |= source_oracle;
    has_unsafe_taint |= source_unsafe;

    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        status = status.max(PreludeContractStatus::Downgraded);
        open_obligations.push("standard prelude contract depends on Axiom/Oracle/Unsafe taint and is not clean Checked prelude code".to_string());
    }

    open_obligations.sort();
    open_obligations.dedup();

    let fingerprint = stable_fingerprint(&[
        "standard-prelude-contract-v1".to_string(),
        name.clone(),
        signature.operation.to_string(),
        signature.domain.clone(),
        signature.codomain.clone(),
        function_contract.name.clone(),
        format!("budget={budget_name:?}"),
        status.to_string(),
        format!("trust={max_trust:?}"),
    ]);

    Ok(StandardPreludeContractReport {
        name,
        operation: signature.operation,
        domain: signature.domain.clone(),
        codomain: signature.codomain.clone(),
        function_contract: function_contract.name.clone(),
        budget_name,
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

pub fn require_verified_standard_prelude_contract(
    report: &StandardPreludeContractReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.status == PreludeContractStatus::VerifiedChecked {
        Ok(())
    } else {
        Err(prelude_error(
            line,
            format!(
                "standard prelude contract `{}` for {} is {}, not verified_checked",
                report.name, report.operation, report.status
            ),
            "standard prelude contracts must remain pure, total, signature-exact and budget-bounded before compiler/prelude lowering may rely on them",
        ))
    }
}

pub fn standard_prelude_contract_passport(
    theory: &str,
    report: &StandardPreludeContractReport,
    sources: &[&Passport],
) -> Passport {
    Passport {
        ty: TypeKind::StandardPreludeContract {
            name: report.name.clone(),
            operation: report.operation.to_string(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanSerializeForMigration,
            Capability::CanCompareByProof,
        ]),
        cost: CostClass::ProofRequired,
        trust: report.max_trust.max(taint_summary(sources).0).max(TrustLevel::Builtin),
        provenance: report.max_provenance.max(taint_summary(sources).1).max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(
            sources,
            format!(
                "standard_prelude:contract:{}:{}:{}:fingerprint={}",
                report.name, report.operation, report.status, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_standard_prelude_contract(report: &StandardPreludeContractReport) -> String {
    let mut out = String::new();
    out.push_str("standard_prelude_contract: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("domain: {}\n", report.domain));
    out.push_str(&format!("codomain: {}\n", report.codomain));
    out.push_str(&format!("function_contract: {}\n", report.function_contract));
    out.push_str(&format!("budget_name: {:?}\n", report.budget_name));
    out.push_str(&format!("status: {}\n", report.status));
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

fn status_from_function_contract(status: FunctionContractStatus) -> PreludeContractStatus {
    match status {
        FunctionContractStatus::Verified => PreludeContractStatus::VerifiedChecked,
        FunctionContractStatus::Downgraded => PreludeContractStatus::Downgraded,
        FunctionContractStatus::Open => PreludeContractStatus::Open,
        FunctionContractStatus::Rejected => PreludeContractStatus::RejectedSignature,
    }
}

fn status_from_budget(status: TerminationBudgetStatus) -> PreludeContractStatus {
    match status {
        TerminationBudgetStatus::VerifiedUnified => PreludeContractStatus::VerifiedChecked,
        TerminationBudgetStatus::Downgraded => PreludeContractStatus::Downgraded,
        TerminationBudgetStatus::Open => PreludeContractStatus::Open,
        TerminationBudgetStatus::RejectedBudgetExceeded | TerminationBudgetStatus::RejectedInconsistent => {
            PreludeContractStatus::RejectedBudget
        }
    }
}

fn product_type(lhs: &str, rhs: &str) -> String {
    format!("ProductType<{lhs}*{rhs}>")
}

fn option_type(item: &str) -> String {
    format!("OptionType<{item}>")
}

fn result_type(ok: &str, err: &str) -> String {
    format!("ResultType<{ok},{err}>")
}

fn list_type(item: &str) -> String {
    format!("ListType<{item}>")
}

fn sequence_type(item: &str) -> String {
    format!("SequenceType<{item}>")
}

fn function_type(domain: &str, codomain: &str) -> String {
    format!("FunctionType<{domain}->{codomain}>")
}

fn validate_identifier(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return Err(prelude_error(line, format!("{label} is empty"), "standard prelude contracts require stable ASCII identifiers"));
    };
    if !(first.is_ascii_alphabetic() || first == '_') || !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(prelude_error(line, format!("invalid {label} `{text}`"), "use a stable identifier such as nat_add_checked or sequence_map_checked"));
    }
    Ok(())
}

fn validate_type_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(prelude_error(line, format!("{label} is empty"), "standard prelude signatures must carry explicit type identities"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(prelude_error(line, format!("{label} contains a newline"), "prelude type identities must be stable single-line audit keys"));
    }
    Ok(())
}

fn source_taint(sources: &[&Passport]) -> (TrustLevel, Provenance, bool, bool, bool) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    let mut has_axiom = false;
    let mut has_oracle = false;
    let mut has_unsafe = false;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
        has_axiom |= source.trust >= TrustLevel::Axiom;
        has_oracle |= source.trust >= TrustLevel::Oracle || source.provenance == Provenance::OracleInput;
        has_unsafe |= source.trust >= TrustLevel::Unsafe || source.provenance == Provenance::UnsafeExternal;
    }
    (max_trust, max_provenance, has_axiom, has_oracle, has_unsafe)
}

fn taint_summary(sources: &[&Passport]) -> (TrustLevel, Provenance) {
    let (trust, provenance, _, _, _) = source_taint(sources);
    (trust, provenance)
}

fn merge_history(sources: &[&Passport], event: impl Into<String>) -> HistoryChain {
    if sources.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(sources.iter().map(|source| &source.history), event)
    }
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

fn prelude_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::StandardPreludeError, Some(line), message).with_help(help)
}
