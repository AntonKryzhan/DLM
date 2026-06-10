use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{Passport, TrustLevel};

/// Checker trust policy.
///
/// The ordering of `TrustLevel` is intentionally monotone:
/// `Checked < Builtin < Axiom < Oracle < Unsafe`.
/// A normal computation may only keep or raise taint, never silently lower it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CheckPolicy {
    pub max_trust: TrustLevel,
}

impl CheckPolicy {
    pub fn research() -> Self {
        Self { max_trust: TrustLevel::Axiom }
    }

    pub fn trusted_only() -> Self {
        Self { max_trust: TrustLevel::Builtin }
    }

    pub fn allow_unsafe() -> Self {
        Self { max_trust: TrustLevel::Unsafe }
    }
}

impl Default for CheckPolicy {
    fn default() -> Self {
        Self::research()
    }
}

pub fn join_trust(lhs: TrustLevel, rhs: TrustLevel) -> TrustLevel {
    lhs.max(rhs)
}

pub fn join_many_trust(values: impl IntoIterator<Item = TrustLevel>) -> TrustLevel {
    values.into_iter().fold(TrustLevel::Checked, |acc, trust| acc.max(trust))
}

pub fn is_allowed_by_policy(policy: CheckPolicy, trust: TrustLevel) -> bool {
    trust <= policy.max_trust
}

pub fn validate_policy(passport: &Passport, policy: CheckPolicy, line: usize) -> Result<(), Diagnostic> {
    if is_allowed_by_policy(policy, passport.trust) {
        return Ok(());
    }

    Err(Diagnostic::error(
        DiagnosticKind::TrustTaintError,
        Some(line),
        format!(
            "value trust level {:?} exceeds current policy {:?}",
            passport.trust, policy.max_trust
        ),
    ).with_help(format!(
        "value passport: {passport}\n  use --allow-unsafe for unsafe prototypes, or avoid Axiom/Unsafe sources for --trusted-only checks"
    )))
}

pub fn history_requires_at_least_axiom(event: &str) -> bool {
    event.contains("axiom:")
        || event.starts_with("truth:from_provable_axiom")
        || event.starts_with("consistency:axiom:")
        || event.starts_with("reflection:axiom:")
        || event.starts_with("self_reference:axiom:")
        || event.starts_with("bridge:soundness:")
}

pub fn history_requires_unsafe(event: &str) -> bool {
    event.contains("unsafe:") || event.starts_with("bridge:unsafe:")
}
