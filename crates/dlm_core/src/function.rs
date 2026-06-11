use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::logic::BoundVariable;
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ApplicationStatus {
    Applied,
    RejectedDomainMismatch,
}

impl fmt::Display for ApplicationStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ApplicationStatus::Applied => write!(f, "applied"),
            ApplicationStatus::RejectedDomainMismatch => write!(f, "rejected_domain_mismatch"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunctionTypeReport {
    pub domain: String,
    pub codomain: String,
    pub is_total: bool,
    pub is_pure: bool,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LambdaTermReport {
    pub parameter: BoundVariable,
    pub body: String,
    pub domain: String,
    pub codomain: String,
    pub captures: Vec<String>,
    pub is_total: bool,
    pub is_pure: bool,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ApplicationTermReport {
    pub function: String,
    pub argument: String,
    pub expected_domain: String,
    pub argument_domain: String,
    pub result: String,
    pub status: ApplicationStatus,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn function_type(
    domain: impl Into<String>,
    codomain: impl Into<String>,
    is_total: bool,
    is_pure: bool,
    sources: &[&Passport],
    line: usize,
) -> Result<FunctionTypeReport, Diagnostic> {
    let domain = domain.into();
    let codomain = codomain.into();
    validate_type_text(&domain, "function domain", line)?;
    validate_type_text(&codomain, "function codomain", line)?;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "function-type-v1".to_string(),
        domain.clone(),
        codomain.clone(),
        format!("total={is_total}"),
        format!("pure={is_pure}"),
        format!("trust={max_trust:?}"),
    ]);
    Ok(FunctionTypeReport {
        domain,
        codomain,
        is_total,
        is_pure,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn lambda_term(
    parameter: BoundVariable,
    body: impl Into<String>,
    declared_type: &FunctionTypeReport,
    captures: Vec<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<LambdaTermReport, Diagnostic> {
    let body = body.into();
    validate_formula_text(&body, "lambda body", line)?;
    if parameter.domain != declared_type.domain {
        return Err(function_error(
            line,
            format!(
                "lambda parameter `{}` has domain `{}`, but declared function domain is `{}`",
                parameter.name, parameter.domain, declared_type.domain
            ),
            "lambda terms must expose their domain/codomain contract before application can be checked",
        ));
    }
    let mut captures = captures;
    captures.sort();
    captures.dedup();
    for capture in &captures {
        validate_identifier(capture, "lambda capture", line)?;
        if capture == &parameter.name {
            return Err(function_error(
                line,
                format!("lambda capture `{capture}` is shadowed by the parameter"),
                "captured variables and bound parameters must remain separate; otherwise substitution/application would be ambiguous",
            ));
        }
    }
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let max_trust = max_trust.max(declared_type.max_trust);
    let max_provenance = max_provenance.max(declared_type.max_provenance);
    let has_axiom_taint = has_axiom_taint || declared_type.has_axiom_taint;
    let has_oracle_taint = has_oracle_taint || declared_type.has_oracle_taint;
    let has_unsafe_taint = has_unsafe_taint || declared_type.has_unsafe_taint;
    let fingerprint = stable_fingerprint(&[
        "lambda-term-v1".to_string(),
        parameter.name.clone(),
        parameter.domain.clone(),
        declared_type.codomain.clone(),
        body.clone(),
        format!("captures={captures:?}"),
        format!("total={}", declared_type.is_total),
        format!("pure={}", declared_type.is_pure),
        format!("trust={max_trust:?}"),
    ]);
    Ok(LambdaTermReport {
        parameter,
        body,
        domain: declared_type.domain.clone(),
        codomain: declared_type.codomain.clone(),
        captures,
        is_total: declared_type.is_total,
        is_pure: declared_type.is_pure,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn application_term(
    function: &Passport,
    argument: &Passport,
    argument_repr: impl Into<String>,
    line: usize,
) -> Result<ApplicationTermReport, Diagnostic> {
    let argument_repr = argument_repr.into();
    validate_term_text(&argument_repr, "application argument", line)?;
    let descriptor = function_descriptor_from_passport(function, line)?;
    let argument_domain = value_domain_from_passport(argument, line)?;
    ensure_argument_allowed(argument, line)?;
    let status = if descriptor.domain == argument_domain {
        ApplicationStatus::Applied
    } else {
        ApplicationStatus::RejectedDomainMismatch
    };
    let result = if status == ApplicationStatus::Applied {
        format!("{}({}) : {}", descriptor.name, argument_repr, descriptor.codomain)
    } else {
        format!("rejected({}({}))", descriptor.name, argument_repr)
    };
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(&[function, argument]);
    let fingerprint = stable_fingerprint(&[
        "application-term-v1".to_string(),
        descriptor.name.clone(),
        argument_repr.clone(),
        descriptor.domain.clone(),
        argument_domain.clone(),
        descriptor.codomain.clone(),
        status.to_string(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(ApplicationTermReport {
        function: descriptor.name,
        argument: argument_repr,
        expected_domain: descriptor.domain,
        argument_domain,
        result,
        status,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_function_type(passport: &Passport, line: usize) -> Result<(String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::FunctionType { domain, codomain } => Ok((domain.clone(), codomain.clone())),
        _ => Err(function_error(
            line,
            format!("expected FunctionType, got {}", passport.ty),
            "ordinary function type objects are not theorems, proofs or runtime witnesses",
        )),
    }
}

pub fn require_lambda_term(passport: &Passport, line: usize) -> Result<(String, String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::LambdaTerm { parameter, domain, body } => Ok((parameter.clone(), domain.clone(), body.clone())),
        _ => Err(function_error(
            line,
            format!("expected LambdaTerm, got {}", passport.ty),
            "lambda terms are function terms; applying proof/theorem/truth objects requires explicit proof-kernel rules, not function application",
        )),
    }
}

pub fn function_type_passport(theory: &str, report: &FunctionTypeReport, sources: &[&Passport]) -> Passport {
    function_passport(
        theory,
        TypeKind::FunctionType { domain: report.domain.clone(), codomain: report.codomain.clone() },
        report.max_trust,
        report.max_provenance,
        merge_history(sources, format!("function:type:{}->{}:fingerprint={}", report.domain, report.codomain, report.fingerprint)),
    )
}

pub fn lambda_term_passport(theory: &str, report: &LambdaTermReport, sources: &[&Passport]) -> Passport {
    function_passport(
        theory,
        TypeKind::LambdaTerm {
            parameter: report.parameter.name.clone(),
            domain: report.domain.clone(),
            body: report.body.clone(),
        },
        report.max_trust,
        report.max_provenance,
        merge_history(sources, format!("function:lambda:{}:{}->{}:fingerprint={}", report.parameter.name, report.domain, report.codomain, report.fingerprint)),
    )
}

pub fn application_term_passport(theory: &str, report: &ApplicationTermReport, sources: &[&Passport]) -> Passport {
    function_passport(
        theory,
        TypeKind::ApplicationTerm {
            function: report.function.clone(),
            argument: report.argument.clone(),
            result: report.result.clone(),
            status: report.status.to_string(),
        },
        report.max_trust,
        report.max_provenance,
        merge_history(sources, format!("function:application:{}:fingerprint={}", report.status, report.fingerprint)),
    )
}

pub fn export_function_type(report: &FunctionTypeReport) -> String {
    let mut out = String::new();
    out.push_str("function_type_report: v1\n");
    out.push_str(&format!("domain: {}\n", report.domain));
    out.push_str(&format!("codomain: {}\n", report.codomain));
    out.push_str(&format!("total: {}\n", report.is_total));
    out.push_str(&format!("pure: {}\n", report.is_pure));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

pub fn export_lambda_term(report: &LambdaTermReport) -> String {
    let mut out = String::new();
    out.push_str("lambda_term_report: v1\n");
    out.push_str(&format!("parameter: {}\n", report.parameter.name));
    out.push_str(&format!("domain: {}\n", report.domain));
    out.push_str(&format!("codomain: {}\n", report.codomain));
    out.push_str(&format!("body: {}\n", report.body));
    out.push_str(&format!("captures: {:?}\n", report.captures));
    out.push_str(&format!("total: {}\n", report.is_total));
    out.push_str(&format!("pure: {}\n", report.is_pure));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

pub fn export_application_term(report: &ApplicationTermReport) -> String {
    let mut out = String::new();
    out.push_str("application_term_report: v1\n");
    out.push_str(&format!("function: {}\n", report.function));
    out.push_str(&format!("argument: {}\n", report.argument));
    out.push_str(&format!("expected_domain: {}\n", report.expected_domain));
    out.push_str(&format!("argument_domain: {}\n", report.argument_domain));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("result: {}\n", report.result));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct FunctionDescriptor {
    name: String,
    domain: String,
    codomain: String,
}

fn function_descriptor_from_passport(passport: &Passport, line: usize) -> Result<FunctionDescriptor, Diagnostic> {
    match &passport.ty {
        TypeKind::FunctionType { domain, codomain } => Ok(FunctionDescriptor {
            name: format!("fn:{}->{}", domain, codomain),
            domain: domain.clone(),
            codomain: codomain.clone(),
        }),
        TypeKind::LambdaTerm { parameter, domain, body } => Ok(FunctionDescriptor {
            name: format!("lambda:{parameter}:{domain}. {body}"),
            domain: domain.clone(),
            codomain: "Unknown".to_string(),
        }),
        TypeKind::Theorem { .. }
        | TypeKind::Statement { .. }
        | TypeKind::Goal { .. }
        | TypeKind::Hypothesis { .. }
        | TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. } => Err(function_error(
            line,
            format!("{} cannot be used as an ordinary function", passport.ty),
            "function application must not silently consume theorem/proof/truth/provability/runtime/reflection evidence",
        )),
        _ => Err(function_error(
            line,
            format!("{} is not a function passport", passport.ty),
            "use FunctionType or LambdaTerm as the ordinary function/application source",
        )),
    }
}

fn ensure_argument_allowed(passport: &Passport, line: usize) -> Result<(), Diagnostic> {
    match &passport.ty {
        TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. }
        | TypeKind::Theorem { .. } => Err(function_error(
            line,
            format!("{} cannot be used as an ordinary function argument", passport.ty),
            "proof/theorem/truth-like evidence must enter ordinary mathematics through explicit proof-kernel or extraction rules",
        )),
        _ => Ok(()),
    }
}

fn value_domain_from_passport(passport: &Passport, _line: usize) -> Result<String, Diagnostic> {
    let domain = match &passport.ty {
        TypeKind::Nat => "Nat".to_string(),
        TypeKind::Bool => "Bool".to_string(),
        TypeKind::Bytes => "Bytes".to_string(),
        TypeKind::Text => "Text".to_string(),
        TypeKind::Prop { .. } => "Prop".to_string(),
        TypeKind::LogicalFormula { .. } | TypeKind::QuantifiedFormula { .. } => "Prop".to_string(),
        TypeKind::FunctionType { domain, codomain } => format!("FunctionType<{}->{}>", domain, codomain),
        TypeKind::LambdaTerm { domain, .. } => format!("LambdaTerm<{}>", domain),
        other => other.to_string(),
    };
    Ok(domain)
}

fn function_passport(
    theory: &str,
    ty: TypeKind,
    trust: TrustLevel,
    provenance: Provenance,
    history: HistoryChain,
) -> Passport {
    Passport {
        ty,
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanCompareByProof,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: trust.max(TrustLevel::Builtin),
        provenance: provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

fn merge_history(sources: &[&Passport], event: impl Into<String>) -> HistoryChain {
    if sources.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(sources.iter().map(|source| &source.history), event)
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
        has_oracle_taint |= source.trust >= TrustLevel::Oracle || source.provenance == Provenance::OracleInput;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe || source.provenance == Provenance::UnsafeExternal;
    }
    (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint)
}

fn validate_type_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(function_error(line, format!("{label} is empty"), "function types require explicit domain and codomain"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(function_error(line, format!("{label} contains a newline"), "function type identities must remain single-line audit keys"));
    }
    Ok(())
}

fn validate_formula_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(function_error(line, format!("{label} is empty"), "function/lambda formulas must have explicit textual identity"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(function_error(line, format!("{label} contains a newline"), "formula identities must remain stable single-line audit keys in this MVP"));
    }
    Ok(())
}

fn validate_term_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(function_error(line, format!("{label} is empty"), "application terms require explicit argument identity"));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(function_error(line, format!("{label} contains a newline"), "application identities must remain stable single-line audit keys"));
    }
    Ok(())
}

fn validate_identifier(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return Err(function_error(line, format!("{label} is empty"), "identifiers must be explicit"));
    };
    if !(first.is_ascii_alphabetic() || first == '_') || !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(function_error(line, format!("invalid {label} `{text}`"), "identifiers must use MVP ASCII identifier syntax"));
    }
    Ok(())
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

fn function_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::FunctionTermError, Some(line), message).with_help(help)
}
