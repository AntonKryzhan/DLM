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

/// v0.78: base case of structural list induction, `P([])`.
pub fn list_base_case_proposition(proposition_family: &str) -> String {
    format!("{proposition_family}([])")
}

/// v0.78: step case of structural list induction.
pub fn list_step_case_proposition(element: &str, proposition_family: &str) -> String {
    format!("forall h:{element}, t:List<{element}>. {proposition_family}(t) -> {proposition_family}(cons(h, t))")
}

/// v0.78: conclusion of structural list induction.
pub fn list_induction_conclusion(element: &str, proposition_family: &str) -> String {
    format!("forall xs:List<{element}>. {proposition_family}(xs)")
}

/// v0.78: `InductionScheme<List<element>, P>`.
pub fn list_induction_scheme(
    theory: &str,
    element: impl Into<String>,
    proposition_family: impl Into<String>,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let element = require_non_empty(element.into(), "list element type", line)?;
    let proposition_family = require_non_empty(proposition_family.into(), "proposition family", line)?;
    Ok(Passport {
        ty: TypeKind::ListInductionScheme { element: element.clone(), proposition_family: proposition_family.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("induction:list:scheme:List<{element}>:{proposition_family}")),
        location: LocationContext::local(),
    })
}

pub fn require_list_induction_scheme(passport: &Passport, line: usize) -> Result<(String, String), Diagnostic> {
    match &passport.ty {
        TypeKind::ListInductionScheme { element, proposition_family } => Ok((element.clone(), proposition_family.clone())),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "list induction requires an InductionScheme<List<T>,P> passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

/// v0.78: base case `P([])` of a list induction scheme.
pub fn list_induction_base_case(theory: &str, scheme: &Passport, proof: &Passport, line: usize) -> Result<Passport, Diagnostic> {
    let (_, proposition_family) = require_list_induction_scheme(scheme, line)?;
    let expected = list_base_case_proposition(&proposition_family);
    require_static_proof_of_exact(proof, &expected, line)?;
    Ok(Passport {
        ty: TypeKind::InductionBaseCase { proposition: expected.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(&scheme.history, &proof.history, format!("induction:list:base:{expected}")),
        location: LocationContext::local(),
    })
}

/// v0.78: step case `P(t) -> P(cons(h, t))` of a list induction scheme.
pub fn list_induction_step_case(theory: &str, scheme: &Passport, proof: &Passport, line: usize) -> Result<Passport, Diagnostic> {
    let (element, proposition_family) = require_list_induction_scheme(scheme, line)?;
    let expected = list_step_case_proposition(&element, &proposition_family);
    require_static_proof_of_exact(proof, &expected, line)?;
    Ok(Passport {
        ty: TypeKind::InductionStepCase { proposition: expected.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(&scheme.history, &proof.history, format!("induction:list:step:{expected}")),
        location: LocationContext::local(),
    })
}

/// v0.78: `InductionProof<forall xs:List<T>. P(xs)>` from a base and a step case
/// of the same list scheme.
pub fn list_induction_proof(
    theory: &str,
    scheme: &Passport,
    base_case: &Passport,
    step_case: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let (element, proposition_family) = require_list_induction_scheme(scheme, line)?;
    for (case, expected, label) in [
        (base_case, list_base_case_proposition(&proposition_family), "base"),
        (step_case, list_step_case_proposition(&element, &proposition_family), "step"),
    ] {
        let found = match &case.ty {
            TypeKind::InductionBaseCase { proposition } if label == "base" => proposition,
            TypeKind::InductionStepCase { proposition } if label == "step" => proposition,
            other => {
                return Err(Diagnostic::error(
                    DiagnosticKind::InductionError,
                    Some(line),
                    format!("list induction requires a {label} case passport"),
                )
                .with_help(format!("received passport type: {other}")))
            }
        };
        if found != &expected {
            return Err(Diagnostic::error(
                DiagnosticKind::InductionError,
                Some(line),
                format!("{label} case proves `{found}`, but scheme requires `{expected}`"),
            ));
        }
    }
    let conclusion = list_induction_conclusion(&element, &proposition_family);
    Ok(Passport {
        ty: TypeKind::InductionProof { proposition: conclusion.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(join_trust(scheme.trust, base_case.trust), step_case.trust),
        provenance: scheme.provenance.max(base_case.provenance).max(step_case.provenance),
        validation: join_validation(join_validation(scheme.validation, base_case.validation), step_case.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge_many(
            vec![&scheme.history, &base_case.history, &step_case.history],
            format!("induction:list:proof:{conclusion}"),
        ),
        location: LocationContext::local(),
    })
}

/// v0.79: `n + offset` in induction propositions (`n` for offset 0).
fn nat_offset(offset: u32) -> String {
    if offset == 0 {
        "n".to_string()
    } else {
        format!("n+{offset}")
    }
}

/// v0.79: base case `P(index)` of a step-`s` Nat induction.
pub fn nat_step_base_case_proposition(proposition_family: &str, index: u32) -> String {
    format!("{proposition_family}({index})")
}

/// v0.79: step case `forall n:Nat. P(n) and .. and P(n+s-1) -> P(n+s)`.
pub fn nat_step_step_case_proposition(proposition_family: &str, step: u32) -> String {
    let premises = (0..step).map(|i| format!("{proposition_family}({})", nat_offset(i))).collect::<Vec<_>>().join(" and ");
    format!("forall n:Nat. {premises} -> {proposition_family}({})", nat_offset(step))
}

/// v0.79: `InductionScheme<Nat,step=s,P>` for `2 <= s`.
pub fn nat_step_induction_scheme(
    theory: &str,
    proposition_family: impl Into<String>,
    step: u32,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let proposition_family = require_non_empty(proposition_family.into(), "proposition family", line)?;
    if step < 2 {
        return Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("step induction needs step >= 2, got {step}; use the Nat induction scheme for step 1"),
        ));
    }
    Ok(Passport {
        ty: TypeKind::NatStepInductionScheme { proposition_family: proposition_family.clone(), step },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("induction:nat_step:scheme:step={step}:{proposition_family}")),
        location: LocationContext::local(),
    })
}

fn require_nat_step_induction_scheme(passport: &Passport, line: usize) -> Result<(String, u32), Diagnostic> {
    match &passport.ty {
        TypeKind::NatStepInductionScheme { proposition_family, step } => Ok((proposition_family.clone(), *step)),
        other => Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            "step induction requires an InductionScheme<Nat,step=s,P> passport",
        )
        .with_help(format!("received passport type: {other}"))),
    }
}

/// v0.79: base case `P(index)`, `index < step`.
pub fn nat_step_induction_base_case(
    theory: &str,
    scheme: &Passport,
    index: u32,
    proof: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let (proposition_family, step) = require_nat_step_induction_scheme(scheme, line)?;
    if index >= step {
        return Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("base case {index} is outside the scheme bases 0..{step}"),
        ));
    }
    let expected = nat_step_base_case_proposition(&proposition_family, index);
    require_static_proof_of_exact(proof, &expected, line)?;
    Ok(Passport {
        ty: TypeKind::InductionBaseCase { proposition: expected.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(&scheme.history, &proof.history, format!("induction:nat_step:base:{expected}")),
        location: LocationContext::local(),
    })
}

/// v0.79: step case of a step-`s` scheme.
pub fn nat_step_induction_step_case(theory: &str, scheme: &Passport, proof: &Passport, line: usize) -> Result<Passport, Diagnostic> {
    let (proposition_family, step) = require_nat_step_induction_scheme(scheme, line)?;
    let expected = nat_step_step_case_proposition(&proposition_family, step);
    require_static_proof_of_exact(proof, &expected, line)?;
    Ok(Passport {
        ty: TypeKind::InductionStepCase { proposition: expected.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust: join_trust(scheme.trust, proof.trust),
        provenance: scheme.provenance.max(proof.provenance),
        validation: join_validation(scheme.validation, proof.validation),
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge2(&scheme.history, &proof.history, format!("induction:nat_step:step:{expected}")),
        location: LocationContext::local(),
    })
}

/// v0.79: `InductionProof<forall n:Nat. P(n)>` from the bases `P(0) .. P(s-1)`
/// in order and the step case of the same scheme.
pub fn nat_step_induction_proof(
    theory: &str,
    scheme: &Passport,
    base_cases: &[Passport],
    step_case: &Passport,
    line: usize,
) -> Result<Passport, Diagnostic> {
    let (proposition_family, step) = require_nat_step_induction_scheme(scheme, line)?;
    if base_cases.len() != step as usize {
        return Err(Diagnostic::error(
            DiagnosticKind::InductionError,
            Some(line),
            format!("step-{step} induction needs {step} base cases, got {}", base_cases.len()),
        ));
    }
    for (index, base) in base_cases.iter().enumerate() {
        let expected = nat_step_base_case_proposition(&proposition_family, index as u32);
        match &base.ty {
            TypeKind::InductionBaseCase { proposition } if proposition == &expected => {}
            other => {
                return Err(Diagnostic::error(
                    DiagnosticKind::InductionError,
                    Some(line),
                    format!("base case {index} must be BaseCase<{expected}>"),
                )
                .with_help(format!("received passport type: {other}")))
            }
        }
    }
    let expected_step = nat_step_step_case_proposition(&proposition_family, step);
    match &step_case.ty {
        TypeKind::InductionStepCase { proposition } if proposition == &expected_step => {}
        other => {
            return Err(Diagnostic::error(
                DiagnosticKind::InductionError,
                Some(line),
                format!("step case must be StepCase<{expected_step}>"),
            )
            .with_help(format!("received passport type: {other}")))
        }
    }
    let conclusion = nat_induction_conclusion(&proposition_family);
    let mut trust = scheme.trust;
    let mut provenance = scheme.provenance;
    let mut validation = scheme.validation;
    for part in base_cases.iter().chain(std::iter::once(step_case)) {
        trust = join_trust(trust, part.trust);
        provenance = provenance.max(part.provenance);
        validation = join_validation(validation, part.validation);
    }
    let mut sources = vec![&scheme.history];
    sources.extend(base_cases.iter().map(|b| &b.history));
    sources.push(&step_case.history);
    Ok(Passport {
        ty: TypeKind::InductionProof { proposition: conclusion.clone() },
        construction: ConstructionMode::ProofFinite,
        capabilities: induction_capabilities(),
        cost: CostClass::ProofRequired,
        trust,
        provenance,
        validation,
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge_many(sources, format!("induction:nat_step:proof:step={step}:{conclusion}")),
        location: LocationContext::local(),
    })
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
