use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RewriteDirection {
    Forward,
    Reverse,
}

impl fmt::Display for RewriteDirection {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RewriteDirection::Forward => write!(f, "forward"),
            RewriteDirection::Reverse => write!(f, "reverse"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EqProofDecl {
    pub theory: String,
    pub lhs: String,
    pub rhs: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RewriteRuleDecl {
    pub theory: String,
    pub name: String,
    pub lhs: String,
    pub rhs: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RewriteStep {
    pub rule_name: String,
    pub direction: RewriteDirection,
    pub from: String,
    pub to: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RewriteTrace {
    pub theory: String,
    pub from: String,
    pub to: String,
    pub steps: Vec<RewriteStep>,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub rule_histories: Vec<HistoryChain>,
}

impl RewriteTrace {
    pub fn is_axiom_tainted(&self) -> bool {
        self.trust >= TrustLevel::Axiom
    }

    pub fn len(&self) -> usize {
        self.steps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.steps.is_empty()
    }

    pub fn step_labels(&self) -> Vec<String> {
        self.steps.iter().map(rewrite_step_label).collect()
    }
}

pub fn equality_proposition(lhs: &str, rhs: &str) -> String {
    format!("Eq({lhs},{rhs})")
}

pub fn eq_proof_decl(
    theory: impl Into<String>,
    lhs: impl Into<String>,
    rhs: impl Into<String>,
) -> EqProofDecl {
    EqProofDecl {
        theory: theory.into(),
        lhs: lhs.into(),
        rhs: rhs.into(),
    }
}

pub fn rewrite_rule_decl(
    theory: impl Into<String>,
    name: impl Into<String>,
    lhs: impl Into<String>,
    rhs: impl Into<String>,
) -> RewriteRuleDecl {
    RewriteRuleDecl {
        theory: theory.into(),
        name: name.into(),
        lhs: lhs.into(),
        rhs: rhs.into(),
    }
}

pub fn reflexive_eq_proof(theory: &str, term: impl Into<String>, line: usize) -> Result<Passport, Diagnostic> {
    let term = require_non_empty_term(term.into(), "term", line)?;
    Ok(Passport {
        ty: TypeKind::EqProof {
            lhs: term.clone(),
            rhs: term.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: equality_capabilities(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("eq:refl:{term}")),
        location: LocationContext::local(),
    })
}

pub fn eq_proof_from_static_proof(
    theory: &str,
    lhs: impl Into<String>,
    rhs: impl Into<String>,
    proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let lhs = require_non_empty_term(lhs.into(), "lhs", line)?;
    let rhs = require_non_empty_term(rhs.into(), "rhs", line)?;
    let expected = equality_proposition(&lhs, &rhs);

    match &proof.ty {
        TypeKind::StaticProof(predicate) if predicate == &expected => Ok(Passport {
            ty: TypeKind::EqProof {
                lhs: lhs.clone(),
                rhs: rhs.clone(),
            },
            construction: ConstructionMode::ProofFinite,
            capabilities: equality_capabilities(),
            cost: CostClass::ProofRequired,
            trust: proof.trust,
            provenance: proof.provenance,
            validation: ValidationState::StaticChecked,
            theory: TheoryContext::new(theory),
            history: HistoryChain::from_source(
                &proof.history,
                format!("eq:from_static_proof:{lhs}:{rhs}"),
            ),
            location: LocationContext::local(),
        }),
        TypeKind::StaticProof(predicate) => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            format!("static proof proves `{predicate}`, not equality `{expected}`"),
        )
        .with_help("EqProof construction requires a StaticProof of exactly Eq(lhs,rhs)")),
        TypeKind::RuntimeWitness(_) => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "EqProof requires StaticProof; RuntimeWitness cannot justify rewrite equality",
        )),
        TypeKind::ProofTerm { .. } => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "EqProof requires StaticProof; ProofTerm must be kernel-checked first",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "EqProof construction requires StaticProof evidence",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

pub fn axiom_eq_proof(
    theory: &str,
    lhs: impl Into<String>,
    rhs: impl Into<String>,
    reason: impl Into<String>,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let lhs = require_non_empty_term(lhs.into(), "lhs", line)?;
    let rhs = require_non_empty_term(rhs.into(), "rhs", line)?;
    let reason = reason.into();
    Ok(Passport {
        ty: TypeKind::EqProof {
            lhs: lhs.clone(),
            rhs: rhs.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: equality_capabilities(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Axiom,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::Assumed,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("eq:axiom:{lhs}:{rhs}:{reason}")),
        location: LocationContext::local(),
    })
}

pub fn rewrite_rule_from_eq_proof(
    theory: &str,
    name: impl Into<String>,
    eq_proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let name = require_non_empty_term(name.into(), "rewrite rule name", line)?;
    let (lhs, rhs) = require_eq_proof(eq_proof, line)?;
    Ok(Passport {
        ty: TypeKind::RewriteRule {
            name: name.clone(),
            lhs: lhs.clone(),
            rhs: rhs.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: rewrite_capabilities(),
        cost: CostClass::ProofRequired,
        trust: eq_proof.trust,
        provenance: eq_proof.provenance,
        validation: eq_proof.validation,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_source(&eq_proof.history, format!("rewrite:rule:{name}:{lhs}->{rhs}")),
        location: LocationContext::local(),
    })
}

pub fn apply_rewrite_rule(
    rule: &Passport,
    current: impl Into<String>,
    direction: RewriteDirection,
    line: usize,
) -> Result<RewriteStep, Diagnostic> {
    let current = require_non_empty_term(current.into(), "current term", line)?;
    let (name, lhs, rhs) = require_rewrite_rule(rule, line)?;
    let (from, to) = match direction {
        RewriteDirection::Forward if current == lhs => (lhs.clone(), rhs.clone()),
        RewriteDirection::Reverse if current == rhs => (rhs.clone(), lhs.clone()),
        RewriteDirection::Forward => {
            return Err(Diagnostic::error(
                DiagnosticKind::EqualityRewriteError,
                Some(line),
                format!("rewrite rule `{name}` cannot apply forward to `{current}`"),
            )
            .with_help(format!("forward rule expects `{lhs}` and rewrites to `{rhs}`")))
        }
        RewriteDirection::Reverse => {
            return Err(Diagnostic::error(
                DiagnosticKind::EqualityRewriteError,
                Some(line),
                format!("rewrite rule `{name}` cannot apply reverse to `{current}`"),
            )
            .with_help(format!("reverse rule expects `{rhs}` and rewrites to `{lhs}`")))
        }
    };

    Ok(RewriteStep {
        rule_name: name,
        direction,
        from,
        to,
    })
}

pub fn rewrite_trace(
    theory: &str,
    start: impl Into<String>,
    applications: &[(Passport, RewriteDirection)],
    line: usize,
) -> Result<RewriteTrace, Diagnostic> {
    let start = require_non_empty_term(start.into(), "start term", line)?;
    let mut current = start.clone();
    let mut steps = Vec::new();
    let mut trust = TrustLevel::Builtin;
    let mut provenance = Provenance::BuiltinKnown;
    let mut rule_histories = Vec::new();

    for (rule, direction) in applications {
        let step = apply_rewrite_rule(rule, current.clone(), *direction, line)?;
        current = step.to.clone();
        trust = trust.max(rule.trust);
        provenance = provenance.max(rule.provenance);
        rule_histories.push(rule.history.clone());
        steps.push(step);
    }

    Ok(RewriteTrace {
        theory: theory.to_string(),
        from: start,
        to: current,
        steps,
        trust,
        provenance,
        rule_histories,
    })
}

pub fn rewrite_certificate_passport(theory: &str, trace: &RewriteTrace) -> Passport {
    let event = format!("rewrite:certificate:{}->{}:steps={}", trace.from, trace.to, trace.steps.len());
    let history = if trace.rule_histories.is_empty() {
        HistoryChain::from_event(event)
    } else {
        HistoryChain::merge_many(trace.rule_histories.iter(), event)
    };

    Passport {
        ty: TypeKind::RewriteCertificate {
            from: trace.from.clone(),
            to: trace.to.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: rewrite_capabilities(),
        cost: CostClass::ProofRequired,
        trust: trace.trust,
        provenance: trace.provenance,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history,
        location: LocationContext::local(),
    }
}

pub fn require_eq_proof(passport: &Passport, line: usize) -> Result<(String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::EqProof { lhs, rhs } => Ok((lhs.clone(), rhs.clone())),
        TypeKind::Bool => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "Bool equality result is not an EqProof and cannot justify rewriting",
        )),
        TypeKind::RuntimeWitness(_) => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "RuntimeWitness cannot justify static rewriting",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "rewrite rule construction requires EqProof evidence",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

pub fn require_rewrite_rule(
    passport: &Passport,
    line: usize,
) -> Result<(String, String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::RewriteRule { name, lhs, rhs } => Ok((name.clone(), lhs.clone(), rhs.clone())),
        TypeKind::EqProof { .. } => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "EqProof must first be registered as a RewriteRule before applying it",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            "rewrite application requires a RewriteRule passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

fn equality_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanCompareByProof,
        Capability::CanPropositionReason,
    ])
}

fn rewrite_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanCompareByProof,
        Capability::CanCompareSyntax,
        Capability::CanPropositionReason,
    ])
}

fn require_non_empty_term(term: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if term.trim().is_empty() {
        Err(Diagnostic::error(
            DiagnosticKind::EqualityRewriteError,
            Some(line),
            format!("{label} cannot be empty in equality/rewrite context"),
        ))
    } else {
        Ok(term)
    }
}

fn rewrite_step_label(step: &RewriteStep) -> String {
    format!(
        "rewrite:{}:{}:{}->{}",
        step.rule_name, step.direction, step.from, step.to
    )
}
