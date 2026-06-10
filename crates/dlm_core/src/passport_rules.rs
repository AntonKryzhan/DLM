use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{Capability, Passport, TrustLevel};

/// Small rule helpers for passport checks.
///
/// v0.32 does not replace the full checker yet. It starts extracting the
/// safety-sensitive rules into small pure functions so future passes can call
/// the same logic instead of duplicating ad-hoc checks.
pub fn require_capability(passport: &Passport, capability: Capability, line: usize, reason: &str) -> Result<(), Diagnostic> {
    if passport.capabilities.contains(capability) {
        Ok(())
    } else {
        Err(Diagnostic::error(
            DiagnosticKind::AccessError,
            Some(line),
            reason,
        ).with_help(format!(
            "value passport: {passport}\n  missing capability: {:?}",
            capability
        )))
    }
}

pub fn trust_join_from_sources(lhs: &Passport, rhs: &Passport) -> TrustLevel {
    lhs.trust.max(rhs.trust)
}

pub fn passport_is_axiom_or_worse(passport: &Passport) -> bool {
    passport.trust >= TrustLevel::Axiom
}

pub fn passport_is_unsafe(passport: &Passport) -> bool {
    passport.trust == TrustLevel::Unsafe
}

pub fn history_contains_ordered_subsequence(passport: &Passport, needles: &[&str]) -> bool {
    if needles.is_empty() {
        return true;
    }

    let mut next = 0usize;
    for event in passport.history.events() {
        if event.contains(needles[next]) {
            next += 1;
            if next == needles.len() {
                return true;
            }
        }
    }
    false
}
