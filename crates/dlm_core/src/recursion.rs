use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::function_contract::{
    FunctionContractReport, FunctionContractStatus, FunctionPurity, FunctionTotality,
};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum RecursionStatus {
    VerifiedWellFounded,
    Downgraded,
    Open,
    RejectedFuelExceeded,
    RejectedMeasure,
    RejectedContract,
}

impl fmt::Display for RecursionStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RecursionStatus::VerifiedWellFounded => write!(f, "verified_well_founded"),
            RecursionStatus::Downgraded => write!(f, "downgraded"),
            RecursionStatus::Open => write!(f, "open"),
            RecursionStatus::RejectedFuelExceeded => write!(f, "rejected_fuel_exceeded"),
            RecursionStatus::RejectedMeasure => write!(f, "rejected_measure"),
            RecursionStatus::RejectedContract => write!(f, "rejected_contract"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum RecursionMeasureKind {
    NatDecreasing,
    StructuralSubterm,
    Lexicographic,
    FuelOnly,
    Unknown,
}

impl fmt::Display for RecursionMeasureKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RecursionMeasureKind::NatDecreasing => write!(f, "nat_decreasing"),
            RecursionMeasureKind::StructuralSubterm => write!(f, "structural_subterm"),
            RecursionMeasureKind::Lexicographic => write!(f, "lexicographic"),
            RecursionMeasureKind::FuelOnly => write!(f, "fuel_only"),
            RecursionMeasureKind::Unknown => write!(f, "unknown"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecursionSchemeReport {
    pub name: String,
    pub subject: String,
    pub argument_type: String,
    pub result_type: String,
    pub measure: RecursionMeasureKind,
    pub initial_fuel: usize,
    pub status: RecursionStatus,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecursiveCallReport {
    pub scheme: String,
    pub argument_type: String,
    pub previous_measure: u64,
    pub next_measure: u64,
    pub fuel_before: usize,
    pub fuel_after: usize,
    pub status: RecursionStatus,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn recursion_scheme(
    name: impl Into<String>,
    subject: &FunctionContractReport,
    measure: RecursionMeasureKind,
    initial_fuel: usize,
    well_founded_evidence: &[&Passport],
    line: usize,
) -> Result<RecursionSchemeReport, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "recursion scheme name", line)?;
    if initial_fuel == 0 {
        return Err(recursion_error(
            line,
            "recursion scheme must declare positive initial fuel",
            "use at least one fuel step, even when a well-founded measure is available",
        ));
    }

    let mut status = RecursionStatus::VerifiedWellFounded;
    let mut open_obligations = Vec::new();

    if subject.status == FunctionContractStatus::Rejected {
        status = RecursionStatus::RejectedContract;
        open_obligations.push("function contract is rejected; recursion cannot be admitted".to_string());
    } else if subject.status != FunctionContractStatus::Verified {
        status = status.max(RecursionStatus::Downgraded);
        open_obligations.push(format!(
            "function contract is {}, so recursion cannot be fully verified",
            subject.status
        ));
    }

    if subject.purity != FunctionPurity::Pure || subject.totality != FunctionTotality::Total || !subject.effects.is_empty() {
        status = status.max(RecursionStatus::Downgraded);
        open_obligations.push("recursive subject is not pure total effect-free code".to_string());
    }

    let has_wf_evidence = well_founded_evidence.iter().any(|p| is_static_evidence(p));
    match measure {
        RecursionMeasureKind::NatDecreasing
        | RecursionMeasureKind::StructuralSubterm
        | RecursionMeasureKind::Lexicographic => {
            if !has_wf_evidence {
                status = status.max(RecursionStatus::Open);
                open_obligations.push("well-founded measure is declared but no StaticProof/Theorem evidence was supplied".to_string());
            }
        }
        RecursionMeasureKind::FuelOnly => {
            status = status.max(RecursionStatus::Open);
            open_obligations.push("fuel-only recursion terminates by budget, not by a mathematical well-founded proof".to_string());
        }
        RecursionMeasureKind::Unknown => {
            status = RecursionStatus::RejectedMeasure;
            open_obligations.push("unknown recursion measure is rejected; declare nat_decreasing, structural_subterm, lexicographic, or fuel_only".to_string());
        }
    }

    let (mut max_trust, mut max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        evidence_taint(subject, well_founded_evidence);
    if subject.has_unsafe_taint || has_unsafe_taint {
        max_trust = max_trust.max(TrustLevel::Unsafe);
        max_provenance = max_provenance.max(Provenance::UnsafeExternal);
    }
    if subject.has_oracle_taint || has_oracle_taint {
        max_trust = max_trust.max(TrustLevel::Oracle);
        max_provenance = max_provenance.max(Provenance::OracleInput);
    }
    if subject.has_axiom_taint || has_axiom_taint {
        max_trust = max_trust.max(TrustLevel::Axiom);
        max_provenance = max_provenance.max(Provenance::BuiltinKnown);
    }

    let has_axiom_taint = has_axiom_taint || subject.has_axiom_taint;
    let has_oracle_taint = has_oracle_taint || subject.has_oracle_taint;
    let has_unsafe_taint = has_unsafe_taint || subject.has_unsafe_taint;
    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        status = status.max(RecursionStatus::Downgraded);
        open_obligations.push("recursion scheme preserves Axiom/Oracle/Unsafe taint and cannot be treated as clean verified core recursion".to_string());
    }

    let fingerprint = fingerprint(&[
        "recursion_scheme",
        &name,
        &subject.function,
        &subject.domain,
        &subject.codomain,
        &measure.to_string(),
        &initial_fuel.to_string(),
        &status.to_string(),
        &format!("{:?}", max_trust),
    ]);

    Ok(RecursionSchemeReport {
        name,
        subject: subject.function.clone(),
        argument_type: subject.domain.clone(),
        result_type: subject.codomain.clone(),
        measure,
        initial_fuel,
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

pub fn recursive_call(
    scheme: &RecursionSchemeReport,
    argument: &Passport,
    argument_type: impl Into<String>,
    previous_measure: u64,
    next_measure: u64,
    fuel_before: usize,
    line: usize,
) -> Result<RecursiveCallReport, Diagnostic> {
    let argument_type = argument_type.into();
    validate_text(&argument_type, "recursive call argument type", line)?;
    ordinary_recursion_argument(argument, line)?;

    if argument_type != scheme.argument_type {
        return Err(recursion_error(
            line,
            format!(
                "recursive call argument type {argument_type} does not match scheme argument type {}",
                scheme.argument_type
            ),
            "DLM recursion does not use implicit coercion at recursive-call boundaries",
        ));
    }

    let mut status = scheme.status;
    let mut open_obligations = Vec::new();
    let fuel_after = fuel_before.saturating_sub(1);
    if fuel_before == 0 {
        status = RecursionStatus::RejectedFuelExceeded;
        open_obligations.push("recursive call has no remaining fuel".to_string());
    }

    match scheme.measure {
        RecursionMeasureKind::NatDecreasing
        | RecursionMeasureKind::StructuralSubterm
        | RecursionMeasureKind::Lexicographic => {
            if next_measure >= previous_measure {
                status = RecursionStatus::RejectedMeasure;
                open_obligations.push(format!(
                    "measure did not strictly decrease: previous={previous_measure}, next={next_measure}"
                ));
            }
        }
        RecursionMeasureKind::FuelOnly => {
            if fuel_before > 0 {
                status = status.max(RecursionStatus::Open);
                open_obligations.push("fuel-only recursive call has operational budget but no mathematical decrease proof".to_string());
            }
        }
        RecursionMeasureKind::Unknown => {
            status = RecursionStatus::RejectedMeasure;
            open_obligations.push("unknown measure cannot justify recursive calls".to_string());
        }
    }

    let (arg_trust, arg_provenance, arg_axiom, arg_oracle, arg_unsafe) = taint_summary(&[argument]);
    let max_trust = scheme.max_trust.max(arg_trust);
    let max_provenance = scheme.max_provenance.max(arg_provenance);
    let has_axiom_taint = scheme.has_axiom_taint || arg_axiom;
    let has_oracle_taint = scheme.has_oracle_taint || arg_oracle;
    let has_unsafe_taint = scheme.has_unsafe_taint || arg_unsafe;

    let fingerprint = fingerprint(&[
        "recursive_call",
        &scheme.name,
        &argument_type,
        &previous_measure.to_string(),
        &next_measure.to_string(),
        &fuel_before.to_string(),
        &fuel_after.to_string(),
        &status.to_string(),
        &format!("{:?}", max_trust),
    ]);

    Ok(RecursiveCallReport {
        scheme: scheme.name.clone(),
        argument_type,
        previous_measure,
        next_measure,
        fuel_before,
        fuel_after,
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

pub fn require_verified_well_founded_recursion(
    scheme: &RecursionSchemeReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if scheme.status == RecursionStatus::VerifiedWellFounded {
        Ok(())
    } else {
        Err(recursion_error(
            line,
            format!("recursion scheme is {}, not verified_well_founded", scheme.status),
            "only verified_well_founded recursion may be used as a total static recursion assumption",
        ))
    }
}

pub fn require_accepted_recursive_call(call: &RecursiveCallReport, line: usize) -> Result<(), Diagnostic> {
    match call.status {
        RecursionStatus::VerifiedWellFounded | RecursionStatus::Downgraded | RecursionStatus::Open => Ok(()),
        RecursionStatus::RejectedFuelExceeded | RecursionStatus::RejectedMeasure | RecursionStatus::RejectedContract => Err(
            recursion_error(
                line,
                format!("recursive call is {}", call.status),
                "fix the fuel, measure decrease, or function contract before accepting this recursive edge",
            ),
        ),
    }
}

pub fn recursion_scheme_passport(theory: &str, report: &RecursionSchemeReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::RecursionScheme {
            name: report.name.clone(),
            measure: report.measure.to_string(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Recursive,
        capabilities: recursion_caps(),
        cost: CostClass::Recursive,
        trust: report.max_trust,
        provenance: report.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "recursion:scheme"),
        location: LocationContext::local(),
    }
}

pub fn recursive_call_passport(theory: &str, call: &RecursiveCallReport, sources: &[&Passport]) -> Passport {
    Passport {
        ty: TypeKind::RecursiveCall {
            scheme: call.scheme.clone(),
            status: call.status.to_string(),
            fuel_after: call.fuel_after,
        },
        construction: ConstructionMode::Recursive,
        capabilities: recursion_caps(),
        cost: CostClass::Recursive,
        trust: call.max_trust,
        provenance: call.max_provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "recursion:call"),
        location: LocationContext::local(),
    }
}

pub fn recursion_report_passport(theory: &str, subject: impl Into<String>, status: RecursionStatus, sources: &[&Passport]) -> Passport {
    let (trust, provenance, _, _, _) = taint_summary(sources);
    Passport {
        ty: TypeKind::RecursionReport {
            subject: subject.into(),
            status: status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: recursion_caps(),
        cost: CostClass::SmallFinite,
        trust,
        provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: merge_history(sources, "recursion:report"),
        location: LocationContext::local(),
    }
}

pub fn export_recursion_scheme(report: &RecursionSchemeReport) -> String {
    let mut out = String::new();
    out.push_str("recursion_scheme_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("argument_type: {}\n", report.argument_type));
    out.push_str(&format!("result_type: {}\n", report.result_type));
    out.push_str(&format!("measure: {}\n", report.measure));
    out.push_str(&format!("initial_fuel: {}\n", report.initial_fuel));
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

pub fn export_recursive_call(call: &RecursiveCallReport) -> String {
    let mut out = String::new();
    out.push_str("recursive_call_report: v1\n");
    out.push_str(&format!("scheme: {}\n", call.scheme));
    out.push_str(&format!("argument_type: {}\n", call.argument_type));
    out.push_str(&format!("previous_measure: {}\n", call.previous_measure));
    out.push_str(&format!("next_measure: {}\n", call.next_measure));
    out.push_str(&format!("fuel_before: {}\n", call.fuel_before));
    out.push_str(&format!("fuel_after: {}\n", call.fuel_after));
    out.push_str(&format!("status: {}\n", call.status));
    out.push_str("open_obligations:\n");
    for obligation in &call.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("max_trust: {:?}\n", call.max_trust));
    out.push_str(&format!("max_provenance: {:?}\n", call.max_provenance));
    out.push_str(&format!("has_axiom_taint: {}\n", call.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", call.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", call.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", call.fingerprint));
    out
}

fn ordinary_recursion_argument(passport: &Passport, line: usize) -> Result<(), Diagnostic> {
    if is_forbidden_recursion_value(passport) {
        return Err(recursion_error(
            line,
            format!("{} cannot be used as an ordinary recursive-call argument", passport.ty),
            "recursion over proof/truth/runtime evidence must go through explicit proof-kernel or reflection machinery",
        ));
    }
    Ok(())
}

fn is_forbidden_recursion_value(passport: &Passport) -> bool {
    matches!(
        passport.ty,
        TypeKind::ProofTerm { .. }
            | TypeKind::StaticProof(_)
            | TypeKind::Theorem { .. }
            | TypeKind::TruthClaim { .. }
            | TypeKind::RuntimeWitness(_)
            | TypeKind::Provable { .. }
            | TypeKind::EqProof { .. }
            | TypeKind::RewriteCertificate { .. }
    )
}

fn is_static_evidence(passport: &Passport) -> bool {
    matches!(passport.ty, TypeKind::StaticProof(_) | TypeKind::Theorem { .. })
}

fn evidence_taint(subject: &FunctionContractReport, evidence: &[&Passport]) -> (TrustLevel, Provenance, bool, bool, bool) {
    let (trust, provenance, axiom, oracle, unsafe_) = taint_summary(evidence);
    (
        trust.max(subject.max_trust),
        provenance.max(subject.max_provenance),
        axiom || subject.has_axiom_taint,
        oracle || subject.has_oracle_taint,
        unsafe_ || subject.has_unsafe_taint,
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
        has_oracle_taint |= source.trust >= TrustLevel::Oracle;
        has_unsafe_taint |= source.trust >= TrustLevel::Unsafe;
    }
    (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint)
}

fn merge_history(sources: &[&Passport], event: &str) -> HistoryChain {
    HistoryChain::merge_many(sources.iter().map(|p| &p.history), event)
}

fn recursion_caps() -> CapabilitySet {
    CapabilitySet::from([Capability::CanSymbolicPrint, Capability::CanSerializeForMigration])
}

fn validate_identifier(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(recursion_error(
            line,
            format!("{label} must not be empty"),
            "give every recursion boundary a stable audit identifier",
        ));
    }
    if value.chars().any(char::is_whitespace) {
        return Err(recursion_error(
            line,
            format!("{label} must not contain whitespace: {value}"),
            "use a stable identifier such as nat_rec or list_fold_rec",
        ));
    }
    Ok(())
}

fn validate_text(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(recursion_error(line, format!("{label} must not be empty"), "use an explicit non-empty value"));
    }
    Ok(())
}

fn recursion_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::RecursionBoundaryError, Some(line), message.into()).with_help(help.into())
}

fn fingerprint(parts: &[&str]) -> String {
    let mut acc: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            acc ^= u64::from(*byte);
            acc = acc.wrapping_mul(0x100000001b3);
        }
        acc ^= 0xff;
        acc = acc.wrapping_mul(0x100000001b3);
    }
    format!("rec-{acc:016x}")
}
