use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::equality::{apply_rewrite_rule, require_rewrite_rule, rewrite_certificate_passport, RewriteDirection, RewriteStep, RewriteTrace};
use crate::passport::{HistoryChain, Passport, Provenance, TrustLevel, TypeKind};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RewriteNormalizationStatus {
    AlreadyNormal,
    Normalized,
}

impl RewriteNormalizationStatus {
    pub fn is_normal(self) -> bool {
        matches!(
            self,
            RewriteNormalizationStatus::AlreadyNormal | RewriteNormalizationStatus::Normalized
        )
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RewriteNormalizationReport {
    pub status: RewriteNormalizationStatus,
    pub theory: String,
    pub input: String,
    pub normal_form: String,
    pub max_steps: usize,
    pub trace: RewriteTrace,
    pub certificate: Passport,
}

impl RewriteNormalizationReport {
    pub fn is_axiom_tainted(&self) -> bool {
        self.trace.is_axiom_tainted() || self.certificate.trust >= TrustLevel::Axiom
    }

    pub fn step_count(&self) -> usize {
        self.trace.steps.len()
    }

    pub fn is_already_normal(&self) -> bool {
        self.status == RewriteNormalizationStatus::AlreadyNormal
    }
}

pub fn normalize_with_rewrite_rules(
    theory: &str,
    input: impl Into<String>,
    rules: &[Passport],
    max_steps: usize,
    line: usize,
) -> Result<RewriteNormalizationReport, Diagnostic> {
    let input = require_non_empty(input.into(), "normalization input", line)?;
    validate_rewrite_rule_list(rules, line)?;

    let mut current = input.clone();
    let mut steps = Vec::new();
    let mut trust = TrustLevel::Builtin;
    let mut provenance = Provenance::BuiltinKnown;
    let mut rule_histories = Vec::new();

    loop {
        let next_rule = first_applicable_rule(rules, &current, line)?;
        let Some(rule) = next_rule else {
            break;
        };

        if steps.len() >= max_steps {
            return Err(Diagnostic::error(
                DiagnosticKind::RewriteNormalizationError,
                Some(line),
                format!(
                    "rewrite normalization exceeded max_steps={max_steps} before reaching a normal form"
                ),
            )
            .with_help(format!(
                "term `{current}` is still rewriteable; increase the bound or remove cyclic rewrite rules"
            )));
        }

        let step = apply_rewrite_rule(rule, current.clone(), RewriteDirection::Forward, line)?;
        current = step.to.clone();
        trust = trust.max(rule.trust);
        provenance = provenance.max(rule.provenance);
        rule_histories.push(rule.history.clone());
        steps.push(step);
    }

    let status = if steps.is_empty() {
        RewriteNormalizationStatus::AlreadyNormal
    } else {
        RewriteNormalizationStatus::Normalized
    };

    let trace = RewriteTrace {
        theory: theory.to_string(),
        from: input.clone(),
        to: current.clone(),
        steps,
        trust,
        provenance,
        rule_histories,
    };
    let certificate = rewrite_certificate_passport(theory, &trace);

    let report = RewriteNormalizationReport {
        status,
        theory: theory.to_string(),
        input,
        normal_form: current,
        max_steps,
        trace,
        certificate,
    };
    audit_rewrite_normalization_report(&report, line)?;
    Ok(report)
}

pub fn audit_rewrite_normalization_report(
    report: &RewriteNormalizationReport,
    line: usize,
) -> Result<(), Diagnostic> {
    if report.theory.trim().is_empty() {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization report theory cannot be empty",
        ));
    }

    if report.input != report.trace.from {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization report input does not match trace start",
        ));
    }

    if report.normal_form != report.trace.to {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization report normal form does not match trace end",
        ));
    }

    if report.trace.steps.len() > report.max_steps {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization report contains more steps than its declared bound",
        ));
    }

    if report.status == RewriteNormalizationStatus::AlreadyNormal && !report.trace.steps.is_empty() {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "AlreadyNormal report must have an empty rewrite trace",
        ));
    }

    if report.status == RewriteNormalizationStatus::Normalized && report.trace.steps.is_empty() {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "Normalized report must contain at least one rewrite step",
        ));
    }

    match &report.certificate.ty {
        TypeKind::RewriteCertificate { from, to }
            if from == &report.input && to == &report.normal_form => {}
        TypeKind::RewriteCertificate { .. } => {
            return Err(Diagnostic::error(
                DiagnosticKind::RewriteNormalizationError,
                Some(line),
                "rewrite normalization certificate endpoints do not match report endpoints",
            ));
        }
        other => {
            return Err(Diagnostic::error(
                DiagnosticKind::RewriteNormalizationError,
                Some(line),
                "rewrite normalization report must carry a RewriteCertificate passport",
            )
            .with_help(format!("received passport type: {other}")));
        }
    }

    if report.certificate.trust != report.trace.trust {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization certificate trust does not match trace trust",
        ));
    }

    if report.certificate.provenance != report.trace.provenance {
        return Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            "rewrite normalization certificate provenance does not match trace provenance",
        ));
    }

    Ok(())
}

pub fn export_rewrite_normalization_report(
    report: &RewriteNormalizationReport,
    line: usize,
) -> Result<String, Diagnostic> {
    audit_rewrite_normalization_report(report, line)?;
    Ok(export_rewrite_normalization_report_unchecked(report))
}

pub fn export_rewrite_normalization_report_unchecked(report: &RewriteNormalizationReport) -> String {
    let mut out = String::new();
    push_line(&mut out, "DLM-REWRITE-NORMALIZATION v1");
    push_field(&mut out, "status", &format!("{:?}", report.status));
    push_field(&mut out, "theory", &report.theory);
    push_field(&mut out, "input", &report.input);
    push_field(&mut out, "normal_form", &report.normal_form);
    push_field(&mut out, "max_steps", &report.max_steps.to_string());
    push_field(&mut out, "step_count", &report.step_count().to_string());
    push_field(&mut out, "trust", &format!("{:?}", report.trace.trust));
    push_field(&mut out, "provenance", &format!("{:?}", report.trace.provenance));
    push_field(&mut out, "axiom_tainted", &report.is_axiom_tainted().to_string());
    push_line(&mut out, "steps:");
    for (index, step) in report.trace.steps.iter().enumerate() {
        push_line(&mut out, &format!("  {index}: {}", render_rewrite_step(step)));
    }
    out
}

fn validate_rewrite_rule_list(rules: &[Passport], line: usize) -> Result<(), Diagnostic> {
    for (index, rule) in rules.iter().enumerate() {
        require_normalization_rule(rule, index, line)?;
    }
    Ok(())
}

fn first_applicable_rule<'a>(
    rules: &'a [Passport],
    current: &str,
    line: usize,
) -> Result<Option<&'a Passport>, Diagnostic> {
    for (index, rule) in rules.iter().enumerate() {
        let (_, lhs, _) = require_normalization_rule(rule, index, line)?;
        if lhs == current {
            return Ok(Some(rule));
        }
    }
    Ok(None)
}

fn require_normalization_rule(
    rule: &Passport,
    index: usize,
    line: usize,
) -> Result<(String, String, String), Diagnostic> {
    require_rewrite_rule(rule, line).map_err(|diagnostic| {
        Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            format!("normalization rule #{index} is not a valid RewriteRule"),
        )
        .with_help(diagnostic.message)
    })
}

fn require_non_empty(value: String, label: &str, line: usize) -> Result<String, Diagnostic> {
    if value.trim().is_empty() {
        Err(Diagnostic::error(
            DiagnosticKind::RewriteNormalizationError,
            Some(line),
            format!("{label} cannot be empty"),
        ))
    } else {
        Ok(value)
    }
}

fn render_rewrite_step(step: &RewriteStep) -> String {
    format!(
        "{}:{}:{}->{}",
        step.rule_name, step.direction, step.from, step.to
    )
}

fn push_field(out: &mut String, key: &str, value: &str) {
    push_line(out, &format!("{key}: {value}"));
}

fn push_line(out: &mut String, line: &str) {
    out.push_str(line);
    out.push('\n');
}

#[allow(dead_code)]
fn _keep_history_chain_public_shape(_: &HistoryChain) {}
