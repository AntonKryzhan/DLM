//! v0.76 — Statements, theorems and proofs in `.dlm` syntax.
//!
//! ```text
//! theorem name: P                         closed P, proved by computation
//! theorem name(x: Nat, b: Bool) below N: P   forall x < N, b. P, proved by exhaustion
//! axiom name(x: T, ...): P                 forall x: T. P, admitted (Axiom-tainted)
//! conjecture name(x: Nat) tested N: P      forall x: Nat. P, tested below N — never a theorem
//! ```
//!
//! Every statement goes through the core proof pipeline:
//!
//! ```text
//! statement_passport -> goal_passport -> open_proof_context
//!   -> close_proof_with_static_proof | close_proof_by_axiom
//!   -> Theorem passport -> certificate_from_closure -> verify_certificate_against_theorem
//! ```
//!
//! Proof by computation is sound for the stated proposition because statement
//! bodies may only use pure total functions (checked contracts) and builtins,
//! and evaluation is deterministic. The evaluator is part of the trusted base.
//! Exhaustion proves exactly the bounded proposition it states. An unbounded
//! universal statement is never proved by testing: it is either an axiom
//! (visible Axiom taint) or a conjecture (an open goal with evidence).

use std::collections::BTreeMap;

use super::ordinary_surface::{free_idents, static_evidence, typed_value};
use super::Checker;
use crate::ast::*;
use crate::certificate::{certificate_from_closure, verify_certificate_against_theorem};
use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::ordinary::{statement_proposition, surface_type_of};
use crate::passport::*;
use crate::proof_context::{close_proof_by_axiom, close_proof_with_static_proof, open_proof_context, ProofClosure};
use crate::induction::{
    induction_base_case, induction_step_case, list_base_case_proposition, list_induction_base_case, list_induction_proof,
    list_induction_scheme, list_induction_step_case, list_step_case_proposition, nat_base_case_proposition,
    nat_induction_proof, nat_induction_scheme, nat_step_base_case_proposition, nat_step_case_proposition,
    nat_step_induction_base_case, nat_step_induction_proof, nat_step_induction_scheme, nat_step_induction_step_case,
    nat_step_step_case_proposition,
};
use crate::runtime::Runtime;
use crate::statement::{goal_passport, statement_passport};
use crate::symbolic::{prove_by_induction_with, prove_by_normalization, LemmaRule, ObligationReport};

/// v0.77: a theorem usable as a rewrite lemma by symbolic proofs.
#[derive(Debug, Clone)]
pub(crate) struct LemmaInfo {
    pub vars: Vec<String>,
    pub body: Expr,
    pub theorem: Passport,
}

/// Maximum number of assignments a proof by exhaustion or a conjecture test
/// may evaluate.
pub const MAX_PROOF_CASES: u128 = 100_000;

/// v0.78: Nat range of the runtime cross-check of symbolic proofs.
pub const CROSSCHECK_NAT_BOUND: u64 = 6;

/// v0.78: largest grid evaluated by the cross-check; larger grids are skipped
/// (and recorded as skipped).
pub const CROSSCHECK_MAX_CASES: u128 = 20_000;

fn statement_error(line: usize, message: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::StatementTheoremError, Some(line), message)
}

fn render_assignment(assignment: &[(String, String)]) -> String {
    assignment.iter().map(|(name, value)| format!("{name} = {value}")).collect::<Vec<_>>().join(", ")
}

impl Checker {
    pub(super) fn check_statement_item(&mut self, theory: &str, item: &StatementItem) -> Result<Passport, Diagnostic> {
        let line = item.line;
        let keyword = item.kind.keyword();
        if self.is_builtin_name(&item.name, theory, line) {
            return Err(Diagnostic::error(
                DiagnosticKind::NameError,
                Some(line),
                format!("{keyword} '{}' shadows a builtin", item.name),
            ));
        }

        for variable in &item.params {
            self.require_known_records(&variable.ty, theory, line)?;
            let enumerable = variable.ty == "Nat" || variable.ty == "Bool";
            let symbolic = item.proof.is_some() && ["Nat", "List<Nat>", "Text", "Int"].contains(&variable.ty.as_str());
            if item.kind != StatementItemKind::Axiom && !enumerable && !symbolic && item.proof.is_some() {
                return Err(statement_error(
                    line,
                    format!(
                        "theorem '{}' variable '{}' has type {}; symbolic proofs support Nat, Int, Bool, Text and List<Nat> variables",
                        item.name, variable.name, variable.ty
                    ),
                ));
            }
            if item.kind != StatementItemKind::Axiom && !enumerable && !symbolic {
                return Err(statement_error(
                    line,
                    format!("{keyword} '{}' variable '{}' has type {}; only Nat and Bool variables can be enumerated", item.name, variable.name, variable.ty),
                )
                .with_help("theorems over Nat and List<Nat> variables can be proved symbolically with `by normalize` or `by induction x`"));
            }
        }
        let has_nat = item.params.iter().any(|p| p.ty == "Nat");
        match (item.kind, has_nat, item.bound) {
            (StatementItemKind::Theorem, true, None) if item.proof.is_none() => {
                return Err(statement_error(
                    line,
                    format!("theorem '{}' quantifies over variables without a bound or a proof", item.name),
                )
                .with_help(
                    "a universal statement over all of Nat cannot be proved by testing; prove it symbolically with `by normalize` or `by induction n`, prove the bounded statement with `below N`, admit it as an `axiom`, or record it as a `conjecture ... tested N`",
                ))
            }
            (StatementItemKind::Theorem, false, Some(_)) => {
                return Err(statement_error(
                    line,
                    format!("theorem '{}' has no Nat variables, so `below` does not apply", item.name),
                ))
            }
            _ => {}
        }

        // Statements are about functions, not about theory state: theory values
        // may come from input, effects or axioms and are not visible to proofs.
        let variable_names: Vec<String> = item.params.iter().map(|p| p.name.clone()).collect();
        let mut free = Vec::new();
        free_idents(&item.body, &mut variable_names.clone(), &mut free);
        for name in &free {
            if self.theories.get(theory).is_some_and(|scope| scope.contains_key(name)) {
                return Err(statement_error(
                    line,
                    format!("{keyword} '{}' depends on theory value '{name}'", item.name),
                )
                .with_help("statements may use variables, declared fns, records and builtins; turn the value into a nullary fn: fn name() -> T = ..."));
            }
        }

        let frame: BTreeMap<String, Passport> = item
            .params
            .iter()
            .map(|variable| {
                (
                    variable.name.clone(),
                    typed_value(theory, &variable.ty, &[], format!("statement:{}:var:{}:{}", item.name, variable.name, variable.ty)),
                )
            })
            .collect();
        self.locals.borrow_mut().push(frame);
        let body = self.infer_expr(&item.body, theory);
        self.locals.borrow_mut().pop();
        let body = body?;
        let body_type = surface_type_of(&body).unwrap_or_else(|| body.ty.to_string());
        if body_type != "Bool" {
            return Err(statement_error(line, format!("{keyword} '{}' must be a Bool proposition, got {body_type}", item.name)));
        }

        let (effects, partial) = self.effects_in(&item.body, &item.params, None, theory, line)?;
        if !effects.is_empty() || partial {
            let effects: Vec<String> = effects.iter().map(ToString::to_string).collect();
            return Err(statement_error(
                line,
                format!(
                    "{keyword} '{}' must be pure and total; it uses effects [{}]{}",
                    item.name,
                    effects.join(", "),
                    if partial { " and partial operations" } else { "" }
                ),
            ));
        }

        let proposition = statement_proposition(item);
        let mut statement = statement_passport(theory, proposition.clone());
        statement.trust = statement.trust.max(body.trust);
        statement.provenance = statement.provenance.max(body.provenance);

        match item.kind {
            StatementItemKind::Axiom => {
                let context = open_proof_context(theory, goal_passport(theory, proposition.clone()), line)?;
                let closure = close_proof_by_axiom(context, item.name.clone(), &statement, "admitted by axiom declaration", line)?;
                let theorem = self.certify(&closure, line)?;
                self.register_lemma(theory, item, &theorem);
                Ok(theorem)
            }
            StatementItemKind::Theorem if item.proof.is_some() => {
                let theorem = self.prove_symbolically(theory, item, &statement, &body, &proposition)?;
                Ok(theorem)
            }
            StatementItemKind::Theorem => {
                let (cases, method) = match (item.params.is_empty(), item.bound) {
                    (true, _) => (1, "computation".to_string()),
                    (false, Some(bound)) => (self.case_count(item, bound)?, format!("exhaustion:below={bound}")),
                    (false, None) => (self.case_count(item, 1)?, "exhaustion:finite".to_string()),
                };
                let evaluated = self.evaluate(theory, item)?;
                if let Some(counterexample) = evaluated.counterexample {
                    return Err(statement_error(
                        line,
                        if counterexample.is_empty() {
                            format!("theorem '{}' is false: `{proposition}` evaluates to false", item.name)
                        } else {
                            format!("theorem '{}' is false: counterexample {}", item.name, render_assignment(&counterexample))
                        },
                    ));
                }
                let mut evidence = static_evidence(
                    theory,
                    proposition.clone(),
                    format!("proof:{method}:cases={cases}:theorem={}", item.name),
                );
                evidence.trust = evidence.trust.max(body.trust);
                evidence.provenance = evidence.provenance.max(body.provenance);
                evidence.history = HistoryChain::merge2(&body.history, &evidence.history, format!("proof:{method}:checked"));
                let proof = Passport::static_proof(theory, proposition.clone(), &evidence);
                let context = open_proof_context(theory, goal_passport(theory, proposition.clone()), line)?;
                let closure = close_proof_with_static_proof(context, item.name.clone(), &statement, &proof, line)?;
                let theorem = self.certify(&closure, line)?;
                // A closed theorem is an unconditional ground equation; a
                // bounded theorem only holds below its bound and is not a lemma.
                if item.params.is_empty() {
                    self.register_lemma(theory, item, &theorem);
                }
                Ok(theorem)
            }
            StatementItemKind::Conjecture => {
                let bound = item.bound.expect("parser requires a test bound for conjectures");
                let cases = self.case_count(item, bound)?;
                let evaluated = self.evaluate(theory, item)?;
                if let Some(counterexample) = evaluated.counterexample {
                    return Err(statement_error(
                        line,
                        format!("conjecture '{}' is refuted: counterexample {}", item.name, render_assignment(&counterexample)),
                    ));
                }
                // Testing below N is evidence, not proof: the result stays an
                // open goal that no theorem constructor accepts.
                let mut goal = goal_passport(theory, proposition);
                goal.trust = goal.trust.max(body.trust);
                goal.provenance = goal.provenance.max(body.provenance);
                goal.history.push(format!("conjecture:{}:tested:cases={cases}:below={bound}:no_counterexample", item.name));
                Ok(goal)
            }
        }
    }

    fn certify(&mut self, closure: &ProofClosure, line: usize) -> Result<Passport, Diagnostic> {
        self.certify_with_kernel(closure, line, None)
    }

    fn certify_with_kernel(&mut self, closure: &ProofClosure, line: usize, proof: Option<crate::proof_kernel::KernelProof>) -> Result<Passport, Diagnostic> {
        let mut certificate = certificate_from_closure(closure, line)?;
        if let Some(proof) = proof { certificate = certificate.with_kernel(proof, line)?; }
        verify_certificate_against_theorem(&certificate, &closure.theorem, line)?;
        let mut theorem = closure.theorem.clone();
        if let Some(proof) = &certificate.kernel {
            theorem.history.push(format!("proof:kernel-replay:steps={}:theorem={}", proof.steps.len(), certificate.theorem_name));
        }
        self.certificates.push(certificate);
        Ok(theorem)
    }

    fn case_count(&self, item: &StatementItem, bound: u64) -> Result<u128, Diagnostic> {
        let mut cases: u128 = 1;
        for variable in &item.params {
            let size = if variable.ty == "Bool" { 2 } else { u128::from(bound) };
            cases = cases.saturating_mul(size);
        }
        if cases > MAX_PROOF_CASES {
            return Err(statement_error(
                item.line,
                format!("{} '{}' needs {cases} cases; the limit is {MAX_PROOF_CASES}", item.kind.keyword(), item.name),
            )
            .with_help("lower the bound or the number of variables"));
        }
        Ok(cases)
    }

    fn evaluate(&self, theory: &str, item: &StatementItem) -> Result<crate::runtime::StatementEvaluation, Diagnostic> {
        let functions: Vec<FnDecl> = self
            .functions
            .get(theory)
            .map(|fns| fns.values().map(|f| f.decl.clone()).collect())
            .unwrap_or_default();
        let records: Vec<RecordDecl> = self
            .records
            .get(theory)
            .map(|records| {
                records
                    .values()
                    .map(|report| RecordDecl {
                        id: crate::ids::DeclId::fresh(),
                        name: report.name.clone(),
                        fields: report.fields.iter().map(|f| FnParam { name: f.name.clone(), ty: f.ty.clone() }).collect(),
                        enum_variants: report.enum_variants.clone(),
                        line: item.line,
                        type_params: Vec::new(),
                        type_bounds: Vec::new(),
                    })
                    .collect()
            })
            .unwrap_or_default();
        Runtime::new()
            .evaluate_statement(theory, &functions, &records, &item.params, item.bound.unwrap_or(1), &item.body)
            .map_err(|diag| {
                statement_error(
                    item.line,
                    format!("cannot evaluate {} '{}': {}", item.kind.keyword(), item.name, diag.message),
                )
                .with_help("statements are checked by evaluating pure total code; values outside the exact runtime (big numbers, axioms, symbolic values) cannot be evaluated")
            })
    }

    fn register_lemma(&mut self, theory: &str, item: &StatementItem, theorem: &Passport) {
        self.lemmas.entry(theory.to_string()).or_default().insert(
            item.name.clone(),
            LemmaInfo {
                vars: item.params.iter().map(|p| p.name.clone()).collect(),
                body: item.body.clone(),
                theorem: theorem.clone(),
            },
        );
    }

    /// v0.77: `by normalize` / `by induction n` through the symbolic kernel and
    /// the core induction objects.
    fn prove_symbolically(
        &mut self,
        theory: &str,
        item: &StatementItem,
        statement: &Passport,
        body: &Passport,
        proposition: &str,
    ) -> Result<Passport, Diagnostic> {
        let line = item.line;
        let hint = item.proof.as_ref().expect("caller checked the proof hint");
        for variable in &item.params {
            if !["Nat", "List<Nat>", "Bool", "Text", "Int"].contains(&variable.ty.as_str()) {
                return Err(statement_error(
                    line,
                    format!(
                        "theorem '{}' variable '{}' has type {}; symbolic proofs support Nat, Int, Bool, Text and List<Nat> variables",
                        item.name, variable.name, variable.ty
                    ),
                ));
            }
        }
        // v0.79: induction options.
        if let ProofTactic::Induction(variable) = &hint.tactic {
            let induction_ty = item.params.iter().find(|p| &p.name == variable).map(|p| p.ty.as_str()).unwrap_or("");
            if hint.induction.step > 1 && induction_ty != "Nat" {
                return Err(statement_error(
                    line,
                    format!(
                        "theorem '{}' uses `step {}` on '{variable}' of type {induction_ty}; step induction needs a Nat variable",
                        item.name, hint.induction.step
                    ),
                ));
            }
            if induction_ty == "Bool" {
                return Err(statement_error(line, format!("theorem '{}' inducts on Bool variable '{variable}'", item.name))
                    .with_help("Bool variables are split by cases automatically; use `by normalize`"));
            }
            for name in &hint.induction.generalizing {
                if item.params.iter().any(|p| &p.name == name && p.ty == "Bool") {
                    return Err(statement_error(line, format!("theorem '{}' generalizes Bool variable '{name}'", item.name))
                        .with_help("Bool variables are split by cases outside the induction"));
                }
            }
        }
        let functions: BTreeMap<String, FnDecl> = self
            .functions
            .get(theory)
            .map(|fns| fns.iter().map(|(name, f)| (name.clone(), f.decl.clone())).collect())
            .unwrap_or_default();

        // Lemmas: only unconditional theorems (symbolic, closed, axioms).
        let mut rules = Vec::with_capacity(hint.lemmas.len());
        let mut lemma_passports = Vec::with_capacity(hint.lemmas.len());
        for name in &hint.lemmas {
            let Some(info) = self.lemmas.get(theory).and_then(|lemmas| lemmas.get(name)) else {
                let reason = match self.theories.get(theory).and_then(|scope| scope.get(name)).map(|p| &p.ty) {
                    Some(TypeKind::Theorem { .. }) => "is a bounded theorem; it holds only below its bound",
                    Some(TypeKind::Goal { .. }) => "is a conjecture; tested evidence is not a proof",
                    Some(_) => "is not a theorem",
                    None => "is not declared before this theorem",
                };
                return Err(statement_error(line, format!("lemma '{name}' {reason}"))
                    .with_help("lemmas must be closed theorems, symbolic theorems (by normalize / by induction) or axioms declared earlier"));
            };
            let rule = LemmaRule::from_equation(name, &info.vars, &info.body, &functions).map_err(|err| statement_error(line, err))?;
            rules.push(rule);
            lemma_passports.push(info.theorem.clone());
        }

        let outcome = match &hint.tactic {
            ProofTactic::Normalize => prove_by_normalization(&functions, &rules, &item.params, &item.body),
            ProofTactic::Induction(variable) => {
                prove_by_induction_with(&functions, &rules, &item.params, variable, &item.body, &hint.induction)
            }
        };
        let proof = outcome.map_err(|failure| {
            statement_error(line, format!("cannot prove theorem '{}': {failure}", item.name)).with_help(
                "the symbolic kernel unfolds definitions, normalizes Nat polynomials and uses the induction hypothesis and `using` lemmas as rewrite rules; add a lemma or restate the goal",
            )
        })?;
        let crosscheck = self.crosscheck(theory, item)?;

        // Trust of the proof: the statement body plus every lemma used.
        let mut trust = body.trust;
        let mut provenance = body.provenance;
        for lemma in &lemma_passports {
            trust = trust.max(lemma.trust);
            provenance = provenance.max(lemma.provenance);
        }
        let lemma_list = hint.lemmas.join(",");
        let evidence_for = |predicate: String, report: &ObligationReport, label: &str| {
            let mut evidence = static_evidence(
                theory,
                predicate,
                format!(
                    "proof:symbolic:{label}:{}:steps={}:lemmas=[{lemma_list}]:theorem={}",
                    report.method, report.steps, item.name
                ),
            );
            evidence.trust = evidence.trust.max(trust);
            evidence.provenance = evidence.provenance.max(provenance);
            for lemma in &lemma_passports {
                evidence.history = HistoryChain::merge2(&evidence.history, &lemma.history, format!("lemma:use:{}", lemma_name(lemma)));
            }
            evidence
        };

        // P_name(variable) := body, quantified over the generalized variables.
        let instantiation = |family: &str, variable: &str| {
            let generalized: Vec<String> = item
                .params
                .iter()
                .filter(|p| hint.induction.generalizing.contains(&p.name))
                .map(|p| format!("{}: {}", p.name, p.ty))
                .collect();
            let body = crate::ordinary::render_expr(&item.body);
            if generalized.is_empty() {
                format!("induction:instantiate:{family}({variable}) := {body}")
            } else {
                format!("induction:instantiate:{family}({variable}) := forall {}. {body}", generalized.join(", "))
            }
        };

        let mut static_proof = match &hint.tactic {
            ProofTactic::Normalize => {
                let evidence = evidence_for(proposition.to_string(), &proof.obligations[0].1, "normalize");
                Passport::static_proof(theory, proposition.to_string(), &evidence)
            }
            ProofTactic::Induction(variable)
                if item.params.iter().any(|p| &p.name == variable && p.ty.starts_with("List<")) =>
            {
                // v0.78: structural list induction through the core list
                // induction objects.
                let family = format!("P_{}", item.name);
                let element = item
                    .params
                    .iter()
                    .find(|p| &p.name == variable)
                    .and_then(|p| p.ty.strip_prefix("List<").and_then(|t| t.strip_suffix('>')))
                    .unwrap_or("Nat")
                    .to_string();
                let scheme = list_induction_scheme(theory, element.clone(), family.clone(), line)?;
                let base_proposition = list_base_case_proposition(&family);
                let step_proposition = list_step_case_proposition(&element, &family);
                let base_evidence = evidence_for(base_proposition.clone(), &proof.obligations[0].1, "base");
                let step_evidence = evidence_for(step_proposition.clone(), &proof.obligations[1].1, "step");
                let base_proof = Passport::static_proof(theory, base_proposition, &base_evidence);
                let step_proof = Passport::static_proof(theory, step_proposition, &step_evidence);
                let base_case = list_induction_base_case(theory, &scheme, &base_proof, line)?;
                let step_case = list_induction_step_case(theory, &scheme, &step_proof, line)?;
                let mut induction = list_induction_proof(theory, &scheme, &base_case, &step_case, line)?;
                induction.history.push(instantiation(&family, variable));
                Passport::static_proof(theory, proposition.to_string(), &induction)
            }
            ProofTactic::Induction(variable) if hint.induction.step > 1 => {
                // v0.79: step-s induction through the core step scheme.
                let family = format!("P_{}", item.name);
                let step = hint.induction.step;
                let scheme = nat_step_induction_scheme(theory, family.clone(), step, line)?;
                let mut base_cases = Vec::with_capacity(step as usize);
                for index in 0..step {
                    let base_proposition = nat_step_base_case_proposition(&family, index);
                    let evidence = evidence_for(base_proposition.clone(), &proof.obligations[index as usize].1, "base");
                    let base_proof = Passport::static_proof(theory, base_proposition, &evidence);
                    base_cases.push(nat_step_induction_base_case(theory, &scheme, index, &base_proof, line)?);
                }
                let step_proposition = nat_step_step_case_proposition(&family, step);
                let step_evidence = evidence_for(step_proposition.clone(), &proof.obligations[step as usize].1, "step");
                let step_proof = Passport::static_proof(theory, step_proposition, &step_evidence);
                let step_case = nat_step_induction_step_case(theory, &scheme, &step_proof, line)?;
                let mut induction = nat_step_induction_proof(theory, &scheme, &base_cases, &step_case, line)?;
                induction.history.push(instantiation(&family, variable));
                Passport::static_proof(theory, proposition.to_string(), &induction)
            }
            ProofTactic::Induction(variable) => {
                // Core induction objects over the proposition family P_name,
                // where P_name(variable) is the statement body with the other
                // variables held fixed.
                let family = format!("P_{}", item.name);
                let scheme = nat_induction_scheme(theory, family.clone(), line)?;
                let base_evidence = evidence_for(nat_base_case_proposition(&family), &proof.obligations[0].1, "base");
                let step_evidence = evidence_for(nat_step_case_proposition(&family), &proof.obligations[1].1, "step");
                let base_proof = Passport::static_proof(theory, nat_base_case_proposition(&family), &base_evidence);
                let step_proof = Passport::static_proof(theory, nat_step_case_proposition(&family), &step_evidence);
                let base_case = induction_base_case(theory, &scheme, &base_proof, line)?;
                let step_case = induction_step_case(theory, &scheme, &step_proof, line)?;
                let mut induction = nat_induction_proof(theory, &scheme, &base_case, &step_case, line)?;
                induction.history.push(instantiation(&family, variable));
                Passport::static_proof(theory, proposition.to_string(), &induction)
            }
        };

        // v0.78: the runtime cross-check is recorded next to the kernel evidence.
        static_proof.history.push(crosscheck);

        let context = open_proof_context(theory, goal_passport(theory, proposition.to_string()), line)?;
        let closure = close_proof_with_static_proof(context, item.name.clone(), statement, &static_proof, line)?;
        let replay = if matches!(hint.tactic, ProofTactic::Normalize) && hint.lemmas.is_empty() {
            crate::proof_kernel::builder::build_equational_proof(item)
        } else { None };
        let theorem = self.certify_with_kernel(&closure, line, replay)?;
        self.register_lemma(theory, item, &theorem);
        Ok(theorem)
    }

    /// v0.78: independent cross-check of a kernel proof. The runtime (a
    /// separate implementation) evaluates the statement on a finite grid:
    /// Nat variables in `0..CROSSCHECK_NAT_BOUND`, List<Nat> variables over all
    /// lists of length <= 3 with elements below 3. This is not a proof; a
    /// counterexample means the kernel and the runtime disagree, which is a
    /// hard error.
    fn crosscheck(&self, theory: &str, item: &StatementItem) -> Result<String, Diagnostic> {
        let mut cases: u128 = 1;
        for variable in &item.params {
            cases = cases.saturating_mul(match variable.ty.as_str() {
                "Nat" => u128::from(CROSSCHECK_NAT_BOUND),
                "Bool" => 2,
                "Text" => 5,
                "Int" => 2 * u128::from(CROSSCHECK_NAT_BOUND) - 1,
                _ => 40,
            });
        }
        if cases > CROSSCHECK_MAX_CASES {
            return Ok(format!("crosscheck:runtime:skipped:cases={cases}:theorem={}", item.name));
        }
        let mut probe = item.clone();
        probe.bound = Some(CROSSCHECK_NAT_BOUND);
        match self.evaluate(theory, &probe) {
            Ok(outcome) => match outcome.counterexample {
                None => Ok(format!("crosscheck:runtime:agree:cases={}:theorem={}", outcome.cases, item.name)),
                Some(counterexample) => Err(statement_error(
                    item.line,
                    format!(
                        "kernel/runtime disagreement: the symbolic kernel proved theorem '{}', but the runtime evaluates it to false at {}",
                        item.name,
                        render_assignment(&counterexample)
                    ),
                )
                .with_help("either an axiom used via `using` is false for the declared functions, or the symbolic kernel and the runtime disagree; the theorem is rejected")),
            },
            Err(diag) => Ok(format!(
                "crosscheck:runtime:skipped:{}:theorem={}",
                diag.message.replace(':', ";").chars().take(80).collect::<String>(),
                item.name
            )),
        }
    }
}

fn lemma_name(theorem: &Passport) -> String {
    match &theorem.ty {
        TypeKind::Theorem { name, .. } => name.clone(),
        other => other.to_string(),
    }
}
