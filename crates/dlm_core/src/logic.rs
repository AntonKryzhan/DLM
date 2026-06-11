use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum LogicalConnective {
    And,
    Or,
    Implies,
    Iff,
    Not,
}

impl LogicalConnective {
    pub fn arity(self) -> usize {
        match self {
            LogicalConnective::Not => 1,
            LogicalConnective::And
            | LogicalConnective::Or
            | LogicalConnective::Implies
            | LogicalConnective::Iff => 2,
        }
    }
}

impl fmt::Display for LogicalConnective {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LogicalConnective::And => write!(f, "and"),
            LogicalConnective::Or => write!(f, "or"),
            LogicalConnective::Implies => write!(f, "implies"),
            LogicalConnective::Iff => write!(f, "iff"),
            LogicalConnective::Not => write!(f, "not"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum QuantifierKind {
    Forall,
    Exists,
}

impl fmt::Display for QuantifierKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            QuantifierKind::Forall => write!(f, "forall"),
            QuantifierKind::Exists => write!(f, "exists"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundVariable {
    pub name: String,
    pub domain: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogicalFormula {
    pub connective: LogicalConnective,
    pub operands: Vec<String>,
    pub proposition: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct QuantifiedFormula {
    pub quantifier: QuantifierKind,
    pub variable: BoundVariable,
    pub body: String,
    pub proposition: String,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn bound_variable(
    name: impl Into<String>,
    domain: impl Into<String>,
    line: usize,
) -> Result<BoundVariable, Diagnostic> {
    let name = name.into();
    let domain = domain.into();
    if !is_identifier(&name) {
        return Err(logic_error(
            line,
            format!("invalid bound variable name `{name}`"),
            "bound variables must be explicit identifiers; substitution and alpha-renaming will rely on this invariant",
        ));
    }
    if domain.trim().is_empty() {
        return Err(logic_error(
            line,
            format!("bound variable `{name}` has an empty domain"),
            "quantifier objects require an explicit domain even before full dependent typing is implemented",
        ));
    }
    Ok(BoundVariable { name, domain })
}

pub fn formula_atom(atom: impl Into<String>, line: usize) -> Result<String, Diagnostic> {
    let atom = atom.into();
    validate_formula_text(&atom, "atomic formula", line)?;
    Ok(atom)
}

pub fn formula_from_passport(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::Prop { name } => Ok(name.clone()),
        TypeKind::Statement { proposition }
        | TypeKind::Goal { proposition }
        | TypeKind::Hypothesis { proposition } => Ok(proposition.clone()),
        TypeKind::LogicalFormula { form } => Ok(form.clone()),
        TypeKind::QuantifiedFormula { quantifier, variable, domain, body } => {
            Ok(format!("{} {}:{}. {}", quantifier, variable, domain, body))
        }
        TypeKind::Theorem { .. } => Err(logic_error(
            line,
            "Theorem cannot be used as a formula operand without explicitly extracting its proposition",
            "theorem status is evidence; logical formula construction works on propositions, statements, goals or hypotheses",
        )),
        TypeKind::StaticProof(_)
        | TypeKind::ProofTerm { .. }
        | TypeKind::RuntimeWitness(_)
        | TypeKind::Provable { .. }
        | TypeKind::TruthClaim { .. }
        | TypeKind::ReflectionClaim { .. }
        | TypeKind::SelfReferenceClaim { .. }
        | TypeKind::ConsistencyClaim { .. } => Err(logic_error(
            line,
            format!("{} is not a formula operand", passport.ty),
            "ordinary logical formulas must not silently consume proof, truth, provability, runtime witness, consistency or reflection objects",
        )),
        _ => Err(logic_error(
            line,
            format!("{} is not a proposition-like passport", passport.ty),
            "use Prop, Statement, Goal, Hypothesis, LogicalFormula or QuantifiedFormula as formula operands",
        )),
    }
}

pub fn logical_formula(
    connective: LogicalConnective,
    operands: Vec<String>,
    source_passports: &[&Passport],
    line: usize,
) -> Result<LogicalFormula, Diagnostic> {
    if operands.len() != connective.arity() {
        return Err(logic_error(
            line,
            format!(
                "logical connective `{connective}` expects {} operand(s), got {}",
                connective.arity(),
                operands.len()
            ),
            "logical connective arity is part of the formula fingerprint and cannot be repaired implicitly",
        ));
    }
    for operand in &operands {
        validate_formula_text(operand, "logical operand", line)?;
    }
    let proposition = match connective {
        LogicalConnective::Not => format!("not({})", operands[0]),
        LogicalConnective::And => format!("and({}, {})", operands[0], operands[1]),
        LogicalConnective::Or => format!("or({}, {})", operands[0], operands[1]),
        LogicalConnective::Implies => format!("implies({}, {})", operands[0], operands[1]),
        LogicalConnective::Iff => format!("iff({}, {})", operands[0], operands[1]),
    };
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(source_passports);
    let mut parts = vec![
        "logic-formula-v1".to_string(),
        connective.to_string(),
        proposition.clone(),
        format!("trust={max_trust:?}"),
    ];
    parts.extend(operands.iter().cloned());
    let fingerprint = stable_fingerprint(&parts);
    Ok(LogicalFormula {
        connective,
        operands,
        proposition,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn quantified_formula(
    quantifier: QuantifierKind,
    variable: BoundVariable,
    body: impl Into<String>,
    source_passports: &[&Passport],
    line: usize,
) -> Result<QuantifiedFormula, Diagnostic> {
    let body = body.into();
    validate_formula_text(&body, "quantifier body", line)?;
    let proposition = format!("{} {}:{}. {}", quantifier, variable.name, variable.domain, body);
    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) = taint_summary(source_passports);
    let fingerprint = stable_fingerprint(&[
        "quantified-formula-v1".to_string(),
        quantifier.to_string(),
        variable.name.clone(),
        variable.domain.clone(),
        body.clone(),
        format!("trust={max_trust:?}"),
    ]);
    Ok(QuantifiedFormula {
        quantifier,
        variable,
        body,
        proposition,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_logical_formula(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::LogicalFormula { form } => Ok(form.clone()),
        _ => Err(logic_error(
            line,
            format!("expected LogicalFormula, got {}", passport.ty),
            "logical formula reports are ordinary proposition objects, not proof or theorem evidence",
        )),
    }
}

pub fn require_quantified_formula(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::QuantifiedFormula { quantifier, variable, domain, body } => {
            Ok(format!("{} {}:{}. {}", quantifier, variable, domain, body))
        }
        _ => Err(logic_error(
            line,
            format!("expected QuantifiedFormula, got {}", passport.ty),
            "quantified formula reports are formula objects; introduction/elimination proofs will be separate proof-kernel rules",
        )),
    }
}

pub fn logical_formula_passport(theory: &str, formula: &LogicalFormula, sources: &[&Passport]) -> Passport {
    let history = merge_formula_history(
        sources,
        format!("logic:formula:{}:fingerprint={}", formula.connective, formula.fingerprint),
    );
    formula_passport(
        theory,
        TypeKind::LogicalFormula { form: formula.proposition.clone() },
        formula.max_trust,
        formula.max_provenance,
        history,
    )
}

pub fn quantified_formula_passport(
    theory: &str,
    formula: &QuantifiedFormula,
    sources: &[&Passport],
) -> Passport {
    let history = merge_formula_history(
        sources,
        format!(
            "logic:quantifier:{}:{}:{}:fingerprint={}",
            formula.quantifier, formula.variable.name, formula.variable.domain, formula.fingerprint
        ),
    );
    formula_passport(
        theory,
        TypeKind::QuantifiedFormula {
            quantifier: formula.quantifier.to_string(),
            variable: formula.variable.name.clone(),
            domain: formula.variable.domain.clone(),
            body: formula.body.clone(),
        },
        formula.max_trust,
        formula.max_provenance,
        history,
    )
}

pub fn export_logical_formula(formula: &LogicalFormula) -> String {
    let mut out = String::new();
    out.push_str("logical_formula_report: v1\n");
    out.push_str(&format!("connective: {}\n", formula.connective));
    out.push_str(&format!("proposition: {}\n", formula.proposition));
    out.push_str(&format!("max_trust: {:?}\n", formula.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", formula.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", formula.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", formula.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", formula.fingerprint));
    out.push_str("operands:\n");
    for operand in &formula.operands {
        out.push_str(&format!("- {operand}\n"));
    }
    out
}

pub fn export_quantified_formula(formula: &QuantifiedFormula) -> String {
    let mut out = String::new();
    out.push_str("quantified_formula_report: v1\n");
    out.push_str(&format!("quantifier: {}\n", formula.quantifier));
    out.push_str(&format!("variable: {}\n", formula.variable.name));
    out.push_str(&format!("domain: {}\n", formula.variable.domain));
    out.push_str(&format!("body: {}\n", formula.body));
    out.push_str(&format!("proposition: {}\n", formula.proposition));
    out.push_str(&format!("max_trust: {:?}\n", formula.max_trust));
    out.push_str(&format!("has_axiom_taint: {}\n", formula.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", formula.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", formula.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", formula.fingerprint));
    out
}

fn formula_passport(theory: &str, ty: TypeKind, max_trust: TrustLevel, max_provenance: Provenance, history: HistoryChain) -> Passport {
    Passport {
        ty,
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanCompareByProof,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: max_trust.max(TrustLevel::Builtin),
        provenance: max_provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

fn merge_formula_history(sources: &[&Passport], event: impl Into<String>) -> HistoryChain {
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

fn validate_formula_text(text: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if text.trim().is_empty() {
        return Err(logic_error(
            line,
            format!("{label} is empty"),
            "formula objects must carry explicit textual proposition identity until full term binding is implemented",
        ));
    }
    if text.contains('\n') || text.contains('\r') {
        return Err(logic_error(
            line,
            format!("{label} contains a newline"),
            "single-line proposition identity keeps audit exports and fingerprints stable in this MVP layer",
        ));
    }
    Ok(())
}

fn is_identifier(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(first) if first == '_' || first.is_ascii_alphabetic() => {}
        _ => return false,
    }
    chars.all(|ch| ch == '_' || ch.is_ascii_alphanumeric())
}


fn logic_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::LogicFormulaError, Some(line), message).with_help(help)
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
    format!("dlm-logic-v1-{hash:016x}")
}
