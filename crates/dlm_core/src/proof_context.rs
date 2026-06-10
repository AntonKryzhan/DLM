use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{Passport, TypeKind};
use crate::statement::{axiom_theorem, hypothesis_passport, require_statement_like, require_static_proof, theorem_from_static_proof};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct HypothesisId(pub u32);

impl fmt::Display for HypothesisId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "h{}", self.0)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Hypothesis {
    pub id: HypothesisId,
    pub proposition: String,
    pub passport: Passport,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HypothesisSet {
    pub theory: String,
    hypotheses: Vec<Hypothesis>,
    next_id: u32,
}

impl HypothesisSet {
    pub fn new(theory: impl Into<String>) -> Self {
        Self {
            theory: theory.into(),
            hypotheses: Vec::new(),
            next_id: 0,
        }
    }

    pub fn push(&mut self, proposition: impl Into<String>, passport: Passport) -> HypothesisId {
        let proposition = proposition.into();
        let id = HypothesisId(self.next_id);
        self.next_id += 1;
        self.hypotheses.push(Hypothesis {
            id,
            proposition,
            passport,
        });
        id
    }

    pub fn len(&self) -> usize {
        self.hypotheses.len()
    }

    pub fn is_empty(&self) -> bool {
        self.hypotheses.is_empty()
    }

    pub fn iter(&self) -> impl Iterator<Item = &Hypothesis> {
        self.hypotheses.iter()
    }

    pub fn get(&self, id: HypothesisId) -> Option<&Hypothesis> {
        self.hypotheses.iter().find(|hypothesis| hypothesis.id == id)
    }

    pub fn contains_proposition(&self, proposition: &str) -> bool {
        self.hypotheses
            .iter()
            .any(|hypothesis| hypothesis.proposition == proposition)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TacticStep {
    OpenGoal { proposition: String },
    Assume { hypothesis: HypothesisId, proposition: String },
    ExactStaticProof { proposition: String },
    AdmitAxiom { reason: String },
    CloseTheorem { name: String, proposition: String },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProofContext {
    pub theory: String,
    pub goal: Passport,
    pub hypotheses: HypothesisSet,
    pub steps: Vec<TacticStep>,
}

impl ProofContext {
    pub fn goal_proposition(&self) -> Option<&str> {
        goal_proposition(&self.goal)
    }

    pub fn has_hypothesis(&self, proposition: &str) -> bool {
        self.hypotheses.contains_proposition(proposition)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProofObligation {
    pub proposition: String,
    pub reason: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProofClosureStatus {
    ClosedByStaticProof,
    AdmittedByAxiom,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProofClosure {
    pub theorem: Passport,
    pub obligations: Vec<ProofObligation>,
    pub status: ProofClosureStatus,
    pub steps: Vec<TacticStep>,
}

pub fn open_proof_context(
    theory: impl Into<String>,
    goal: Passport,
    line: usize,
) -> Result<ProofContext, Diagnostic> {
    let theory = theory.into();
    let proposition = goal_proposition(&goal)
        .ok_or_else(|| {
            Diagnostic::error(
                DiagnosticKind::ProofObligationError,
                Some(line),
                "proof context must be opened from a Goal passport",
            )
            .with_help(format!("value passport: {goal}"))
        })?
        .to_string();

    Ok(ProofContext {
        theory: theory.clone(),
        goal,
        hypotheses: HypothesisSet::new(theory),
        steps: vec![TacticStep::OpenGoal { proposition }],
    })
}

pub fn assume_hypothesis(
    context: &mut ProofContext,
    proposition: impl Into<String>,
    source: &Passport,
    line: usize,
) -> Result<HypothesisId, Diagnostic> {
    let proposition = proposition.into();
    if proposition.trim().is_empty() {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofObligationError,
            Some(line),
            "hypothesis proposition cannot be empty",
        ));
    }

    let passport = hypothesis_passport(&context.theory, proposition.clone(), source);
    let id = context.hypotheses.push(proposition.clone(), passport);
    context.steps.push(TacticStep::Assume {
        hypothesis: id,
        proposition,
    });
    Ok(id)
}

pub fn proof_obligation_for_goal(context: &ProofContext) -> ProofObligation {
    ProofObligation {
        proposition: context
            .goal_proposition()
            .unwrap_or("<invalid-goal>")
            .to_string(),
        reason: "open goal requires StaticProof or explicit axiom admission".to_string(),
    }
}

pub fn close_proof_with_static_proof(
    mut context: ProofContext,
    theorem_name: impl Into<String>,
    statement: &Passport,
    proof: &Passport,
    line: usize,
) -> Result<ProofClosure, Diagnostic> {
    let theorem_name = theorem_name.into();
    let goal = require_goal_matches_statement(&context, statement, line)?;
    let proof_prop = require_static_proof(proof, line)?.to_string();

    if proof_prop != goal {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofObligationError,
            Some(line),
            format!(
                "static proof proves `{proof_prop}`, but open goal requires `{goal}`"
            ),
        )
        .with_help("close the goal with a StaticProof of the exact goal proposition"));
    }

    let theorem = theorem_from_static_proof(&context.theory, theorem_name.clone(), statement, proof, line)?;
    context.steps.push(TacticStep::ExactStaticProof {
        proposition: proof_prop,
    });
    context.steps.push(TacticStep::CloseTheorem {
        name: theorem_name,
        proposition: goal,
    });

    Ok(ProofClosure {
        theorem,
        obligations: Vec::new(),
        status: ProofClosureStatus::ClosedByStaticProof,
        steps: context.steps,
    })
}

pub fn close_proof_by_axiom(
    mut context: ProofContext,
    theorem_name: impl Into<String>,
    statement: &Passport,
    reason: impl Into<String>,
    line: usize,
) -> Result<ProofClosure, Diagnostic> {
    let theorem_name = theorem_name.into();
    let reason = reason.into();
    let goal = require_goal_matches_statement(&context, statement, line)?;
    let theorem = axiom_theorem(&context.theory, theorem_name.clone(), statement, line)?;

    context.steps.push(TacticStep::AdmitAxiom { reason });
    context.steps.push(TacticStep::CloseTheorem {
        name: theorem_name,
        proposition: goal,
    });

    Ok(ProofClosure {
        theorem,
        obligations: Vec::new(),
        status: ProofClosureStatus::AdmittedByAxiom,
        steps: context.steps,
    })
}

fn goal_proposition(passport: &Passport) -> Option<&str> {
    match &passport.ty {
        TypeKind::Goal { proposition } => Some(proposition.as_str()),
        _ => None,
    }
}

fn require_goal_matches_statement(
    context: &ProofContext,
    statement: &Passport,
    line: usize,
) -> Result<String, Diagnostic> {
    let goal = context.goal_proposition().ok_or_else(|| {
        Diagnostic::error(
            DiagnosticKind::ProofObligationError,
            Some(line),
            "proof context has no open Goal proposition",
        )
    })?;
    let statement_prop = require_statement_like(statement, line)?;

    if statement_prop != goal {
        return Err(Diagnostic::error(
            DiagnosticKind::ProofObligationError,
            Some(line),
            format!(
                "statement proposition `{statement_prop}` does not match open goal `{goal}`"
            ),
        )
        .with_help("close_proof requires Statement<P>, Goal<P>, and StaticProof<P> to agree exactly"));
    }

    Ok(goal.to_string())
}
