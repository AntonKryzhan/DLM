use std::fmt;

use crate::ast::{BridgeDecl, BridgeKind};
use crate::checker::CheckReport;
use crate::passport::{Passport, Provenance, TrustLevel, TypeKind};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SoundnessIssue {
    pub subject: String,
    pub message: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BridgeSoundnessProfile {
    pub name: String,
    pub source: String,
    pub target: String,
    pub kind: String,
    pub preserves_syntax: bool,
    pub preserves_value: bool,
    pub preserves_proof: bool,
    pub preserves_truth: bool,
    pub requires_axiom: bool,
    pub is_conservative: bool,
    pub is_reflective: bool,
    pub is_reversible: bool,
    pub taint: TrustLevel,
    pub role: &'static str,
}

impl BridgeSoundnessProfile {
    pub fn from_decl(bridge: &BridgeDecl) -> Self {
        let (preserves_syntax, preserves_value, preserves_proof, preserves_truth, requires_axiom, is_conservative, is_reflective, is_reversible, taint, role) = match &bridge.kind {
            BridgeKind::Definitional => (
                true, true, true, true, false, true, false, true, TrustLevel::Builtin,
                "definitional conservative extension: syntax/value/proof/truth are preserved by definition",
            ),
            BridgeKind::Conservative => (
                false, true, true, true, false, true, false, false, TrustLevel::Builtin,
                "conservative extension: old-theory truth is preserved without adding old-language theorems",
            ),
            BridgeKind::Quote => (
                true, false, false, false, false, false, false, false, TrustLevel::Builtin,
                "syntax-only bridge: object becomes Term; value/proof/truth are not transported",
            ),
            BridgeKind::Transport => (
                false, true, false, false, false, false, false, false, TrustLevel::Builtin,
                "value transport bridge: value role is moved, but proof/truth are not implicitly preserved",
            ),
            BridgeKind::Soundness => (
                false, false, true, true, true, false, false, false, TrustLevel::Axiom,
                "axiom-tainted truth bridge: provability/trusted proof is lifted toward truth via soundness assumption",
            ),
            BridgeKind::Reflection => (
                true, false, true, false, true, false, true, false, TrustLevel::Axiom,
                "reflective bridge: metatheoretic self-reference; requires explicit reflective/soundness controls",
            ),
            BridgeKind::Migration => (
                false, true, false, false, false, false, false, false, TrustLevel::Builtin,
                "runtime migration bridge: moves passported value/state across location/architecture boundaries",
            ),
            BridgeKind::Materialize => (
                false, true, false, false, false, false, false, false, TrustLevel::Builtin,
                "materialization bridge: remote value is explicitly re-entered into local value space",
            ),
            BridgeKind::Unsafe => (
                false, true, false, false, true, false, false, false, TrustLevel::Unsafe,
                "unsafe bridge: may change meaning/capabilities; result must remain Unsafe-tainted",
            ),
            BridgeKind::Unknown(_) => (
                false, false, false, false, true, false, false, false, TrustLevel::Unsafe,
                "unknown bridge kind: no preservation law is known; treated as unsafe for metatheory",
            ),
        };

        Self {
            name: bridge.name.clone(),
            source: bridge.source.clone(),
            target: bridge.target.clone(),
            kind: bridge.kind.as_str().to_string(),
            preserves_syntax,
            preserves_value,
            preserves_proof,
            preserves_truth,
            requires_axiom,
            is_conservative,
            is_reflective,
            is_reversible,
            taint,
            role,
        }
    }

    fn render_flags(&self) -> String {
        format!(
            "preserves=[syntax:{}, value:{}, proof:{}, truth:{}], conservative={}, reflective={}, reversible={}, requires_axiom={}, taint={:?}",
            self.preserves_syntax,
            self.preserves_value,
            self.preserves_proof,
            self.preserves_truth,
            self.is_conservative,
            self.is_reflective,
            self.is_reversible,
            self.requires_axiom,
            self.taint,
        )
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SoundnessSummary {
    pub module_name: String,
    pub values_checked: usize,
    pub static_proofs: usize,
    pub kernel_checked_proofs: usize,
    pub proof_terms: usize,
    pub propositions: usize,
    pub provability_claims: usize,
    pub truth_axiom_lifts: usize,
    pub runtime_witnesses: usize,
    pub runtime_input_values: usize,
    pub axiom_tainted: usize,
    pub unsafe_tainted: usize,
    pub oracle_tainted: usize,
    pub quote_bridge_events: usize,
    pub transport_bridge_events: usize,
    pub soundness_bridge_events: usize,
    pub migration_events: usize,
    pub materialize_events: usize,
    pub gpu_events: usize,
    pub bridge_declarations: usize,
    pub definitional_bridge_declarations: usize,
    pub conservative_bridge_declarations: usize,
    pub quote_bridge_declarations: usize,
    pub transport_bridge_declarations: usize,
    pub soundness_bridge_declarations: usize,
    pub reflection_bridge_declarations: usize,
    pub migration_bridge_declarations: usize,
    pub materialize_bridge_declarations: usize,
    pub unsafe_bridge_declarations: usize,
    pub unknown_bridge_declarations: usize,
    pub bridge_profiles: Vec<BridgeSoundnessProfile>,
    pub issues: Vec<SoundnessIssue>,
}

impl SoundnessSummary {
    pub fn from_report(report: &CheckReport) -> Self {
        let mut summary = Self {
            module_name: report.module_name.clone(),
            values_checked: report.inferred.len(),
            ..Self::default()
        };

        for bridge in &report.bridges {
            summary.observe_bridge(bridge);
        }

        for (name, passport) in &report.inferred {
            summary.observe(name, passport);
        }

        summary
    }

    fn observe_bridge(&mut self, bridge: &BridgeDecl) {
        self.bridge_declarations += 1;
        match &bridge.kind {
            BridgeKind::Definitional => self.definitional_bridge_declarations += 1,
            BridgeKind::Conservative => self.conservative_bridge_declarations += 1,
            BridgeKind::Quote => self.quote_bridge_declarations += 1,
            BridgeKind::Transport => self.transport_bridge_declarations += 1,
            BridgeKind::Soundness => self.soundness_bridge_declarations += 1,
            BridgeKind::Reflection => self.reflection_bridge_declarations += 1,
            BridgeKind::Migration => self.migration_bridge_declarations += 1,
            BridgeKind::Materialize => self.materialize_bridge_declarations += 1,
            BridgeKind::Unsafe => self.unsafe_bridge_declarations += 1,
            BridgeKind::Unknown(_) => self.unknown_bridge_declarations += 1,
        }

        let profile = BridgeSoundnessProfile::from_decl(bridge);
        if matches!(&bridge.kind, BridgeKind::Unsafe | BridgeKind::Unknown(_)) {
            self.issues.push(SoundnessIssue {
                subject: format!("bridge {}", bridge.name),
                message: format!("{} bridge has no safe preservation law", profile.kind),
            });
        }
        self.bridge_profiles.push(profile);
    }

    fn observe(&mut self, name: &str, passport: &Passport) {
        match &passport.ty {
            TypeKind::StaticProof(predicate) => {
                self.static_proofs += 1;
                if predicate.starts_with("kernel_checked:") {
                    self.kernel_checked_proofs += 1;
                }
            }
            TypeKind::ProofTerm { .. } => self.proof_terms += 1,
            TypeKind::Prop { .. } => self.propositions += 1,
            TypeKind::Provable { .. } => self.provability_claims += 1,
            TypeKind::RuntimeWitness(_) => self.runtime_witnesses += 1,
            _ => {}
        }

        if passport.provenance == Provenance::RuntimeInput {
            self.runtime_input_values += 1;
        }

        match passport.trust {
            TrustLevel::Axiom => self.axiom_tainted += 1,
            TrustLevel::Unsafe => self.unsafe_tainted += 1,
            TrustLevel::Oracle => self.oracle_tainted += 1,
            TrustLevel::Checked | TrustLevel::Builtin => {}
        }

        for event in passport.history.events() {
            if event.starts_with("bridge:quote:") {
                self.quote_bridge_events += 1;
            } else if event.starts_with("bridge:transport:") {
                self.transport_bridge_events += 1;
            } else if event.starts_with("bridge:soundness:") {
                self.soundness_bridge_events += 1;
            } else if event.starts_with("truth:from_provable_axiom") {
                self.truth_axiom_lifts += 1;
            } else if event.starts_with("migration:") || event.starts_with("cluster:schedule:") {
                self.migration_events += 1;
            } else if event.starts_with("remote:materialize:") {
                self.materialize_events += 1;
            }

            if event.starts_with("gpu:")
                || event.starts_with("gpu_pool:")
                || event.starts_with("gpu_memory:")
                || event.starts_with("gpu_kernel:")
                || event.starts_with("copy:cpu_to_gpu")
                || event.starts_with("copy:gpu_to_cpu")
            {
                self.gpu_events += 1;
            }
        }

        self.check_invariants(name, passport);
    }

    fn check_invariants(&mut self, name: &str, passport: &Passport) {
        if passport.history.contains_event("unsafe:") && passport.trust != TrustLevel::Unsafe {
            self.issues.push(SoundnessIssue {
                subject: name.to_string(),
                message: "history contains unsafe event but trust is not Unsafe".to_string(),
            });
        }

        if passport.history.contains_event("axiom:")
            && passport.trust != TrustLevel::Axiom
            && passport.trust != TrustLevel::Oracle
            && passport.trust != TrustLevel::Unsafe
        {
            self.issues.push(SoundnessIssue {
                subject: name.to_string(),
                message: "history contains axiom event but trust is below Axiom".to_string(),
            });
        }

        // Bridge events can be inherited by derived values.
        //
        // Example:
        //   quote(PA.n)        -> Term<PA.Nat>, history ends with bridge:quote:...
        //   inspect_ast(code)  -> Text, history still contains bridge:quote:...
        //
        // The invariant below must therefore inspect the direct producer event
        // rather than the full inherited history. Otherwise any value derived
        // from a quoted Term would be incorrectly reported as unsound.
        let last_event = passport.history.events().last().map(String::as_str);

        if last_event.is_some_and(|event| event.starts_with("bridge:quote:"))
            && !matches!(passport.ty, TypeKind::Term { .. })
        {
            self.issues.push(SoundnessIssue {
                subject: name.to_string(),
                message: "direct quote bridge event produced a non-Term passport".to_string(),
            });
        }

        if last_event.is_some_and(|event| event.starts_with("bridge:soundness:"))
            && !matches!(passport.ty, TypeKind::StaticProof(_))
        {
            self.issues.push(SoundnessIssue {
                subject: name.to_string(),
                message: "direct soundness bridge event produced a non-StaticProof passport".to_string(),
            });
        }

        if matches!(passport.ty, TypeKind::StaticProof(_)) && passport.provenance == Provenance::RuntimeInput {
            self.issues.push(SoundnessIssue {
                subject: name.to_string(),
                message: "StaticProof has RuntimeInput provenance".to_string(),
            });
        }
    }

    pub fn is_clean(&self) -> bool {
        self.axiom_tainted == 0
            && self.unsafe_tainted == 0
            && self.oracle_tainted == 0
            && self.issues.is_empty()
    }

    pub fn render_human(&self) -> String {
        let mut out = String::new();
        out.push_str("Soundness summary:\n");
        out.push_str(&format!("  module: {}\n", self.module_name));
        out.push_str(&format!("  values checked: {}\n", self.values_checked));
        out.push_str(&format!("  static proofs: {}\n", self.static_proofs));
        out.push_str(&format!("  proof terms: {}\n", self.proof_terms));
        out.push_str(&format!("  propositions: {}\n", self.propositions));
        out.push_str(&format!("  provability claims: {}\n", self.provability_claims));
        out.push_str(&format!("  axiom truth lifts from provability: {}\n", self.truth_axiom_lifts));
        out.push_str(&format!("  kernel-checked proofs: {}\n", self.kernel_checked_proofs));
        out.push_str(&format!("  runtime witnesses: {}\n", self.runtime_witnesses));
        out.push_str(&format!("  runtime-input values: {}\n", self.runtime_input_values));
        out.push_str(&format!("  axiom-tainted values: {}\n", self.axiom_tainted));
        out.push_str(&format!("  oracle-tainted values: {}\n", self.oracle_tainted));
        out.push_str(&format!("  unsafe-tainted values: {}\n", self.unsafe_tainted));
        out.push_str("\nBridge/history events:\n");
        out.push_str(&format!("  quote bridge events: {}\n", self.quote_bridge_events));
        out.push_str(&format!("  transport bridge events: {}\n", self.transport_bridge_events));
        out.push_str(&format!("  soundness bridge events: {}\n", self.soundness_bridge_events));
        out.push_str(&format!("  migration/schedule events: {}\n", self.migration_events));
        out.push_str(&format!("  materialize events: {}\n", self.materialize_events));
        out.push_str(&format!("  gpu-related events: {}\n", self.gpu_events));
        out.push_str("\nBridge declarations:\n");
        out.push_str(&format!("  total: {}\n", self.bridge_declarations));
        out.push_str(&format!("  definitional: {}\n", self.definitional_bridge_declarations));
        out.push_str(&format!("  conservative: {}\n", self.conservative_bridge_declarations));
        out.push_str(&format!("  quote: {}\n", self.quote_bridge_declarations));
        out.push_str(&format!("  transport: {}\n", self.transport_bridge_declarations));
        out.push_str(&format!("  soundness: {}\n", self.soundness_bridge_declarations));
        out.push_str(&format!("  reflection: {}\n", self.reflection_bridge_declarations));
        out.push_str(&format!("  migration: {}\n", self.migration_bridge_declarations));
        out.push_str(&format!("  materialize: {}\n", self.materialize_bridge_declarations));
        out.push_str(&format!("  unsafe: {}\n", self.unsafe_bridge_declarations));
        out.push_str(&format!("  unknown: {}\n", self.unknown_bridge_declarations));
        if !self.bridge_profiles.is_empty() {
            out.push_str("\nBridge soundness profiles:\n");
            for profile in &self.bridge_profiles {
                out.push_str(&format!(
                    "  {}: {} -> {} kind={}\n    {}\n    role: {}\n",
                    profile.name,
                    profile.source,
                    profile.target,
                    profile.kind,
                    profile.render_flags(),
                    profile.role,
                ));
            }
        }
        out.push_str("\nGuarantee:\n");
        if self.is_clean() {
            out.push_str("  clean under current passport soundness checks: no Axiom/Oracle/Unsafe taint detected.\n");
        } else {
            out.push_str("  not clean: result depends on Axiom/Oracle/Unsafe taint or invariant issues.\n");
        }
        if !self.issues.is_empty() {
            out.push_str("\nInvariant issues:\n");
            for issue in &self.issues {
                out.push_str(&format!("  {}: {}\n", issue.subject, issue.message));
            }
        }
        out
    }
}

impl fmt::Display for SoundnessSummary {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.render_human())
    }
}
