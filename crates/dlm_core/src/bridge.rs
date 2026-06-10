use crate::ast::{BridgeDecl, BridgeKind};
use crate::passport::TrustLevel;

/// Central bridge preservation profile.
///
/// v0.32 moves the bridge truth table out of ad-hoc checker/soundness logic.
/// Every bridge kind has exactly one preservation profile and every consumer
/// should read bridge semantics from this module.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BridgeProfile {
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

impl BridgeProfile {
    pub fn from_decl(bridge: &BridgeDecl) -> Self {
        let law = bridge_law(&bridge.kind);
        Self {
            name: bridge.name.clone(),
            source: bridge.source.clone(),
            target: bridge.target.clone(),
            kind: bridge.kind.as_str().to_string(),
            preserves_syntax: law.preserves_syntax,
            preserves_value: law.preserves_value,
            preserves_proof: law.preserves_proof,
            preserves_truth: law.preserves_truth,
            requires_axiom: law.requires_axiom,
            is_conservative: law.is_conservative,
            is_reflective: law.is_reflective,
            is_reversible: law.is_reversible,
            taint: law.taint,
            role: law.role,
        }
    }

    pub fn render_flags(&self) -> String {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BridgeLaw {
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

pub fn bridge_law(kind: &BridgeKind) -> BridgeLaw {
    match kind {
        BridgeKind::Definitional => BridgeLaw {
            preserves_syntax: true,
            preserves_value: true,
            preserves_proof: true,
            preserves_truth: true,
            requires_axiom: false,
            is_conservative: true,
            is_reflective: false,
            is_reversible: true,
            taint: TrustLevel::Builtin,
            role: "definitional conservative extension: syntax/value/proof/truth are preserved by definition",
        },
        BridgeKind::Conservative => BridgeLaw {
            preserves_syntax: false,
            preserves_value: true,
            preserves_proof: true,
            preserves_truth: true,
            requires_axiom: false,
            is_conservative: true,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Builtin,
            role: "conservative extension: old-theory truth is preserved without adding old-language theorems",
        },
        BridgeKind::Quote => BridgeLaw {
            preserves_syntax: true,
            preserves_value: false,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: false,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Builtin,
            role: "syntax-only bridge: object becomes Term; value/proof/truth are not transported",
        },
        BridgeKind::Transport => BridgeLaw {
            preserves_syntax: false,
            preserves_value: true,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: false,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Builtin,
            role: "value transport bridge: value role is moved, but proof/truth are not implicitly preserved",
        },
        BridgeKind::Soundness => BridgeLaw {
            preserves_syntax: false,
            preserves_value: false,
            preserves_proof: true,
            preserves_truth: true,
            requires_axiom: true,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Axiom,
            role: "axiom-tainted truth bridge: provability/trusted proof is lifted toward truth via soundness assumption",
        },
        BridgeKind::Reflection => BridgeLaw {
            preserves_syntax: true,
            preserves_value: false,
            preserves_proof: true,
            preserves_truth: false,
            requires_axiom: true,
            is_conservative: false,
            is_reflective: true,
            is_reversible: false,
            taint: TrustLevel::Axiom,
            role: "reflective bridge: metatheoretic self-reference; requires explicit reflective/soundness controls",
        },
        BridgeKind::Migration => BridgeLaw {
            preserves_syntax: false,
            preserves_value: true,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: false,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Builtin,
            role: "runtime migration bridge: moves passported value/state across location/architecture boundaries",
        },
        BridgeKind::Materialize => BridgeLaw {
            preserves_syntax: false,
            preserves_value: true,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: false,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Builtin,
            role: "materialization bridge: remote value is explicitly re-entered into local value space",
        },
        BridgeKind::Unsafe => BridgeLaw {
            preserves_syntax: false,
            preserves_value: true,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: true,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Unsafe,
            role: "unsafe bridge: may change meaning/capabilities; result must remain Unsafe-tainted",
        },
        BridgeKind::Unknown(_) => BridgeLaw {
            preserves_syntax: false,
            preserves_value: false,
            preserves_proof: false,
            preserves_truth: false,
            requires_axiom: true,
            is_conservative: false,
            is_reflective: false,
            is_reversible: false,
            taint: TrustLevel::Unsafe,
            role: "unknown bridge kind: no preservation law is known; treated as unsafe for metatheory",
        },
    }
}

pub fn find_bridge<'a>(bridges: &'a [BridgeDecl], source: &str, target: &str, kind: &BridgeKind) -> Option<&'a BridgeDecl> {
    bridges.iter().find(|bridge| {
        bridge.source == source && bridge.target == target && &bridge.kind == kind
    })
}

pub fn has_bridge(bridges: &[BridgeDecl], source: &str, target: &str, kind: &BridgeKind) -> bool {
    find_bridge(bridges, source, target, kind).is_some()
}

pub fn profile_for_decl(bridge: &BridgeDecl) -> BridgeProfile {
    BridgeProfile::from_decl(bridge)
}
