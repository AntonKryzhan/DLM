//! v0.81 — `dlm audit`: the trusted base of a checked program.
//!
//! The audit is computed from passports, not from a separate bookkeeping
//! structure: evidence events (`proof:...:theorem=<name>`), lemma uses
//! (`lemma:use:<name>`) and axiom admissions (`theorem:axiom:<name>:<P>`) are
//! carried in passport histories across lemmas, theory extension and module
//! boundaries. A theorem whose trust is `Axiom` or worse but whose history
//! names no axiom is reported as unattributed taint instead of being hidden.

use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

use crate::checker::CheckReport;
use crate::passport::{Passport, TrustLevel, TypeKind};

/// Parts of the implementation a statement's acceptance relies on.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TrustedComponent {
    /// `Runtime::evaluate_statement`: proof by computation / exhaustion.
    RuntimeEvaluator,
    /// `symbolic.rs`: `by normalize` / `by induction`.
    SymbolicKernel,
    /// `proof_kernel.rs`: independent replay of structured equational evidence.
    EquationalReplayKernel,
    /// Declared axioms (`theorem:axiom:...`).
    Axioms,
    /// Oracle-level trust.
    Oracle,
    /// Unsafe trust or provenance.
    Unsafe,
}

impl fmt::Display for TrustedComponent {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            TrustedComponent::RuntimeEvaluator => "runtime-evaluator",
            TrustedComponent::SymbolicKernel => "symbolic-kernel",
            TrustedComponent::EquationalReplayKernel => "equational-replay-kernel",
            TrustedComponent::Axioms => "axioms",
            TrustedComponent::Oracle => "oracle",
            TrustedComponent::Unsafe => "unsafe",
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StatementAuditKind {
    Theorem,
    Axiom,
    Conjecture,
}

/// One theorem, axiom or conjecture of the audited module.
#[derive(Debug, Clone)]
pub struct StatementAudit {
    /// `Theory.name`.
    pub name: String,
    pub proposition: String,
    pub kind: StatementAuditKind,
    /// `computation`, `exhaustion:below=N`, `symbolic:normalize`,
    /// `symbolic:induction`, `axiom`, `conjecture:tested:cases=N`.
    pub method: String,
    pub trust: TrustLevel,
    /// Lemmas used, transitively.
    pub lemmas: Vec<String>,
    /// Axioms the statement depends on, transitively (`name: proposition`).
    pub axioms: Vec<String>,
    /// `agree:cases=N` or `skipped:...` for symbolic theorems.
    pub crosscheck: Option<String>,
    pub components: BTreeSet<TrustedComponent>,
    /// Trust is `Axiom` or worse, but no axiom is named in the history.
    pub unattributed_taint: bool,
}

/// An axiom and the statements of the module that depend on it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AxiomUse {
    pub axiom: String,
    pub used_by: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum AuditStatus {
    /// No axioms, oracles or unsafe values: every theorem rests on the
    /// runtime evaluator and/or the symbolic kernel only.
    Closed,
    /// Some theorem depends on an axiom, or the module declares one.
    AxiomDependent,
    /// Oracle or unsafe trust is present.
    Unsafe,
}

impl fmt::Display for AuditStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            AuditStatus::Closed => "closed",
            AuditStatus::AxiomDependent => "axiom-dependent",
            AuditStatus::Unsafe => "unsafe",
        })
    }
}

/// Trusted-base audit of one checked module.
#[derive(Debug, Clone)]
pub struct ProgramAudit {
    pub module: String,
    pub imports: Vec<String>,
    pub statements: Vec<StatementAudit>,
    pub axioms: Vec<AxiomUse>,
    /// Values (not statements) with Axiom trust or worse: `name (Trust)`.
    pub tainted_values: Vec<String>,
    /// Declared bridges: `name: Source -> Target (kind)`.
    pub bridges: Vec<String>,
    pub components: BTreeSet<TrustedComponent>,
    pub crosscheck_agree: usize,
    pub crosscheck_skipped: usize,
    pub status: AuditStatus,
    pub fingerprint: String,
}

impl ProgramAudit {
    pub fn proved(&self) -> usize {
        self.statements.iter().filter(|s| s.kind == StatementAuditKind::Theorem).count()
    }

    pub fn open_goals(&self) -> usize {
        self.statements.iter().filter(|s| s.kind == StatementAuditKind::Conjecture).count()
    }
}

fn fnv64(text: &str) -> String {
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for byte in text.bytes() {
        hash ^= u64::from(byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("fnv64:{hash:016x}")
}

fn push_unique(list: &mut Vec<String>, item: String) {
    if !list.contains(&item) {
        list.push(item);
    }
}

fn statement_audit(qualified: &str, passport: &Passport) -> Option<StatementAudit> {
    let short = qualified.rsplit('.').next().unwrap_or(qualified);
    let events = passport.history.events();
    let (kind, proposition) = match &passport.ty {
        TypeKind::Theorem { name, proposition } if name == short => {
            let own_axiom = format!("theorem:axiom:{name}:");
            if events.iter().any(|e| e.starts_with(&own_axiom)) {
                (StatementAuditKind::Axiom, proposition.clone())
            } else {
                (StatementAuditKind::Theorem, proposition.clone())
            }
        }
        TypeKind::Goal { proposition } if events.iter().any(|e| e.starts_with(&format!("conjecture:{short}:tested:"))) => {
            (StatementAuditKind::Conjecture, proposition.clone())
        }
        _ => return None,
    };

    let own_suffix = format!(":theorem={short}");
    let mut method_parts: Vec<String> = Vec::new();
    let mut components = BTreeSet::new();
    let mut crosscheck = None;
    let mut lemmas = Vec::new();
    let mut axioms = Vec::new();
    for event in events {
        if let Some(rest) = event.strip_prefix("proof:") {
            if !event.ends_with(&own_suffix) {
                continue;
            }
            if rest.starts_with("computation") {
                push_unique(&mut method_parts, "computation".to_string());
                components.insert(TrustedComponent::RuntimeEvaluator);
            } else if let Some(exhaustion) = rest.strip_prefix("exhaustion:") {
                let bound = exhaustion.split(":cases=").next().unwrap_or(exhaustion);
                push_unique(&mut method_parts, format!("exhaustion:{bound}"));
                components.insert(TrustedComponent::RuntimeEvaluator);
            } else if let Some(symbolic) = rest.strip_prefix("symbolic:") {
                let label = symbolic.split(':').next().unwrap_or("");
                let tactic = if label == "normalize" { "symbolic:normalize" } else { "symbolic:induction" };
                push_unique(&mut method_parts, tactic.to_string());
                components.insert(TrustedComponent::SymbolicKernel);
            } else if rest.starts_with("kernel-replay:") {
                components.insert(TrustedComponent::EquationalReplayKernel);
            }
        } else if let Some(rest) = event.strip_prefix("crosscheck:runtime:") {
            if event.ends_with(&own_suffix) {
                crosscheck = Some(rest.trim_end_matches(&own_suffix).to_string());
            }
        } else if let Some(lemma) = event.strip_prefix("lemma:use:") {
            push_unique(&mut lemmas, lemma.to_string());
        } else if let Some(rest) = event.strip_prefix("theorem:axiom:") {
            let (name, proposition) = rest.split_once(':').unwrap_or((rest, ""));
            if kind == StatementAuditKind::Axiom && name == short {
                continue;
            }
            push_unique(&mut axioms, format!("{name}: {proposition}"));
        } else if let Some(rest) = event.strip_prefix(&format!("conjecture:{short}:tested:")) {
            let cases = rest.split(':').next().unwrap_or(rest);
            push_unique(&mut method_parts, format!("conjecture:tested:{cases}"));
        }
    }
    if kind == StatementAuditKind::Axiom {
        method_parts = vec!["axiom".to_string()];
        components.insert(TrustedComponent::Axioms);
    }
    if !axioms.is_empty() {
        components.insert(TrustedComponent::Axioms);
    }
    if passport.trust >= TrustLevel::Unsafe {
        components.insert(TrustedComponent::Unsafe);
    } else if passport.trust >= TrustLevel::Oracle {
        components.insert(TrustedComponent::Oracle);
    }
    let unattributed_taint =
        kind == StatementAuditKind::Theorem && passport.trust >= TrustLevel::Axiom && axioms.is_empty();
    Some(StatementAudit {
        name: qualified.to_string(),
        proposition,
        kind,
        method: if method_parts.is_empty() { "unknown".to_string() } else { method_parts.join("+") },
        trust: passport.trust,
        lemmas,
        axioms,
        crosscheck,
        components,
        unattributed_taint,
    })
}

/// Audit the root module of a check report. `imports` are the module names
/// the root imports (direct and transitive), for display.
pub fn audit_check_report(report: &CheckReport, imports: Vec<String>) -> ProgramAudit {
    let mut statements = Vec::new();
    let mut tainted_values = Vec::new();
    let mut value_components = BTreeSet::new();
    for (name, passport) in &report.inferred {
        match statement_audit(name, passport) {
            Some(statement) => statements.push(statement),
            None if passport.trust >= TrustLevel::Axiom => {
                tainted_values.push(format!("{name} ({:?})", passport.trust));
                value_components.insert(if passport.trust >= TrustLevel::Unsafe {
                    TrustedComponent::Unsafe
                } else if passport.trust >= TrustLevel::Oracle {
                    TrustedComponent::Oracle
                } else {
                    TrustedComponent::Axioms
                });
            }
            None => {}
        }
    }
    let mut bridges = Vec::new();
    for bridge in &report.bridges {
        let kind = match &bridge.kind {
            crate::ast::BridgeKind::Unknown(kind) => kind.clone(),
            other => format!("{other:?}").to_lowercase(),
        };
        if bridge.kind == crate::ast::BridgeKind::Unsafe {
            value_components.insert(TrustedComponent::Unsafe);
        }
        bridges.push(format!("{}: {} -> {} ({kind})", bridge.name, bridge.source, bridge.target));
    }

    let mut axiom_users: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for statement in &statements {
        if statement.kind == StatementAuditKind::Axiom {
            axiom_users.entry(format!("{}: {}", statement.name.rsplit('.').next().unwrap_or(&statement.name), statement.proposition)).or_default();
        }
        for axiom in &statement.axioms {
            axiom_users.entry(axiom.clone()).or_default().push(statement.name.clone());
        }
    }
    let axioms: Vec<AxiomUse> = axiom_users.into_iter().map(|(axiom, used_by)| AxiomUse { axiom, used_by }).collect();

    let mut components = value_components;
    for statement in &statements {
        components.extend(statement.components.iter().copied());
    }
    let crosscheck_agree = statements.iter().filter(|s| s.crosscheck.as_deref().is_some_and(|c| c.starts_with("agree"))).count();
    let crosscheck_skipped = statements.iter().filter(|s| s.crosscheck.as_deref().is_some_and(|c| c.starts_with("skipped"))).count();
    let status = if components.contains(&TrustedComponent::Unsafe) || components.contains(&TrustedComponent::Oracle) {
        AuditStatus::Unsafe
    } else if components.contains(&TrustedComponent::Axioms)
        || !axioms.is_empty()
        || statements.iter().any(|s| s.unattributed_taint)
    {
        AuditStatus::AxiomDependent
    } else {
        AuditStatus::Closed
    };

    let mut audit = ProgramAudit {
        module: report.module_name.clone(),
        imports,
        statements,
        axioms,
        tainted_values,
        bridges,
        components,
        crosscheck_agree,
        crosscheck_skipped,
        status,
        fingerprint: String::new(),
    };
    audit.fingerprint = fnv64(&render_body(&audit));
    audit
}

fn render_body(audit: &ProgramAudit) -> String {
    let mut out = String::new();
    let line = |out: &mut String, text: String| {
        out.push_str(&text);
        out.push('\n');
    };
    line(&mut out, format!("module: {}", audit.module));
    line(&mut out, format!("imports: {}", if audit.imports.is_empty() { "-".to_string() } else { audit.imports.join(", ") }));
    line(&mut out, format!("status: {}", audit.status));
    let count = |component: TrustedComponent| audit.statements.iter().filter(|s| s.components.contains(&component)).count();
    let components: Vec<String> = audit
        .components
        .iter()
        .map(|c| match c {
            _ if count(*c) == 0 => c.to_string(),
            _ => format!("{c} ({})", count(*c)),
        })
        .collect();
    line(&mut out, format!("trusted components: {}", if components.is_empty() { "-".to_string() } else { components.join(", ") }));
    line(&mut out, format!("runtime cross-check: agree={} skipped={}", audit.crosscheck_agree, audit.crosscheck_skipped));
    line(
        &mut out,
        format!(
            "statements: {} proved, {} axioms, {} open goals",
            audit.proved(),
            audit.statements.iter().filter(|s| s.kind == StatementAuditKind::Axiom).count(),
            audit.open_goals()
        ),
    );
    for statement in &audit.statements {
        let keyword = match statement.kind {
            StatementAuditKind::Theorem => "theorem",
            StatementAuditKind::Axiom => "axiom",
            StatementAuditKind::Conjecture => "open goal",
        };
        line(&mut out, String::new());
        line(&mut out, format!("{keyword} {}", statement.name));
        line(&mut out, format!("  proposition: {}", statement.proposition));
        line(&mut out, format!("  method: {}", statement.method));
        line(&mut out, format!("  trust: {:?}", statement.trust));
        if !statement.lemmas.is_empty() {
            line(&mut out, format!("  lemmas: {}", statement.lemmas.join(", ")));
        }
        if !statement.axioms.is_empty() {
            line(&mut out, format!("  axioms: {}", statement.axioms.join("; ")));
        }
        if statement.unattributed_taint {
            line(&mut out, "  warning: Axiom-level trust without a named axiom in the history".to_string());
        }
        if let Some(crosscheck) = &statement.crosscheck {
            line(&mut out, format!("  crosscheck: {crosscheck}"));
        }
    }
    if !audit.axioms.is_empty() {
        line(&mut out, String::new());
        for axiom in &audit.axioms {
            line(
                &mut out,
                format!(
                    "axiom dependency {} <- {}",
                    axiom.axiom,
                    if axiom.used_by.is_empty() { "(declared, unused)".to_string() } else { axiom.used_by.join(", ") }
                ),
            );
        }
    }
    for value in &audit.tainted_values {
        line(&mut out, format!("tainted value {value}"));
    }
    for bridge in &audit.bridges {
        line(&mut out, format!("bridge {bridge}"));
    }
    out
}

/// Text form: `DLM-AUDIT v1`, the audit body and its fingerprint.
pub fn render_program_audit(audit: &ProgramAudit) -> String {
    format!("DLM-AUDIT v1\n{}fingerprint: {}\n", render_body(audit), audit.fingerprint)
}
