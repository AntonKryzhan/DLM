use std::collections::BTreeSet;
use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::logic::{formula_from_passport, BoundVariable, QuantifiedFormula};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum VariableOccurrenceKind {
    Free,
    Bound,
}

impl fmt::Display for VariableOccurrenceKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            VariableOccurrenceKind::Free => write!(f, "free"),
            VariableOccurrenceKind::Bound => write!(f, "bound"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum AlphaEquivalenceStatus {
    Equivalent,
    NotEquivalent,
}

impl fmt::Display for AlphaEquivalenceStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AlphaEquivalenceStatus::Equivalent => write!(f, "equivalent"),
            AlphaEquivalenceStatus::NotEquivalent => write!(f, "not_equivalent"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SubstitutionStatus {
    Applied,
    BlockedByBinder,
    RejectedCaptureRisk,
}

impl fmt::Display for SubstitutionStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SubstitutionStatus::Applied => write!(f, "applied"),
            SubstitutionStatus::BlockedByBinder => write!(f, "blocked_by_binder"),
            SubstitutionStatus::RejectedCaptureRisk => write!(f, "rejected_capture_risk"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VariableOccurrence {
    pub name: String,
    pub kind: VariableOccurrenceKind,
    pub domain: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VariableScopeReport {
    pub subject: String,
    pub free_variables: Vec<String>,
    pub bound_variables: Vec<VariableOccurrence>,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AlphaEquivalenceReport {
    pub lhs: String,
    pub rhs: String,
    pub status: AlphaEquivalenceStatus,
    pub canonical_lhs: String,
    pub canonical_rhs: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SubstitutionReport {
    pub source: String,
    pub variable: String,
    pub replacement: String,
    pub result: String,
    pub status: SubstitutionStatus,
    pub free_variables_before: Vec<String>,
    pub free_variables_after: Vec<String>,
    pub capture_risk_variables: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn variable_scope_report(subject: impl Into<String>, line: usize) -> Result<VariableScopeReport, Diagnostic> {
    let subject = subject.into();
    validate_formula_text(&subject, "scope subject", line)?;
    let parsed = parse_scoped_formula(&subject, line)?;
    let free_variables = sorted_strings(parsed.free_variables());
    let bound_variables = parsed.bound_occurrences();
    let fingerprint = stable_fingerprint(&[
        "variable-scope-v1".to_string(),
        subject.clone(),
        format!("free={:?}", free_variables),
        format!("bound={:?}", bound_variables),
    ]);
    Ok(VariableScopeReport { subject, free_variables, bound_variables, fingerprint })
}

pub fn substitution_source_from_passport(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::Prop { .. }
        | TypeKind::Statement { .. }
        | TypeKind::Goal { .. }
        | TypeKind::Hypothesis { .. }
        | TypeKind::LogicalFormula { .. }
        | TypeKind::QuantifiedFormula { .. } => formula_from_passport(passport, line),
        TypeKind::Theorem { .. }
        | TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. } => Err(substitution_error(
            line,
            format!("{} cannot be used as a substitution source", passport.ty),
            "substitution works on formula identity, not on theorem/proof/truth/provability/runtime evidence",
        )),
        _ => Err(substitution_error(
            line,
            format!("{} is not a formula-like substitution source", passport.ty),
            "use Prop, Statement, Goal, Hypothesis, LogicalFormula or QuantifiedFormula as substitution sources",
        )),
    }
}

pub fn alpha_equivalence_report(
    lhs: impl Into<String>,
    rhs: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<AlphaEquivalenceReport, Diagnostic> {
    let lhs = lhs.into();
    let rhs = rhs.into();
    validate_formula_text(&lhs, "alpha-equivalence lhs", line)?;
    validate_formula_text(&rhs, "alpha-equivalence rhs", line)?;
    let canonical_lhs = canonicalize_formula(&lhs, line)?;
    let canonical_rhs = canonicalize_formula(&rhs, line)?;
    let status = if canonical_lhs == canonical_rhs {
        AlphaEquivalenceStatus::Equivalent
    } else {
        AlphaEquivalenceStatus::NotEquivalent
    };
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "alpha-equivalence-v1".to_string(),
        lhs.clone(),
        rhs.clone(),
        status.to_string(),
        canonical_lhs.clone(),
        canonical_rhs.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(AlphaEquivalenceReport {
        lhs,
        rhs,
        status,
        canonical_lhs,
        canonical_rhs,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn substitution_report(
    source: impl Into<String>,
    variable: impl Into<String>,
    replacement: impl Into<String>,
    sources: &[&Passport],
    line: usize,
) -> Result<SubstitutionReport, Diagnostic> {
    let source = source.into();
    let variable = variable.into();
    let replacement = replacement.into();
    validate_formula_text(&source, "substitution source", line)?;
    validate_formula_text(&replacement, "substitution replacement", line)?;
    validate_identifier(&variable, "substitution variable", line)?;

    let source_scope = variable_scope_report(&source, line)?;
    let replacement_scope = variable_scope_report(&replacement, line)?;
    let parsed = parse_scoped_formula(&source, line)?;
    let free_before_set: BTreeSet<String> = source_scope.free_variables.iter().cloned().collect();
    let free_replacement: BTreeSet<String> = replacement_scope.free_variables.iter().cloned().collect();
    let bound_source: BTreeSet<String> = source_scope
        .bound_variables
        .iter()
        .map(|occ| occ.name.clone())
        .collect();

    let shadowed = parsed.binds_variable(&variable);
    let capture_risk: Vec<String> = bound_source.intersection(&free_replacement).cloned().collect();
    let (status, result, capture_risk_variables) = if shadowed {
        (SubstitutionStatus::BlockedByBinder, source.clone(), Vec::new())
    } else if free_before_set.contains(&variable) && !capture_risk.is_empty() {
        (SubstitutionStatus::RejectedCaptureRisk, source.clone(), capture_risk)
    } else {
        let result = substitute_identifier(&source, &variable, &replacement);
        (SubstitutionStatus::Applied, result, Vec::new())
    };
    let free_after = variable_scope_report(&result, line)?.free_variables;
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(sources);
    let fingerprint = stable_fingerprint(&[
        "substitution-v1".to_string(),
        source.clone(),
        variable.clone(),
        replacement.clone(),
        result.clone(),
        status.to_string(),
        format!("capture={capture_risk_variables:?}"),
        format!("trust={max_trust:?}"),
    ]);
    Ok(SubstitutionReport {
        source,
        variable,
        replacement,
        result,
        status,
        free_variables_before: source_scope.free_variables,
        free_variables_after: free_after,
        capture_risk_variables,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn alpha_rename_quantified_formula(
    formula: &QuantifiedFormula,
    new_variable: impl Into<String>,
    line: usize,
) -> Result<QuantifiedFormula, Diagnostic> {
    let new_variable = new_variable.into();
    validate_identifier(&new_variable, "new alpha-renamed variable", line)?;
    if new_variable == formula.variable.name {
        return Ok(formula.clone());
    }
    let replacement_scope = variable_scope_report(&new_variable, line)?;
    let body_scope = variable_scope_report(&formula.body, line)?;
    if body_scope.free_variables.iter().any(|name| name == &new_variable)
        || replacement_scope.free_variables.iter().any(|name| name != &new_variable)
    {
        return Err(substitution_error(
            line,
            format!("alpha-renaming `{}` to `{new_variable}` would capture or confuse a free variable", formula.variable.name),
            "choose a fresh variable that does not already occur free in the quantified body",
        ));
    }
    let new_body = substitute_identifier(&formula.body, &formula.variable.name, &new_variable);
    Ok(QuantifiedFormula {
        quantifier: formula.quantifier,
        variable: BoundVariable { name: new_variable.clone(), domain: formula.variable.domain.clone() },
        body: new_body.clone(),
        proposition: format!("{} {}:{}. {}", formula.quantifier, new_variable, formula.variable.domain, new_body),
        max_trust: formula.max_trust,
        max_provenance: formula.max_provenance,
        has_axiom_taint: formula.has_axiom_taint,
        has_oracle_taint: formula.has_oracle_taint,
        has_unsafe_taint: formula.has_unsafe_taint,
        fingerprint: stable_fingerprint(&[
            "alpha-renamed-quantified-formula-v1".to_string(),
            formula.quantifier.to_string(),
            new_variable,
            formula.variable.domain.clone(),
            new_body,
            format!("source={}", formula.fingerprint),
        ]),
    })
}

pub fn require_alpha_equivalent(report: &AlphaEquivalenceReport, line: usize) -> Result<(), Diagnostic> {
    if report.status == AlphaEquivalenceStatus::Equivalent {
        Ok(())
    } else {
        Err(substitution_error(
            line,
            format!("formulas are not alpha-equivalent: `{}` vs `{}`", report.lhs, report.rhs),
            "alpha-equivalence allows binder renaming only; domains, quantifier kinds and free-variable structure must remain identical",
        ))
    }
}

pub fn variable_scope_passport(theory: &str, report: &VariableScopeReport, sources: &[&Passport]) -> Passport {
    substitution_passport(
        theory,
        TypeKind::VariableScopeReport { subject: report.subject.clone() },
        TrustLevel::Builtin,
        Provenance::BuiltinKnown,
        merge_history(sources, format!("scope:variables:fingerprint={}", report.fingerprint)),
    )
}

pub fn alpha_equivalence_passport(theory: &str, report: &AlphaEquivalenceReport, sources: &[&Passport]) -> Passport {
    substitution_passport(
        theory,
        TypeKind::AlphaEquivalenceReport {
            lhs: report.lhs.clone(),
            rhs: report.rhs.clone(),
            status: report.status.to_string(),
        },
        report.max_trust,
        report.max_provenance,
        merge_history(sources, format!("alpha-equivalence:{}:fingerprint={}", report.status, report.fingerprint)),
    )
}

pub fn substitution_report_passport(theory: &str, report: &SubstitutionReport, sources: &[&Passport]) -> Passport {
    substitution_passport(
        theory,
        TypeKind::SubstitutionReport {
            variable: report.variable.clone(),
            status: report.status.to_string(),
        },
        report.max_trust,
        report.max_provenance,
        merge_history(sources, format!("substitution:{}:{}:fingerprint={}", report.variable, report.status, report.fingerprint)),
    )
}

pub fn export_variable_scope_report(report: &VariableScopeReport) -> String {
    let mut out = String::new();
    out.push_str("variable_scope_report: v1\n");
    out.push_str(&format!("subject: {}\n", report.subject));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out.push_str("free_variables:\n");
    for var in &report.free_variables {
        out.push_str(&format!("- {var}\n"));
    }
    out.push_str("bound_variables:\n");
    for var in &report.bound_variables {
        match &var.domain {
            Some(domain) => out.push_str(&format!("- {}:{} ({})\n", var.name, domain, var.kind)),
            None => out.push_str(&format!("- {} ({})\n", var.name, var.kind)),
        }
    }
    out
}

pub fn export_alpha_equivalence_report(report: &AlphaEquivalenceReport) -> String {
    let mut out = String::new();
    out.push_str("alpha_equivalence_report: v1\n");
    out.push_str(&format!("lhs: {}\n", report.lhs));
    out.push_str(&format!("rhs: {}\n", report.rhs));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("canonical_lhs: {}\n", report.canonical_lhs));
    out.push_str(&format!("canonical_rhs: {}\n", report.canonical_rhs));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

pub fn export_substitution_report(report: &SubstitutionReport) -> String {
    let mut out = String::new();
    out.push_str("substitution_report: v1\n");
    out.push_str(&format!("source: {}\n", report.source));
    out.push_str(&format!("variable: {}\n", report.variable));
    out.push_str(&format!("replacement: {}\n", report.replacement));
    out.push_str(&format!("result: {}\n", report.result));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("free_variables_before: {:?}\n", report.free_variables_before));
    out.push_str(&format!("free_variables_after: {:?}\n", report.free_variables_after));
    out.push_str(&format!("capture_risk_variables: {:?}\n", report.capture_risk_variables));
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

fn substitution_passport(
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
    let mut max_provenance = Provenance::BuiltinKnown;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
    }
    let max_trust = max_trust.max(TrustLevel::Builtin);
    (
        max_trust,
        max_provenance,
        max_trust >= TrustLevel::Axiom,
        max_trust >= TrustLevel::Oracle,
        max_trust >= TrustLevel::Unsafe,
    )
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum ScopedFormula {
    Atom(String),
    Quantified { quantifier: String, variable: String, domain: String, body: Box<ScopedFormula> },
}

impl ScopedFormula {
    fn free_variables(&self) -> BTreeSet<String> {
        match self {
            ScopedFormula::Atom(text) => identifier_set(text),
            ScopedFormula::Quantified { variable, domain, body, .. } => {
                let mut vars = body.free_variables();
                vars.remove(variable);
                for domain_id in identifier_set(domain) {
                    vars.remove(&domain_id);
                }
                vars
            }
        }
    }

    fn bound_occurrences(&self) -> Vec<VariableOccurrence> {
        let mut out = Vec::new();
        self.collect_bound_occurrences(&mut out);
        out
    }

    fn collect_bound_occurrences(&self, out: &mut Vec<VariableOccurrence>) {
        match self {
            ScopedFormula::Atom(_) => {}
            ScopedFormula::Quantified { variable, domain, body, .. } => {
                out.push(VariableOccurrence {
                    name: variable.clone(),
                    kind: VariableOccurrenceKind::Bound,
                    domain: Some(domain.clone()),
                });
                body.collect_bound_occurrences(out);
            }
        }
    }

    fn binds_variable(&self, target: &str) -> bool {
        match self {
            ScopedFormula::Atom(_) => false,
            ScopedFormula::Quantified { variable, body, .. } => variable == target || body.binds_variable(target),
        }
    }
}

fn parse_scoped_formula(text: &str, line: usize) -> Result<ScopedFormula, Diagnostic> {
    let trimmed = text.trim();
    if let Some((quantifier, rest)) = strip_quantifier(trimmed) {
        let Some(colon) = rest.find(':') else {
            return Err(substitution_error(
                line,
                format!("quantified formula `{trimmed}` is missing ':' after the bound variable"),
                "expected textual form like `forall x:Nat. P(x)` or `exists y:Domain. Q(y)`",
            ));
        };
        let variable = rest[..colon].trim();
        validate_identifier(variable, "bound variable", line)?;
        let after_colon = rest[colon + 1..].trim_start();
        let Some(dot) = after_colon.find('.') else {
            return Err(substitution_error(
                line,
                format!("quantified formula `{trimmed}` is missing '.' before the body"),
                "expected textual form like `forall x:Nat. P(x)` or `exists y:Domain. Q(y)`",
            ));
        };
        let domain = after_colon[..dot].trim();
        validate_formula_text(domain, "bound variable domain", line)?;
        let body = after_colon[dot + 1..].trim();
        validate_formula_text(body, "quantified body", line)?;
        return Ok(ScopedFormula::Quantified {
            quantifier: quantifier.to_string(),
            variable: variable.to_string(),
            domain: domain.to_string(),
            body: Box::new(parse_scoped_formula(body, line)?),
        });
    }
    Ok(ScopedFormula::Atom(trimmed.to_string()))
}

fn strip_quantifier(text: &str) -> Option<(&'static str, &str)> {
    if let Some(rest) = text.strip_prefix("forall ") {
        Some(("forall", rest))
    } else if let Some(rest) = text.strip_prefix("exists ") {
        Some(("exists", rest))
    } else {
        None
    }
}

fn canonicalize_formula(text: &str, line: usize) -> Result<String, Diagnostic> {
    let parsed = parse_scoped_formula(text, line)?;
    Ok(canonicalize_scoped_formula(&parsed, &mut Vec::new()))
}

fn canonicalize_scoped_formula(formula: &ScopedFormula, binders: &mut Vec<(String, String)>) -> String {
    match formula {
        ScopedFormula::Atom(text) => replace_identifiers_with_binders(text, binders),
        ScopedFormula::Quantified { quantifier, variable, domain, body } => {
            let marker = format!("${}", binders.len());
            binders.push((variable.clone(), marker.clone()));
            let canonical_body = canonicalize_scoped_formula(body, binders);
            binders.pop();
            format!("{quantifier} {marker}:{domain}. {canonical_body}")
        }
    }
}

fn replace_identifiers_with_binders(text: &str, binders: &[(String, String)]) -> String {
    rewrite_identifier_tokens(text, |ident| {
        for (name, marker) in binders.iter().rev() {
            if ident == name {
                return marker.clone();
            }
        }
        ident.to_string()
    })
}

fn substitute_identifier(text: &str, variable: &str, replacement: &str) -> String {
    rewrite_identifier_tokens(text, |ident| {
        if ident == variable { replacement.to_string() } else { ident.to_string() }
    })
}

fn rewrite_identifier_tokens(text: &str, mut f: impl FnMut(&str) -> String) -> String {
    let mut out = String::new();
    let mut token = String::new();
    for ch in text.chars() {
        if ch == '_' || ch.is_ascii_alphanumeric() {
            token.push(ch);
        } else {
            if !token.is_empty() {
                if is_identifier(&token) {
                    out.push_str(&f(&token));
                } else {
                    out.push_str(&token);
                }
                token.clear();
            }
            out.push(ch);
        }
    }
    if !token.is_empty() {
        if is_identifier(&token) {
            out.push_str(&f(&token));
        } else {
            out.push_str(&token);
        }
    }
    out
}

fn identifier_set(text: &str) -> BTreeSet<String> {
    let mut set = BTreeSet::new();
    let mut token = String::new();
    for ch in text.chars() {
        if ch == '_' || ch.is_ascii_alphanumeric() {
            token.push(ch);
        } else {
            push_identifier_if_relevant(&mut set, &mut token);
        }
    }
    push_identifier_if_relevant(&mut set, &mut token);
    set
}

fn push_identifier_if_relevant(set: &mut BTreeSet<String>, token: &mut String) {
    if token.is_empty() {
        return;
    }
    if is_identifier(token) && !is_reserved_identifier(token) {
        set.insert(token.clone());
    }
    token.clear();
}

fn is_reserved_identifier(token: &str) -> bool {
    matches!(
        token,
        "forall" | "exists" | "and" | "or" | "not" | "implies" | "iff" | "true" | "false" | "True" | "False" | "Nat" | "Bool" | "Prop" | "Set" | "Class" | "Universe"
    )
}

fn sorted_strings(set: BTreeSet<String>) -> Vec<String> {
    set.into_iter().collect()
}

fn validate_formula_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(substitution_error(
            line,
            format!("{label} is empty"),
            "substitution and alpha-equivalence need explicit formula text until full term syntax exists",
        ));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(substitution_error(
            line,
            format!("{label} contains a newline"),
            "single-line formula identity keeps substitution/audit fingerprints deterministic in this MVP layer",
        ));
    }
    Ok(())
}

fn validate_identifier(name: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if is_identifier(name) && !is_reserved_identifier(name) {
        Ok(())
    } else {
        Err(substitution_error(
            line,
            format!("invalid {label} `{name}`"),
            "variables must be explicit non-reserved identifiers so alpha-renaming and substitution remain auditable",
        ))
    }
}

fn is_identifier(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(first) if first == '_' || first.is_ascii_alphabetic() => {}
        _ => return false,
    }
    chars.all(|ch| ch == '_' || ch.is_ascii_alphanumeric())
}

fn substitution_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::SubstitutionError, Some(line), message).with_help(help)
}

fn stable_fingerprint(parts: &[String]) -> String {
    let mut hash: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
        hash ^= 0xff;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    format!("dlm-substitution-v1-{hash:016x}")
}
