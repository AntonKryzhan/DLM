use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::Passport;
use crate::proof_context::{
    assume_hypothesis, close_proof_by_axiom, close_proof_with_static_proof,
    proof_obligation_for_goal, ProofClosure, ProofClosureStatus, ProofContext, ProofObligation,
    TacticStep,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TacticStepIndex(pub usize);

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TacticCommand {
    Assume {
        proposition: String,
        source: Passport,
    },
    ExactStaticProof {
        theorem_name: String,
        statement: Passport,
        proof: Passport,
    },
    AdmitAxiom {
        theorem_name: String,
        statement: Passport,
        reason: String,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TacticScriptStep {
    pub index: TacticStepIndex,
    pub command: TacticCommand,
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct TacticScript {
    steps: Vec<TacticScriptStep>,
}

impl TacticScript {
    pub fn new() -> Self {
        Self { steps: Vec::new() }
    }

    pub fn push(&mut self, command: TacticCommand) -> TacticStepIndex {
        let index = TacticStepIndex(self.steps.len());
        self.steps.push(TacticScriptStep { index, command });
        index
    }

    pub fn with(mut self, command: TacticCommand) -> Self {
        self.push(command);
        self
    }

    pub fn assume(mut self, proposition: impl Into<String>, source: Passport) -> Self {
        self.push(TacticCommand::Assume {
            proposition: proposition.into(),
            source,
        });
        self
    }

    pub fn exact_static_proof(
        mut self,
        theorem_name: impl Into<String>,
        statement: Passport,
        proof: Passport,
    ) -> Self {
        self.push(TacticCommand::ExactStaticProof {
            theorem_name: theorem_name.into(),
            statement,
            proof,
        });
        self
    }

    pub fn admit_axiom(
        mut self,
        theorem_name: impl Into<String>,
        statement: Passport,
        reason: impl Into<String>,
    ) -> Self {
        self.push(TacticCommand::AdmitAxiom {
            theorem_name: theorem_name.into(),
            statement,
            reason: reason.into(),
        });
        self
    }

    pub fn len(&self) -> usize {
        self.steps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.steps.is_empty()
    }

    pub fn iter(&self) -> impl Iterator<Item = &TacticScriptStep> {
        self.steps.iter()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TacticScriptStatus {
    Open,
    ClosedByStaticProof,
    AdmittedByAxiom,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TacticScriptReport {
    pub status: TacticScriptStatus,
    pub executed_steps: usize,
    pub final_context: Option<ProofContext>,
    pub closure: Option<ProofClosure>,
    pub obligations: Vec<ProofObligation>,
    pub trace: Vec<TacticStep>,
}

impl TacticScriptReport {
    pub fn is_closed(&self) -> bool {
        self.closure.is_some()
    }
}

pub fn execute_tactic_script(
    mut context: ProofContext,
    script: &TacticScript,
    line: usize,
) -> Result<TacticScriptReport, Diagnostic> {
    if script.is_empty() {
        return Ok(open_report(context, 0));
    }

    let script_len = script.len();
    for step in script.iter() {
        match &step.command {
            TacticCommand::Assume { proposition, source } => {
                assume_hypothesis(&mut context, proposition.clone(), source, line)?;
            }
            TacticCommand::ExactStaticProof {
                theorem_name,
                statement,
                proof,
            } => {
                require_closing_step_is_final(step.index, script_len, line)?;
                let closure = close_proof_with_static_proof(
                    context,
                    theorem_name.clone(),
                    statement,
                    proof,
                    line,
                )?;
                return Ok(closed_report(
                    step.index.0 + 1,
                    closure,
                    TacticScriptStatus::ClosedByStaticProof,
                ));
            }
            TacticCommand::AdmitAxiom {
                theorem_name,
                statement,
                reason,
            } => {
                require_closing_step_is_final(step.index, script_len, line)?;
                let closure = close_proof_by_axiom(
                    context,
                    theorem_name.clone(),
                    statement,
                    reason.clone(),
                    line,
                )?;
                return Ok(closed_report(
                    step.index.0 + 1,
                    closure,
                    TacticScriptStatus::AdmittedByAxiom,
                ));
            }
        }
    }

    Ok(open_report(context, script_len))
}

fn require_closing_step_is_final(
    index: TacticStepIndex,
    script_len: usize,
    line: usize,
) -> Result<(), Diagnostic> {
    if index.0 + 1 == script_len {
        Ok(())
    } else {
        Err(Diagnostic::error(
            DiagnosticKind::TacticScriptError,
            Some(line),
            "closing tactic must be the final tactic in a script",
        )
        .with_help(format!(
            "closing tactic at step {} is followed by {} more tactic(s)",
            index.0,
            script_len - index.0 - 1
        )))
    }
}

fn open_report(context: ProofContext, executed_steps: usize) -> TacticScriptReport {
    let obligation = proof_obligation_for_goal(&context);
    let trace = context.steps.clone();
    TacticScriptReport {
        status: TacticScriptStatus::Open,
        executed_steps,
        final_context: Some(context),
        closure: None,
        obligations: vec![obligation],
        trace,
    }
}

fn closed_report(
    executed_steps: usize,
    closure: ProofClosure,
    status: TacticScriptStatus,
) -> TacticScriptReport {
    let trace = closure.steps.clone();
    debug_assert!(matches!(
        (status, closure.status),
        (TacticScriptStatus::ClosedByStaticProof, ProofClosureStatus::ClosedByStaticProof)
            | (TacticScriptStatus::AdmittedByAxiom, ProofClosureStatus::AdmittedByAxiom)
    ));
    TacticScriptReport {
        status,
        executed_steps,
        final_context: None,
        closure: Some(closure),
        obligations: Vec::new(),
        trace,
    }
}
