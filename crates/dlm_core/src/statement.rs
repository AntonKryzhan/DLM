use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::policy::join_trust;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeclarationKind {
    Statement,
    Theorem,
    Goal,
    Hypothesis,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StatementDecl {
    pub theory: String,
    pub proposition: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TheoremDecl {
    pub theory: String,
    pub name: String,
    pub proposition: String,
    pub axiom_tainted: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GoalDecl {
    pub theory: String,
    pub proposition: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HypothesisDecl {
    pub theory: String,
    pub proposition: String,
}

pub fn statement_decl(theory: impl Into<String>, proposition: impl Into<String>) -> StatementDecl {
    StatementDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn theorem_decl(
    theory: impl Into<String>,
    name: impl Into<String>,
    proposition: impl Into<String>,
    axiom_tainted: bool,
) -> TheoremDecl {
    TheoremDecl {
        theory: theory.into(),
        name: name.into(),
        proposition: proposition.into(),
        axiom_tainted,
    }
}

pub fn goal_decl(theory: impl Into<String>, proposition: impl Into<String>) -> GoalDecl {
    GoalDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn hypothesis_decl(theory: impl Into<String>, proposition: impl Into<String>) -> HypothesisDecl {
    HypothesisDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn proposition_of(passport: &Passport) -> Option<&str> {
    match &passport.ty {
        TypeKind::Prop { name }
        | TypeKind::Statement { proposition: name }
        | TypeKind::Goal { proposition: name }
        | TypeKind::Hypothesis { proposition: name }
        | TypeKind::Theorem {
            proposition: name,
            ..
        } => Some(name.as_str()),
        TypeKind::Provable { proposition, .. }
        | TypeKind::TruthClaim { proposition }
        | TypeKind::RuntimeWitness(proposition)
        | TypeKind::StaticProof(proposition) => Some(proposition.as_str()),
        _ => None,
    }
}

pub fn statement_passport(theory: &str, proposition: impl Into<String>) -> Passport {
    let proposition = proposition.into();
    Passport {
        ty: TypeKind::Statement {
            proposition: proposition.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("statement:declare:{proposition}")),
        location: LocationContext::local(),
    }
}

pub fn goal_passport(theory: &str, proposition: impl Into<String>) -> Passport {
    let proposition = proposition.into();
    Passport {
        ty: TypeKind::Goal {
            proposition: proposition.clone(),
        },
        construction: ConstructionMode::Definable,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::Parsed,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("goal:open:{proposition}")),
        location: LocationContext::local(),
    }
}

pub fn hypothesis_passport(theory: &str, proposition: impl Into<String>, source: &Passport) -> Passport {
    let proposition = proposition.into();
    Passport {
        ty: TypeKind::Hypothesis {
            proposition: proposition.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: source.trust.max(TrustLevel::Axiom),
        provenance: source.provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::Assumed,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_source(&source.history, format!("hypothesis:assume:{proposition}")),
        location: LocationContext::local(),
    }
}

pub fn theorem_from_static_proof(
    theory: &str,
    name: impl Into<String>,
    statement: &Passport,
    proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let name = name.into();
    let proposition = require_statement_like(statement, line)?.to_string();
    require_static_proof(proof, line)?;

    Ok(Passport {
        ty: TypeKind::Theorem {
            name: name.clone(),
            proposition: proposition.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
            Capability::CanProofKernelCheck,
        ]),
        cost: CostClass::ProofRequired,
        trust: join_trust(statement.trust, proof.trust),
        provenance: statement.provenance.max(proof.provenance),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(
            &statement.history,
            &proof.history,
            format!("theorem:proved:{name}:{proposition}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn axiom_theorem(
    theory: &str,
    name: impl Into<String>,
    statement: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let name = name.into();
    let proposition = require_statement_like(statement, line)?.to_string();

    Ok(Passport {
        ty: TypeKind::Theorem {
            name: name.clone(),
            proposition: proposition.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanPropositionReason,
        ]),
        cost: CostClass::ProofRequired,
        trust: statement.trust.max(TrustLevel::Axiom),
        provenance: statement.provenance.max(Provenance::BuiltinKnown),
        validation: ValidationState::Assumed,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_source(
            &statement.history,
            format!("theorem:axiom:{name}:{proposition}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn require_statement_like(passport: &Passport, line: usize) -> Result<&str, Diagnostic> {
    match &passport.ty {
        TypeKind::Statement { proposition } | TypeKind::Prop { name: proposition } => {
            Ok(proposition.as_str())
        }
        TypeKind::Goal { .. } | TypeKind::Hypothesis { .. } | TypeKind::Theorem { .. } => {
            Err(Diagnostic::error(
                DiagnosticKind::StatementTheoremError,
                Some(line),
                "theorem construction requires a Statement/Prop, not an already-open goal, hypothesis, or theorem",
            )
            .with_help(format!("value passport: {passport}")))
        }
        _ => Err(Diagnostic::error(
            DiagnosticKind::StatementTheoremError,
            Some(line),
            "theorem construction requires a Statement or Prop object",
        )
        .with_help(format!("value passport: {passport}"))),
    }
}

pub fn require_static_proof(passport: &Passport, line: usize) -> Result<&str, Diagnostic> {
    match &passport.ty {
        TypeKind::StaticProof(predicate) => Ok(predicate.as_str()),
        TypeKind::ProofTerm { .. } => Err(Diagnostic::error(
            DiagnosticKind::StatementTheoremError,
            Some(line),
            "Theorem requires StaticProof; ProofTerm must be kernel-checked first",
        )
        .with_help(format!("value passport: {passport}"))),
        TypeKind::RuntimeWitness(_) => Err(Diagnostic::error(
            DiagnosticKind::StatementTheoremError,
            Some(line),
            "Theorem requires StaticProof; RuntimeWitness is not a static proof",
        )
        .with_help(format!("value passport: {passport}"))),
        _ => Err(Diagnostic::error(
            DiagnosticKind::StatementTheoremError,
            Some(line),
            "Theorem requires StaticProof evidence",
        )
        .with_help(format!("value passport: {passport}"))),
    }
}
