use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::standard_prelude::{PreludeContractStatus, PreludeOperationKind, StandardPreludeContractReport};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum PreludeEvalStatus {
    Evaluated,
    SymbolicEvaluated,
    RejectedContract,
    RejectedInput,
    RejectedFuel,
}

impl fmt::Display for PreludeEvalStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreludeEvalStatus::Evaluated => write!(f, "evaluated"),
            PreludeEvalStatus::SymbolicEvaluated => write!(f, "symbolic_evaluated"),
            PreludeEvalStatus::RejectedContract => write!(f, "rejected_contract"),
            PreludeEvalStatus::RejectedInput => write!(f, "rejected_input"),
            PreludeEvalStatus::RejectedFuel => write!(f, "rejected_fuel"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PreludeEvalValue {
    Nat(u128),
    Bool(bool),
    Text(String),
    Product(Box<PreludeEvalValue>, Box<PreludeEvalValue>),
    OptionSome { item_type: String, value: Box<PreludeEvalValue> },
    OptionNone { item_type: String },
    ResultOk { ok_type: String, err_type: String, value: Box<PreludeEvalValue> },
    ResultErr { ok_type: String, err_type: String, error: Box<PreludeEvalValue> },
    List { item_type: String, items: Vec<PreludeEvalValue> },
    Sequence { item_type: String, items: Vec<PreludeEvalValue> },
    FunctionRef { name: String, domain: String, codomain: String },
    Symbolic { expr: String, ty: String },
    Evidence { description: String, kind: String },
}

impl PreludeEvalValue {
    pub fn type_key(&self) -> String {
        match self {
            PreludeEvalValue::Nat(_) => "Nat".to_string(),
            PreludeEvalValue::Bool(_) => "Bool".to_string(),
            PreludeEvalValue::Text(_) => "Text".to_string(),
            PreludeEvalValue::Product(lhs, rhs) => format!("ProductType<{}*{}>", lhs.type_key(), rhs.type_key()),
            PreludeEvalValue::OptionSome { item_type, .. } | PreludeEvalValue::OptionNone { item_type } => {
                format!("OptionType<{item_type}>")
            }
            PreludeEvalValue::ResultOk { ok_type, err_type, .. }
            | PreludeEvalValue::ResultErr { ok_type, err_type, .. } => format!("ResultType<{ok_type},{err_type}>"),
            PreludeEvalValue::List { item_type, .. } => format!("ListType<{item_type}>"),
            PreludeEvalValue::Sequence { item_type, .. } => format!("SequenceType<{item_type}>"),
            PreludeEvalValue::FunctionRef { domain, codomain, .. } => format!("FunctionType<{domain}->{codomain}>"),
            PreludeEvalValue::Symbolic { ty, .. } => ty.clone(),
            PreludeEvalValue::Evidence { kind, .. } => kind.clone(),
        }
    }

    pub fn render(&self) -> String {
        match self {
            PreludeEvalValue::Nat(value) => format!("Nat({value})"),
            PreludeEvalValue::Bool(value) => format!("Bool({value})"),
            PreludeEvalValue::Text(value) => format!("Text({value})"),
            PreludeEvalValue::Product(lhs, rhs) => format!("Product({}, {})", lhs.render(), rhs.render()),
            PreludeEvalValue::OptionSome { item_type, value } => format!("Some<{item_type}>({})", value.render()),
            PreludeEvalValue::OptionNone { item_type } => format!("None<{item_type}>") ,
            PreludeEvalValue::ResultOk { ok_type, err_type, value } => {
                format!("Ok<{ok_type},{err_type}>({})", value.render())
            }
            PreludeEvalValue::ResultErr { ok_type, err_type, error } => {
                format!("Err<{ok_type},{err_type}>({})", error.render())
            }
            PreludeEvalValue::List { item_type, items } => {
                let rendered: Vec<String> = items.iter().map(|item| item.render()).collect();
                format!("List<{item_type}>[{}]", rendered.join(","))
            }
            PreludeEvalValue::Sequence { item_type, items } => {
                let rendered: Vec<String> = items.iter().map(|item| item.render()).collect();
                format!("Sequence<{item_type}>[{}]", rendered.join(","))
            }
            PreludeEvalValue::FunctionRef { name, domain, codomain } => {
                format!("FunctionRef<{name}:{domain}->{codomain}>")
            }
            PreludeEvalValue::Symbolic { expr, ty } => format!("Symbolic<{ty}>({expr})"),
            PreludeEvalValue::Evidence { description, kind } => format!("Evidence<{kind}>({description})"),
        }
    }

    fn contains_evidence_boundary(&self) -> bool {
        match self {
            PreludeEvalValue::Evidence { kind, .. } => is_forbidden_evidence_kind(kind),
            PreludeEvalValue::Product(lhs, rhs) => lhs.contains_evidence_boundary() || rhs.contains_evidence_boundary(),
            PreludeEvalValue::OptionSome { value, .. } => value.contains_evidence_boundary(),
            PreludeEvalValue::ResultOk { value, .. } => value.contains_evidence_boundary(),
            PreludeEvalValue::ResultErr { error, .. } => error.contains_evidence_boundary(),
            PreludeEvalValue::List { items, .. } | PreludeEvalValue::Sequence { items, .. } => {
                items.iter().any(PreludeEvalValue::contains_evidence_boundary)
            }
            PreludeEvalValue::Nat(_)
            | PreludeEvalValue::Bool(_)
            | PreludeEvalValue::Text(_)
            | PreludeEvalValue::OptionNone { .. }
            | PreludeEvalValue::FunctionRef { .. }
            | PreludeEvalValue::Symbolic { .. } => false,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreludeEvaluationReport {
    pub name: String,
    pub operation: PreludeOperationKind,
    pub contract: String,
    pub input_type: String,
    pub output_type: String,
    pub input_render: String,
    pub result: Option<PreludeEvalValue>,
    pub status: PreludeEvalStatus,
    pub steps_used: usize,
    pub fuel_limit: usize,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn eval_nat(value: u128) -> PreludeEvalValue {
    PreludeEvalValue::Nat(value)
}

pub fn eval_bool(value: bool) -> PreludeEvalValue {
    PreludeEvalValue::Bool(value)
}

pub fn eval_text(value: impl Into<String>) -> PreludeEvalValue {
    PreludeEvalValue::Text(value.into())
}

pub fn eval_product(lhs: PreludeEvalValue, rhs: PreludeEvalValue) -> PreludeEvalValue {
    PreludeEvalValue::Product(Box::new(lhs), Box::new(rhs))
}

pub fn eval_function_ref(
    name: impl Into<String>,
    domain: impl Into<String>,
    codomain: impl Into<String>,
    line: usize,
) -> Result<PreludeEvalValue, Diagnostic> {
    let name = name.into();
    let domain = domain.into();
    let codomain = codomain.into();
    validate_identifier(&name, "function reference name", line)?;
    validate_type_text(&domain, "function reference domain", line)?;
    validate_type_text(&codomain, "function reference codomain", line)?;
    Ok(PreludeEvalValue::FunctionRef { name, domain, codomain })
}

pub fn eval_option_some(item_type: impl Into<String>, value: PreludeEvalValue, line: usize) -> Result<PreludeEvalValue, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "option item type", line)?;
    if value.type_key() != item_type {
        return Err(prelude_eval_error(
            line,
            format!("option.some item has type `{}`, expected `{item_type}`", value.type_key()),
            "small-step prelude values must carry exact algebraic type identities; no implicit coercion is inserted",
        ));
    }
    Ok(PreludeEvalValue::OptionSome { item_type, value: Box::new(value) })
}

pub fn eval_option_none(item_type: impl Into<String>, line: usize) -> Result<PreludeEvalValue, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "option item type", line)?;
    Ok(PreludeEvalValue::OptionNone { item_type })
}

pub fn eval_result_ok(
    ok_type: impl Into<String>,
    err_type: impl Into<String>,
    value: PreludeEvalValue,
    line: usize,
) -> Result<PreludeEvalValue, Diagnostic> {
    let ok_type = ok_type.into();
    let err_type = err_type.into();
    validate_type_text(&ok_type, "result ok type", line)?;
    validate_type_text(&err_type, "result err type", line)?;
    if value.type_key() != ok_type {
        return Err(prelude_eval_error(
            line,
            format!("result.ok value has type `{}`, expected `{ok_type}`", value.type_key()),
            "Result<T,E> evaluation preserves the declared ok/error boundary exactly",
        ));
    }
    Ok(PreludeEvalValue::ResultOk { ok_type, err_type, value: Box::new(value) })
}

pub fn eval_result_err(
    ok_type: impl Into<String>,
    err_type: impl Into<String>,
    error: PreludeEvalValue,
    line: usize,
) -> Result<PreludeEvalValue, Diagnostic> {
    let ok_type = ok_type.into();
    let err_type = err_type.into();
    validate_type_text(&ok_type, "result ok type", line)?;
    validate_type_text(&err_type, "result err type", line)?;
    if error.type_key() != err_type {
        return Err(prelude_eval_error(
            line,
            format!("result.err value has type `{}`, expected `{err_type}`", error.type_key()),
            "Result<T,E> evaluation preserves the declared ok/error boundary exactly",
        ));
    }
    Ok(PreludeEvalValue::ResultErr { ok_type, err_type, error: Box::new(error) })
}

pub fn eval_list(item_type: impl Into<String>, items: Vec<PreludeEvalValue>, line: usize) -> Result<PreludeEvalValue, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "list item type", line)?;
    for item in &items {
        if item.type_key() != item_type {
            return Err(prelude_eval_error(
                line,
                format!("list item has type `{}`, expected `{item_type}`", item.type_key()),
                "List<T> evaluation is homogeneous and does not synthesize hidden Any",
            ));
        }
    }
    Ok(PreludeEvalValue::List { item_type, items })
}

pub fn eval_sequence(item_type: impl Into<String>, items: Vec<PreludeEvalValue>, line: usize) -> Result<PreludeEvalValue, Diagnostic> {
    let item_type = item_type.into();
    validate_type_text(&item_type, "sequence item type", line)?;
    for item in &items {
        if item.type_key() != item_type {
            return Err(prelude_eval_error(
                line,
                format!("sequence item has type `{}`, expected `{item_type}`", item.type_key()),
                "Sequence<T> evaluation is homogeneous and does not synthesize hidden Any",
            ));
        }
    }
    Ok(PreludeEvalValue::Sequence { item_type, items })
}

pub fn eval_evidence_boundary(description: impl Into<String>, kind: impl Into<String>) -> PreludeEvalValue {
    PreludeEvalValue::Evidence { description: description.into(), kind: kind.into() }
}

pub fn evaluate_standard_prelude(
    name: impl Into<String>,
    contract: &StandardPreludeContractReport,
    input: PreludeEvalValue,
    fuel_limit: usize,
    sources: &[&Passport],
    line: usize,
) -> Result<PreludeEvaluationReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "prelude evaluation name", line)?;

    let input_type = input.type_key();
    let input_render = input.render();
    let mut status = PreludeEvalStatus::Evaluated;
    let mut open_obligations = Vec::new();
    let mut result = None;
    let mut steps_used = 0usize;

    let mut max_trust = contract.max_trust;
    let mut max_provenance = contract.max_provenance;
    let mut has_axiom_taint = contract.has_axiom_taint;
    let mut has_oracle_taint = contract.has_oracle_taint;
    let mut has_unsafe_taint = contract.has_unsafe_taint;
    let (source_trust, source_provenance, source_axiom, source_oracle, source_unsafe) = source_taint(sources);
    max_trust = max_trust.max(source_trust);
    max_provenance = max_provenance.max(source_provenance);
    has_axiom_taint |= source_axiom;
    has_oracle_taint |= source_oracle;
    has_unsafe_taint |= source_unsafe;

    if contract.status != PreludeContractStatus::VerifiedChecked {
        status = PreludeEvalStatus::RejectedContract;
        open_obligations.push(format!(
            "standard prelude contract `{}` is {}, not verified_checked",
            contract.name, contract.status
        ));
    } else if input.contains_evidence_boundary() {
        status = PreludeEvalStatus::RejectedInput;
        open_obligations.push("proof/theorem/truth/runtime evidence cannot be evaluated as an ordinary prelude value".to_string());
    } else if input_type != contract.domain {
        status = PreludeEvalStatus::RejectedInput;
        open_obligations.push(format!(
            "input type mismatch for {}: expected `{}`, got `{input_type}`",
            contract.operation, contract.domain
        ));
    } else {
        let required_fuel = required_fuel(contract.operation, &input);
        if fuel_limit < required_fuel {
            status = PreludeEvalStatus::RejectedFuel;
            open_obligations.push(format!(
                "{} requires fuel {required_fuel}, but fuel_limit is {fuel_limit}",
                contract.operation
            ));
        } else {
            let eval = eval_operation(contract.operation, &input, &contract.codomain, line)?;
            steps_used = required_fuel;
            if contains_symbolic_value(&eval) {
                status = PreludeEvalStatus::SymbolicEvaluated;
                open_obligations.push("evaluation produced bounded symbolic application; no arbitrary user function body was executed".to_string());
            }
            result = Some(eval);
        }
    }

    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        open_obligations.push("prelude evaluation depends on Axiom/Oracle/Unsafe taint and is not clean deterministic Checked evaluation".to_string());
    }
    open_obligations.sort();
    open_obligations.dedup();

    let result_render = result.as_ref().map(PreludeEvalValue::render).unwrap_or_else(|| "<none>".to_string());
    let fingerprint = stable_fingerprint(&[
        "prelude-evaluation-v1".to_string(),
        name.clone(),
        contract.name.clone(),
        contract.operation.to_string(),
        input_render.clone(),
        result_render,
        status.to_string(),
        format!("steps={steps_used}"),
        format!("fuel={fuel_limit}"),
        format!("trust={max_trust:?}"),
    ]);

    Ok(PreludeEvaluationReport {
        name,
        operation: contract.operation,
        contract: contract.name.clone(),
        input_type,
        output_type: contract.codomain.clone(),
        input_render,
        result,
        status,
        steps_used,
        fuel_limit,
        open_obligations,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_evaluated_prelude(report: &PreludeEvaluationReport, line: usize) -> Result<(), Diagnostic> {
    match report.status {
        PreludeEvalStatus::Evaluated | PreludeEvalStatus::SymbolicEvaluated => Ok(()),
        PreludeEvalStatus::RejectedContract | PreludeEvalStatus::RejectedInput | PreludeEvalStatus::RejectedFuel => {
            Err(prelude_eval_error(
                line,
                format!("prelude evaluation `{}` is {}, not evaluated", report.name, report.status),
                "small-step prelude evaluation must be contract-verified, type-exact and fuel-bounded before downstream lowering can rely on it",
            ))
        }
    }
}

pub fn prelude_evaluation_passport(
    theory: &str,
    report: &PreludeEvaluationReport,
    sources: &[&Passport],
) -> Passport {
    let (source_trust, source_provenance, _, _, _) = source_taint(sources);
    Passport {
        ty: TypeKind::PreludeEvaluationReport {
            name: report.name.clone(),
            operation: report.operation.to_string(),
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
                "standard_prelude:evaluate:{}:{}:{}:steps={}:fingerprint={}",
                report.name, report.operation, report.status, report.steps_used, report.fingerprint
            ),
        ),
        location: LocationContext::local(),
    }
}

pub fn export_prelude_evaluation(report: &PreludeEvaluationReport) -> String {
    let mut out = String::new();
    out.push_str("prelude_evaluation: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("operation: {}\n", report.operation));
    out.push_str(&format!("contract: {}\n", report.contract));
    out.push_str(&format!("input_type: {}\n", report.input_type));
    out.push_str(&format!("output_type: {}\n", report.output_type));
    out.push_str(&format!("input: {}\n", report.input_render));
    out.push_str(&format!("result: {}\n", report.result.as_ref().map(PreludeEvalValue::render).unwrap_or_else(|| "<none>".to_string())));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("steps_used: {}\n", report.steps_used));
    out.push_str(&format!("fuel_limit: {}\n", report.fuel_limit));
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

fn eval_operation(
    operation: PreludeOperationKind,
    input: &PreludeEvalValue,
    codomain: &str,
    line: usize,
) -> Result<PreludeEvalValue, Diagnostic> {
    match operation {
        PreludeOperationKind::NatAdd => {
            let (lhs, rhs) = expect_product(input, line)?;
            match (lhs, rhs) {
                (PreludeEvalValue::Nat(a), PreludeEvalValue::Nat(b)) => Ok(PreludeEvalValue::Nat(a.saturating_add(*b))),
                _ => Err(prelude_eval_error(line, "nat.add expected Nat * Nat", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::NatEq => {
            let (lhs, rhs) = expect_product(input, line)?;
            match (lhs, rhs) {
                (PreludeEvalValue::Nat(a), PreludeEvalValue::Nat(b)) => Ok(PreludeEvalValue::Bool(a == b)),
                _ => Err(prelude_eval_error(line, "nat.eq expected Nat * Nat", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::BoolAnd => {
            let (lhs, rhs) = expect_product(input, line)?;
            match (lhs, rhs) {
                (PreludeEvalValue::Bool(a), PreludeEvalValue::Bool(b)) => Ok(PreludeEvalValue::Bool(*a && *b)),
                _ => Err(prelude_eval_error(line, "bool.and expected Bool * Bool", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::BoolNot => match input {
            PreludeEvalValue::Bool(value) => Ok(PreludeEvalValue::Bool(!*value)),
            _ => Err(prelude_eval_error(line, "bool.not expected Bool", "canonical prelude signatures should have rejected this before evaluation")),
        },
        PreludeOperationKind::OptionMap => {
            let (option, function) = expect_product(input, line)?;
            let function = expect_function_ref(function, line)?;
            match option {
                PreludeEvalValue::OptionSome { value, .. } => Ok(PreludeEvalValue::OptionSome {
                    item_type: function.codomain.to_string(),
                    value: Box::new(symbolic_apply(function.name, value, function.codomain)),
                }),
                PreludeEvalValue::OptionNone { .. } => Ok(PreludeEvalValue::OptionNone { item_type: function.codomain.to_string() }),
                _ => Err(prelude_eval_error(line, "option.map expected Option<T> * Function<T->U>", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::ResultMap => {
            let (result, function) = expect_product(input, line)?;
            let function = expect_function_ref(function, line)?;
            match result {
                PreludeEvalValue::ResultOk { err_type, value, .. } => Ok(PreludeEvalValue::ResultOk {
                    ok_type: function.codomain.to_string(),
                    err_type: err_type.clone(),
                    value: Box::new(symbolic_apply(function.name, value, function.codomain)),
                }),
                PreludeEvalValue::ResultErr { err_type, error, .. } => Ok(PreludeEvalValue::ResultErr {
                    ok_type: function.codomain.to_string(),
                    err_type: err_type.clone(),
                    error: error.clone(),
                }),
                _ => Err(prelude_eval_error(line, "result.map expected Result<T,E> * Function<T->U>", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::ListLength => match input {
            PreludeEvalValue::List { items, .. } => Ok(PreludeEvalValue::Nat(items.len() as u128)),
            _ => Err(prelude_eval_error(line, "list.length expected List<T>", "canonical prelude signatures should have rejected this before evaluation")),
        },
        PreludeOperationKind::SequenceLength => match input {
            PreludeEvalValue::Sequence { items, .. } => Ok(PreludeEvalValue::Nat(items.len() as u128)),
            _ => Err(prelude_eval_error(line, "sequence.length expected Sequence<T>", "canonical prelude signatures should have rejected this before evaluation")),
        },
        PreludeOperationKind::SequenceIndex => {
            let (sequence, index) = expect_product(input, line)?;
            match (sequence, index) {
                (PreludeEvalValue::Sequence { item_type, items }, PreludeEvalValue::Nat(index)) => {
                    let Ok(index) = usize::try_from(*index) else {
                        return Ok(PreludeEvalValue::OptionNone { item_type: item_type.clone() });
                    };
                    match items.get(index) {
                        Some(value) => Ok(PreludeEvalValue::OptionSome { item_type: item_type.clone(), value: Box::new(value.clone()) }),
                        None => Ok(PreludeEvalValue::OptionNone { item_type: item_type.clone() }),
                    }
                }
                _ => Err(prelude_eval_error(line, "sequence.index expected Sequence<T> * Nat", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::ListMap => {
            let (list, function) = expect_product(input, line)?;
            let function = expect_function_ref(function, line)?;
            match list {
                PreludeEvalValue::List { items, .. } => Ok(PreludeEvalValue::List {
                    item_type: function.codomain.to_string(),
                    items: items.iter().map(|item| symbolic_apply(function.name, item, function.codomain)).collect(),
                }),
                _ => Err(prelude_eval_error(line, "list.map expected List<T> * Function<T->U>", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::SequenceMap => {
            let (sequence, function) = expect_product(input, line)?;
            let function = expect_function_ref(function, line)?;
            match sequence {
                PreludeEvalValue::Sequence { items, .. } => Ok(PreludeEvalValue::Sequence {
                    item_type: function.codomain.to_string(),
                    items: items.iter().map(|item| symbolic_apply(function.name, item, function.codomain)).collect(),
                }),
                _ => Err(prelude_eval_error(line, "sequence.map expected Sequence<T> * Function<T->U>", "canonical prelude signatures should have rejected this before evaluation")),
            }
        }
        PreludeOperationKind::ListFold | PreludeOperationKind::SequenceFold => {
            let len = match operation {
                PreludeOperationKind::ListFold => match input {
                    PreludeEvalValue::Product(collection, _) => match collection.as_ref() {
                        PreludeEvalValue::List { items, .. } => items.len(),
                        _ => 0,
                    },
                    _ => 0,
                },
                PreludeOperationKind::SequenceFold => match input {
                    PreludeEvalValue::Product(collection, _) => match collection.as_ref() {
                        PreludeEvalValue::Sequence { items, .. } => items.len(),
                        _ => 0,
                    },
                    _ => 0,
                },
                _ => 0,
            };
            Ok(PreludeEvalValue::Symbolic {
                expr: format!("{}({};len={len})", operation, input.render()),
                ty: codomain.to_string(),
            })
        }
    }
}

fn required_fuel(operation: PreludeOperationKind, input: &PreludeEvalValue) -> usize {
    match operation {
        PreludeOperationKind::ListMap => collection_len_from_product(input, true).unwrap_or(1),
        PreludeOperationKind::SequenceMap => collection_len_from_product(input, false).unwrap_or(1),
        PreludeOperationKind::ListFold => collection_len_from_nested_product(input, true).unwrap_or(1),
        PreludeOperationKind::SequenceFold => collection_len_from_nested_product(input, false).unwrap_or(1),
        PreludeOperationKind::NatAdd
        | PreludeOperationKind::NatEq
        | PreludeOperationKind::BoolAnd
        | PreludeOperationKind::BoolNot
        | PreludeOperationKind::OptionMap
        | PreludeOperationKind::ResultMap
        | PreludeOperationKind::ListLength
        | PreludeOperationKind::SequenceLength
        | PreludeOperationKind::SequenceIndex => 1,
    }
}

fn collection_len_from_product(input: &PreludeEvalValue, list: bool) -> Option<usize> {
    let PreludeEvalValue::Product(lhs, _) = input else { return None; };
    match (list, lhs.as_ref()) {
        (true, PreludeEvalValue::List { items, .. }) | (false, PreludeEvalValue::Sequence { items, .. }) => Some(items.len()),
        _ => None,
    }
}

fn collection_len_from_nested_product(input: &PreludeEvalValue, list: bool) -> Option<usize> {
    let PreludeEvalValue::Product(lhs, _) = input else { return None; };
    match (list, lhs.as_ref()) {
        (true, PreludeEvalValue::List { items, .. }) | (false, PreludeEvalValue::Sequence { items, .. }) => Some(items.len()),
        _ => None,
    }
}

fn expect_product(input: &PreludeEvalValue, line: usize) -> Result<(&PreludeEvalValue, &PreludeEvalValue), Diagnostic> {
    match input {
        PreludeEvalValue::Product(lhs, rhs) => Ok((lhs.as_ref(), rhs.as_ref())),
        _ => Err(prelude_eval_error(line, "expected Product input", "canonical prelude signatures should have rejected this before evaluation")),
    }
}

struct FunctionParts<'a> {
    name: &'a str,
    codomain: &'a str,
}

fn expect_function_ref(input: &PreludeEvalValue, line: usize) -> Result<FunctionParts<'_>, Diagnostic> {
    match input {
        PreludeEvalValue::FunctionRef { name, codomain, .. } => Ok(FunctionParts { name, codomain }),
        _ => Err(prelude_eval_error(line, "expected FunctionRef", "map/fold evaluation never executes arbitrary user code; it records bounded symbolic application only")),
    }
}

fn symbolic_apply(function: &str, value: &PreludeEvalValue, ty: &str) -> PreludeEvalValue {
    PreludeEvalValue::Symbolic {
        expr: format!("{function}({})", value.render()),
        ty: ty.to_string(),
    }
}

fn contains_symbolic_value(value: &PreludeEvalValue) -> bool {
    match value {
        PreludeEvalValue::Symbolic { .. } => true,
        PreludeEvalValue::Product(lhs, rhs) => contains_symbolic_value(lhs) || contains_symbolic_value(rhs),
        PreludeEvalValue::OptionSome { value, .. } => contains_symbolic_value(value),
        PreludeEvalValue::ResultOk { value, .. } => contains_symbolic_value(value),
        PreludeEvalValue::ResultErr { error, .. } => contains_symbolic_value(error),
        PreludeEvalValue::List { items, .. } | PreludeEvalValue::Sequence { items, .. } => {
            items.iter().any(contains_symbolic_value)
        }
        PreludeEvalValue::Nat(_)
        | PreludeEvalValue::Bool(_)
        | PreludeEvalValue::Text(_)
        | PreludeEvalValue::OptionNone { .. }
        | PreludeEvalValue::FunctionRef { .. }
        | PreludeEvalValue::Evidence { .. } => false,
    }
}

fn is_forbidden_evidence_kind(kind: &str) -> bool {
    kind.contains("Proof") || kind.contains("Theorem") || kind.contains("Truth") || kind.contains("RuntimeWitness")
}

fn validate_identifier(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return Err(prelude_eval_error(line, format!("{label} is empty"), "prelude evaluation reports require stable ASCII identifiers"));
    };
    if !(first.is_ascii_alphabetic() || first == '_') || !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(prelude_eval_error(line, format!("invalid {label} `{text}`"), "use a stable identifier such as eval_nat_add_1"));
    }
    Ok(())
}

fn validate_type_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(prelude_eval_error(line, format!("{label} is empty"), "prelude evaluation values must carry explicit type identities"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(prelude_eval_error(line, format!("{label} contains a newline"), "prelude evaluation type identities must be single-line audit keys"));
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
    format!("peval-{h:016x}")
}

fn prelude_eval_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::PreludeEvaluationError, Some(line), message.into()).with_help(help.into())
}
