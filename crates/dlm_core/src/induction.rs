use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::policy::join_trust;
use crate::statement::require_statement_like;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NatInductionSchemeDecl {
    pub theory: String,
    pub proposition_family: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InductionBaseCaseDecl {
    pub theory: String,
    pub proposition: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InductionStepCaseDecl {
    pub theory: String,
    pub proposition: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InductionProofDecl {
    pub theory: String,
    pub proposition: String,
}

pub fn nat_induction_scheme_decl(
    theory: impl Into<String>,
    proposition_family: impl Into<String>,
) -> NatInductionSchemeDecl {
    NatInductionSchemeDecl {
        theory: theory.into(),
        proposition_family: proposition_family.into(),
    }
}

pub fn induction_base_case_decl(
    theory: impl Into<String>,
    proposition: impl Into<String>,
) -> InductionBaseCaseDecl {
    InductionBaseCaseDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn induction_step_case_decl(
    theory: impl Into<String>,
    proposition: impl Into<String>,
) -> InductionStepCaseDecl {
    InductionStepCaseDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn induction_proof_decl(
    theory: impl Into<String>,
    proposition: impl Into<String>,
) -> InductionProofDecl {
    InductionProofDecl {
        theory: theory.into(),
        proposition: proposition.into(),
    }
}

pub fn nat_base_case_proposition(proposition_family: &str) -> String {
    format!("{proposition_family}(0)")
}

pub fn nat_step_case_proposition(proposition_family: &str) -> String {
    format!("forall n:Nat. {proposition_family}(n) -> {proposition_family}(succ(n))")
}

pub fn nat_induction_conclusion(proposition_family: &str) -> String {
    format!("forall n:Nat. {proposition_family}(n)")
}

pub fn nat_induction_scheme(
    theory: &str,
    proposition_family: impl Into<String>,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let proposition_family = require_non_empty(proposition_family.into(), "proposition family", line)?;
    Ok(Passport {
        ty: TypeKind::NatInductionScheme {
            proposition_family: proposition_family.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("induction:nat:scheme:{proposition_family}")),
        location: LocationContext::local(),
    })
}

pub fn induction_base_case(
    theory: &str,
    scheme: &Passport,
    proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let proposition_family = require_nat_induction_scheme(scheme, line)?;
    let expected = nat_base_case_proposition(&proposition_family);
    require_static_proof_of_exact(proof, &expected, line)?;

    Ok(Passport {
        ty: TypeKind::InductionBaseCase {
            proposition: expected.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(
            &scheme.history,
            &proof.history,
            format!("induction:nat:base:{expected}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn induction_step_case(
    theory: &str,
    scheme: &Passport,
    proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let proposition_family = require_nat_induction_scheme(scheme, line)?;
    let expected = nat_step_case_proposition(&proposition_family);
    require_static_proof_of_exact(proof, &expected, line)?;

    Ok(Passport {
        ty: TypeKind::InductionStepCase {
            proposition: expected.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(
            &scheme.history,
            &proof.history,
            format!("induction:nat:step:{expected}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn nat_induction_proof(
    theory: &str,
    scheme: &Passport,
    base_case: &Passport,
    step_case: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let proposition_family = require_nat_induction_scheme(scheme, line)?;
    require_induction_base_case(base_case, &proposition_family, line)?;
    require_induction_step_case(step_case, &proposition_family, line)?;

    let conclusion = nat_induction_conclusion(&proposition_family);
    let trust = join_trust(join_trust(scheme.trust, base_case.trust), step_case.trust);
    let provenance = scheme.provenance.max(base_case.provenance).max(step_case.provenance);
    let validation = join_validation(join_validation(scheme.validation, base_case.validation), step_case.validation);
    let sources = vec![&scheme.history, &base_case.history, &step_case.history];

    Ok(Passport {
        ty: TypeKind::InductionProof {
            proposition: conclusion.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust,
        provenance,
        validation,
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge_many(
            sources,
            format!("induction:nat:proof:{conclusion}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn theorem_from_induction_proof(
    theory: &str,
    name: impl Into<String>,
    statement: &Passport,
    induction_proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let name = require_non_empty(name.into(), "theorem name", line)?;
    let statement_proposition = require_statement_like(statement, line)?.to_string();
    let proof_proposition = require_induction_proof(induction_proof, line)?;

    if statement_proposition != proof_proposition {
        return Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!(
                "induction proof proves `{proof_proposition}`, but theorem statement requires `{statement_proposition}`"
            ),
        )
        .with_help("the target Statement must exactly match the induction conclusion"));
    }

    Ok(Passport {
        ty: TypeKind::Theorem {
            name: name.clone(),
            proposition: statement_proposition.clone(),
        },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(statement.trust, induction_proof.trust),
        provenance: statement.provenance.max(induction_proof.provenance),
        validation: join_validation(statement.validation, induction_proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(
            &statement.history,
            &induction_proof.history,
            format!("theorem:induction:{name}:{statement_proposition}"),
        ),
        location: LocationContext::local(),
    })
}

pub fn require_nat_induction_scheme(
    passport: &Passport,
    line: usize,
) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::NatInductionScheme { proposition_family } => Ok(proposition_family.clone()),
        TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::InductionProof { .. } => {
            Err(Diagnostic::error(
                DiagnosticKind::InductionError,
                Some(line),
                "Nat induction requires an InductionScheme<Nat,P>, not a proof/theorem object",
            )
            .with_help(format!("value passport: {passport}")))
        }
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "Nat induction requires an InductionScheme<Nat,P> passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

pub fn require_induction_base_case(
    passport: &Passport,
    proposition_family: &str,
    line: usize,
) -> Result<String, Diagnostic> {
    let expected = nat_base_case_proposition(proposition_family);
    match &passport.ty {
        TypeKind::InductionBaseCase { proposition } if proposition == &expected => Ok(proposition.clone()),
        TypeKind::InductionBaseCase { proposition } => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("base case proves `{proposition}`, but scheme requires `{expected}`"),
        )),
        TypeKind::StaticProof(_) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "raw StaticProof must first be wrapped as an induction BaseCase for the scheme",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "Nat induction requires a BaseCase passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

pub fn require_induction_step_case(
    passport: &Passport,
    proposition_family: &str,
    line: usize,
) -> Result<String, Diagnostic> {
    let expected = nat_step_case_proposition(proposition_family);
    match &passport.ty {
        TypeKind::InductionStepCase { proposition } if proposition == &expected => Ok(proposition.clone()),
        TypeKind::InductionStepCase { proposition } => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("step case proves `{proposition}`, but scheme requires `{expected}`"),
        )),
        TypeKind::StaticProof(_) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "raw StaticProof must first be wrapped as an induction StepCase for the scheme",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "Nat induction requires a StepCase passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

pub fn require_induction_proof(passport: &Passport, line: usize) -> Result<String, Diagnostic> {
    match &passport.ty {
        TypeKind::InductionProof { proposition } => Ok(proposition.clone()),
        TypeKind::StaticProof(_) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "raw StaticProof is not an InductionProof certificate",
        )),
        TypeKind::RuntimeWitness(_) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "RuntimeWitness cannot close a static Nat induction proof",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "expected an InductionProof passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

fn require_static_proof_of_exact(
    proof: &Passport,
    expected: &str,
    line: usize,
) -> Result<(), Diagnostic> {
    match &proof.ty {
        TypeKind::StaticProof(proposition) if proposition == expected => Ok(()),
        TypeKind::StaticProof(proposition) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("static proof proves `{proposition}`, but induction case requires `{expected}`"),
        )),
        TypeKind::RuntimeWitness(_) => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "RuntimeWitness cannot justify a static induction case",
        )),
        TypeKind::ProofTerm { .. } => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "ProofTerm must be kernel-checked into StaticProof before induction use",
        )),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "induction case requires StaticProof evidence",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

fn induction_capabilities() -> CapabilitySet {
    CapabilitySet::from([
        Capability::CanSymbolicPrint,
        Capability::CanPropositionReason,
        Capability::CanProofKernelCheck,
        Capability::CanCompareByProof,
    ])
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("{label} cannot be empty in Nat induction context"),
        ))
    } else {
        Ok(value)
    }
}

fn join_validation(lhs: ValidationState, rhs: ValidationState) -> ValidationState {
    match (lhs, rhs) {
        (ValidationState::StaticChecked, ValidationState::StaticChecked) => ValidationState::StaticChecked,
        (ValidationState::Assumed, _) | (_, ValidationState::Assumed) => ValidationState::Assumed,
        _ => ValidationState::ConstraintChecked,
    }
}
