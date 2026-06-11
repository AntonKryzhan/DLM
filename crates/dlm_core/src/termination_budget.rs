use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};
use crate::recursion::{RecursiveCallReport, RecursionSchemeReport, RecursionStatus};
use crate::rewrite_normalization::RewriteNormalizationReport;
use crate::traversal::{FoldTraversalReport, MapTraversalReport, TraversalStatus};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TerminationBudgetStatus {
    VerifiedUnified,
    Downgraded,
    Open,
    RejectedBudgetExceeded,
    RejectedInconsistent,
}

impl fmt::Display for TerminationBudgetStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TerminationBudgetStatus::VerifiedUnified => write!(f, "verified_unified"),
            TerminationBudgetStatus::Downgraded => write!(f, "downgraded"),
            TerminationBudgetStatus::Open => write!(f, "open"),
            TerminationBudgetStatus::RejectedBudgetExceeded => write!(f, "rejected_budget_exceeded"),
            TerminationBudgetStatus::RejectedInconsistent => write!(f, "rejected_inconsistent"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum BudgetDomain {
    RewriteNormalization,
    Traversal,
    Recursion,
    Unified,
}

impl fmt::Display for BudgetDomain {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BudgetDomain::RewriteNormalization => write!(f, "rewrite_normalization"),
            BudgetDomain::Traversal => write!(f, "traversal"),
            BudgetDomain::Recursion => write!(f, "recursion"),
            BudgetDomain::Unified => write!(f, "unified"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ComputationBudgetContract {
    pub name: String,
    pub rewrite_limit: usize,
    pub traversal_limit: usize,
    pub recursion_limit: usize,
    pub total_limit: usize,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BudgetUseReport {
    pub domain: BudgetDomain,
    pub subject: String,
    pub used: usize,
    pub limit: usize,
    pub status: TerminationBudgetStatus,
    pub fingerprint: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TerminationBudgetReport {
    pub name: String,
    pub status: TerminationBudgetStatus,
    pub rewrite_used: usize,
    pub rewrite_limit: usize,
    pub traversal_used: usize,
    pub traversal_limit: usize,
    pub recursion_used: usize,
    pub recursion_limit: usize,
    pub total_used: usize,
    pub total_limit: usize,
    pub budget_uses: Vec<BudgetUseReport>,
    pub open_obligations: Vec<String>,
    pub max_trust: TrustLevel,
    pub max_provenance: Provenance,
    pub has_axiom_taint: bool,
    pub has_oracle_taint: bool,
    pub has_unsafe_taint: bool,
    pub fingerprint: String,
}

pub fn computation_budget_contract(
    name: impl Into<String>,
    rewrite_limit: usize,
    traversal_limit: usize,
    recursion_limit: usize,
    total_limit: usize,
    line: usize,
) -> Result<ComputationBudgetContract, Diagnostic> {
    let name = name.into();
    validate_identifier(&name, "computation budget name", line)?;
    if total_limit == 0 {
        return Err(budget_error(
            line,
            "computation budget total_limit must be positive",
            "DLM does not admit unbounded normalization/traversal/recursion through a zero or implicit budget",
        ));
    }
    if rewrite_limit.saturating_add(traversal_limit).saturating_add(recursion_limit) > total_limit {
        return Err(budget_error(
            line,
            "domain budget limits exceed total unified limit",
            "raise total_limit or lower rewrite/traversal/recursion domain limits so the budget contract is coherent",
        ));
    }
    let fingerprint = fingerprint(&[
        "computation_budget_contract",
        &name,
        &rewrite_limit.to_string(),
        &traversal_limit.to_string(),
        &recursion_limit.to_string(),
        &total_limit.to_string(),
    ]);
    Ok(ComputationBudgetContract {
        name,
        rewrite_limit,
        traversal_limit,
        recursion_limit,
        total_limit,
        fingerprint,
    })
}

#[allow(clippy::too_many_arguments)]
pub fn unify_termination_budget(
    contract: &ComputationBudgetContract,
    rewrite_reports: &[&RewriteNormalizationReport],
    map_reports: &[&MapTraversalReport],
    fold_reports: &[&FoldTraversalReport],
    recursion_schemes: &[&RecursionSchemeReport],
    recursive_calls: &[&RecursiveCallReport],
    line: usize,
) -> Result<TerminationBudgetReport, Diagnostic> {
    validate_identifier(&contract.name, "computation budget name", line)?;

    let rewrite_used: usize = rewrite_reports.iter().map(|r| r.step_count()).sum();
    let traversal_used: usize = map_reports.iter().map(|r| r.len).sum::<usize>()
        + fold_reports.iter().map(|r| r.len).sum::<usize>();
    let recursion_used: usize = recursive_calls
        .iter()
        .map(|c| c.fuel_before.saturating_sub(c.fuel_after))
        .sum();
    let total_used = rewrite_used
        .saturating_add(traversal_used)
        .saturating_add(recursion_used);

    let mut status = TerminationBudgetStatus::VerifiedUnified;
    let mut obligations = Vec::new();
    let mut uses = Vec::new();

    push_use(
        &mut uses,
        BudgetDomain::RewriteNormalization,
        "rewrite_normalization",
        rewrite_used,
        contract.rewrite_limit,
    );
    push_use(
        &mut uses,
        BudgetDomain::Traversal,
        "traversal",
        traversal_used,
        contract.traversal_limit,
    );
    push_use(
        &mut uses,
        BudgetDomain::Recursion,
        "recursion",
        recursion_used,
        contract.recursion_limit,
    );
    push_use(
        &mut uses,
        BudgetDomain::Unified,
        &contract.name,
        total_used,
        contract.total_limit,
    );

    if rewrite_used > contract.rewrite_limit {
        status = TerminationBudgetStatus::RejectedBudgetExceeded;
        obligations.push(format!(
            "rewrite normalization used {rewrite_used} steps but budget limit is {}",
            contract.rewrite_limit
        ));
    }
    if traversal_used > contract.traversal_limit {
        status = TerminationBudgetStatus::RejectedBudgetExceeded;
        obligations.push(format!(
            "traversal used {traversal_used} collection steps but budget limit is {}",
            contract.traversal_limit
        ));
    }
    if recursion_used > contract.recursion_limit {
        status = TerminationBudgetStatus::RejectedBudgetExceeded;
        obligations.push(format!(
            "recursion used {recursion_used} recursive calls but budget limit is {}",
            contract.recursion_limit
        ));
    }
    if total_used > contract.total_limit {
        status = TerminationBudgetStatus::RejectedBudgetExceeded;
        obligations.push(format!(
            "unified computation used {total_used} steps but total budget limit is {}",
            contract.total_limit
        ));
    }

    for report in rewrite_reports {
        if report.step_count() > report.max_steps {
            status = TerminationBudgetStatus::RejectedInconsistent;
            obligations.push(format!(
                "rewrite report for `{}` has more steps than its own max_steps",
                report.input
            ));
        }
        if report.step_count() == report.max_steps && report.max_steps != 0 {
            status = status.max(TerminationBudgetStatus::Open);
            obligations.push(format!(
                "rewrite report for `{}` exactly consumed max_steps {}; no slack remains",
                report.input, report.max_steps
            ));
        }
    }

    for report in map_reports {
        status = status.max(status_from_traversal(report.status));
        if report.fuel < report.len {
            status = TerminationBudgetStatus::RejectedBudgetExceeded;
            obligations.push(format!(
                "map traversal `{}` has fuel {} for length {}",
                report.function_contract, report.fuel, report.len
            ));
        }
    }
    for report in fold_reports {
        status = status.max(status_from_traversal(report.status));
        if report.fuel < report.len {
            status = TerminationBudgetStatus::RejectedBudgetExceeded;
            obligations.push(format!(
                "fold traversal `{}` has fuel {} for length {}",
                report.step_contract, report.fuel, report.len
            ));
        }
    }

    for scheme in recursion_schemes {
        status = status.max(status_from_recursion(scheme.status));
        if scheme.initial_fuel == 0 {
            status = TerminationBudgetStatus::RejectedBudgetExceeded;
            obligations.push(format!("recursion scheme `{}` has zero initial fuel", scheme.name));
        }
    }
    for call in recursive_calls {
        status = status.max(status_from_recursion(call.status));
        if call.fuel_before == 0 || call.fuel_after >= call.fuel_before {
            status = TerminationBudgetStatus::RejectedBudgetExceeded;
            obligations.push(format!(
                "recursive call `{}` does not consume positive fuel",
                call.scheme
            ));
        }
    }

    let (max_trust, max_provenance, has_axiom_taint, has_oracle_taint, has_unsafe_taint) =
        collect_taint(rewrite_reports, map_reports, fold_reports, recursion_schemes, recursive_calls);
    if has_axiom_taint || has_oracle_taint || has_unsafe_taint {
        status = status.max(TerminationBudgetStatus::Downgraded);
        obligations.push("unified budget preserves Axiom/Oracle/Unsafe taint and cannot be a clean verified budget".to_string());
    }

    obligations.sort();
    obligations.dedup();

    let fingerprint = fingerprint(&[
        "termination_budget_report",
        &contract.name,
        &rewrite_used.to_string(),
        &contract.rewrite_limit.to_string(),
        &traversal_used.to_string(),
        &contract.traversal_limit.to_string(),
        &recursion_used.to_string(),
        &contract.recursion_limit.to_string(),
        &total_used.to_string(),
        &contract.total_limit.to_string(),
        &status.to_string(),
        &format!("{:?}", max_trust),
    ]);

    Ok(TerminationBudgetReport {
        name: contract.name.clone(),
        status,
        rewrite_used,
        rewrite_limit: contract.rewrite_limit,
        traversal_used,
        traversal_limit: contract.traversal_limit,
        recursion_used,
        recursion_limit: contract.recursion_limit,
        total_used,
        total_limit: contract.total_limit,
        budget_uses: uses,
        open_obligations: obligations,
        max_trust,
        max_provenance,
        has_axiom_taint,
        has_oracle_taint,
        has_unsafe_taint,
        fingerprint,
    })
}

pub fn require_verified_unified_budget(report: &TerminationBudgetReport, line: usize) -> Result<(), Diagnostic> {
    if report.status == TerminationBudgetStatus::VerifiedUnified {
        Ok(())
    } else {
        Err(budget_error(
            line,
            format!("termination budget is {}, not verified_unified", report.status),
            "only verified_unified budgets may be used as optimizer/kernel assumptions for bounded computation",
        ))
    }
}

pub fn computation_budget_passport(theory: &str, contract: &ComputationBudgetContract) -> Passport {
    Passport {
        ty: TypeKind::ComputationBudget {
            name: contract.name.clone(),
            status: "declared".to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: budget_caps(),
        cost: CostClass::SmallFinite,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalDerived,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("termination_budget:contract:{}", contract.name)),
        location: LocationContext::local(),
    }
}

pub fn termination_budget_report_passport(
    theory: &str,
    report: &TerminationBudgetReport,
    sources: &[&Passport],
) -> Passport {
    Passport {
        ty: TypeKind::TerminationBudgetReport {
            subject: report.name.clone(),
            status: report.status.to_string(),
        },
        construction: ConstructionMode::Definable,
        capabilities: budget_caps(),
        cost: CostClass::SmallFinite,
        trust: report.max_trust.max(taint_summary(sources).0),
        provenance: report.max_provenance.max(taint_summary(sources).1),
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::merge_many(sources.iter().map(|p| &p.history), "termination_budget:report"),
        location: LocationContext::local(),
    }
}

pub fn export_termination_budget_report(report: &TerminationBudgetReport) -> String {
    let mut out = String::new();
    out.push_str("termination_budget_report: v1\n");
    out.push_str(&format!("name: {}\n", report.name));
    out.push_str(&format!("status: {}\n", report.status));
    out.push_str(&format!("rewrite_used: {}\n", report.rewrite_used));
    out.push_str(&format!("rewrite_limit: {}\n", report.rewrite_limit));
    out.push_str(&format!("traversal_used: {}\n", report.traversal_used));
    out.push_str(&format!("traversal_limit: {}\n", report.traversal_limit));
    out.push_str(&format!("recursion_used: {}\n", report.recursion_used));
    out.push_str(&format!("recursion_limit: {}\n", report.recursion_limit));
    out.push_str(&format!("total_used: {}\n", report.total_used));
    out.push_str(&format!("total_limit: {}\n", report.total_limit));
    out.push_str("budget_uses:\n");
    for usage in &report.budget_uses {
        out.push_str(&format!(
            "  - {}:{} used={} limit={} status={} fingerprint={}\n",
            usage.domain, usage.subject, usage.used, usage.limit, usage.status, usage.fingerprint
        ));
    }
    out.push_str("open_obligations:\n");
    for obligation in &report.open_obligations {
        out.push_str(&format!("  - {}\n", obligation));
    }
    out.push_str(&format!("max_trust: {:?}\n", report.max_trust));
    out.push_str(&format!("max_provenance: {:?}\n", report.max_provenance));
    out.push_str(&format!("has_axiom_taint: {}\n", report.has_axiom_taint));
    out.push_str(&format!("has_oracle_taint: {}\n", report.has_oracle_taint));
    out.push_str(&format!("has_unsafe_taint: {}\n", report.has_unsafe_taint));
    out.push_str(&format!("fingerprint: {}\n", report.fingerprint));
    out
}

fn push_use(uses: &mut Vec<BudgetUseReport>, domain: BudgetDomain, subject: &str, used: usize, limit: usize) {
    let status = if used > limit {
        TerminationBudgetStatus::RejectedBudgetExceeded
    } else {
        TerminationBudgetStatus::VerifiedUnified
    };
    let fingerprint = fingerprint(&[
        "budget_use",
        &domain.to_string(),
        subject,
        &used.to_string(),
        &limit.to_string(),
        &status.to_string(),
    ]);
    uses.push(BudgetUseReport {
        domain,
        subject: subject.to_string(),
        used,
        limit,
        status,
        fingerprint,
    });
}

fn status_from_traversal(status: TraversalStatus) -> TerminationBudgetStatus {
    match status {
        TraversalStatus::VerifiedBounded => TerminationBudgetStatus::VerifiedUnified,
        TraversalStatus::Downgraded => TerminationBudgetStatus::Downgraded,
        TraversalStatus::Open => TerminationBudgetStatus::Open,
        TraversalStatus::RejectedFuelExceeded | TraversalStatus::RejectedContract => {
            TerminationBudgetStatus::RejectedBudgetExceeded
        }
    }
}

fn status_from_recursion(status: RecursionStatus) -> TerminationBudgetStatus {
    match status {
        RecursionStatus::VerifiedWellFounded => TerminationBudgetStatus::VerifiedUnified,
        RecursionStatus::Downgraded => TerminationBudgetStatus::Downgraded,
        RecursionStatus::Open => TerminationBudgetStatus::Open,
        RecursionStatus::RejectedFuelExceeded
        | RecursionStatus::RejectedMeasure
        | RecursionStatus::RejectedContract => TerminationBudgetStatus::RejectedBudgetExceeded,
    }
}

#[allow(clippy::type_complexity)]
fn collect_taint(
    rewrite_reports: &[&RewriteNormalizationReport],
    map_reports: &[&MapTraversalReport],
    fold_reports: &[&FoldTraversalReport],
    recursion_schemes: &[&RecursionSchemeReport],
    recursive_calls: &[&RecursiveCallReport],
) -> (TrustLevel, Provenance, bool, bool, bool) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    let mut has_axiom = false;
    let mut has_oracle = false;
    let mut has_unsafe = false;

    for report in rewrite_reports {
        max_trust = max_trust.max(report.certificate.trust).max(report.trace.trust);
        max_provenance = max_provenance.max(report.certificate.provenance).max(report.trace.provenance);
        has_axiom |= report.is_axiom_tainted();
        has_oracle |= report.certificate.trust >= TrustLevel::Oracle || report.trace.trust >= TrustLevel::Oracle;
        has_unsafe |= report.certificate.trust >= TrustLevel::Unsafe || report.trace.trust >= TrustLevel::Unsafe;
    }
    for report in map_reports {
        max_trust = max_trust.max(report.max_trust);
        max_provenance = max_provenance.max(report.max_provenance);
        has_axiom |= report.has_axiom_taint;
        has_oracle |= report.has_oracle_taint;
        has_unsafe |= report.has_unsafe_taint;
    }
    for report in fold_reports {
        max_trust = max_trust.max(report.max_trust);
        max_provenance = max_provenance.max(report.max_provenance);
        has_axiom |= report.has_axiom_taint;
        has_oracle |= report.has_oracle_taint;
        has_unsafe |= report.has_unsafe_taint;
    }
    for report in recursion_schemes {
        max_trust = max_trust.max(report.max_trust);
        max_provenance = max_provenance.max(report.max_provenance);
        has_axiom |= report.has_axiom_taint;
        has_oracle |= report.has_oracle_taint;
        has_unsafe |= report.has_unsafe_taint;
    }
    for report in recursive_calls {
        max_trust = max_trust.max(report.max_trust);
        max_provenance = max_provenance.max(report.max_provenance);
        has_axiom |= report.has_axiom_taint;
        has_oracle |= report.has_oracle_taint;
        has_unsafe |= report.has_unsafe_taint;
    }
    (max_trust, max_provenance, has_axiom, has_oracle, has_unsafe)
}

fn taint_summary(sources: &[&Passport]) -> (TrustLevel, Provenance) {
    let mut max_trust = TrustLevel::Checked;
    let mut max_provenance = Provenance::InternalDerived;
    for source in sources {
        max_trust = max_trust.max(source.trust);
        max_provenance = max_provenance.max(source.provenance);
    }
    (max_trust, max_provenance)
}

fn budget_caps() -> CapabilitySet {
    CapabilitySet::from([Capability::CanSymbolicPrint, Capability::CanSerializeForMigration])
}

fn validate_identifier(value: &str, label: &str, line: usize) -> Result<(), Diagnostic> {
    if value.trim().is_empty() {
        return Err(budget_error(
            line,
            format!("{label} must not be empty"),
            "give every computation budget a stable audit identifier",
        ));
    }
    if value.chars().any(char::is_whitespace) {
        return Err(budget_error(
            line,
            format!("{label} must not contain whitespace: {value}"),
            "use a stable identifier such as bounded_nat_eval or list_fold_budget",
        ));
    }
    Ok(())
}

fn budget_error(line: usize, message: impl Into<String>, help: impl Into<String>) -> Diagnostic {
    Diagnostic::error(DiagnosticKind::TerminationBudgetError, Some(line), message.into()).with_help(help.into())
}

fn fingerprint(parts: &[&str]) -> String {
    let mut acc: u64 = 0xcbf29ce484222325;
    for part in parts {
        for byte in part.as_bytes() {
            acc ^= u64::from(*byte);
            acc = acc.wrapping_mul(0x100000001b3);
        }
        acc ^= 0xff;
        acc = acc.wrapping_mul(0x100000001b3);
    }
    format!("tb-{acc:016x}")
}
