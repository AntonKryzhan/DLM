use std::fmt;

use crate::diagnostics::{Diagnostic, DiagnosticKind};
use crate::passport::{
    Capability, CapabilitySet, ConstructionMode, CostClass, HistoryChain, LocationContext, Passport,
    Provenance, TheoryContext, TrustLevel, TypeKind, ValidationState,
};

/// A small explicit meta-level index used before the full HIR/TypedIR split.
///
/// The invariant is intentionally simple:
/// - M0 is the object level;
/// - M1 is the first meta level;
/// - M2 is meta-meta;
/// - observing syntax/provability/truth of level N requires a strict lift to > N.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct MetaLevelIndex(pub u8);

impl MetaLevelIndex {
    pub const OBJECT: Self = Self(0);
    pub const META: Self = Self(1);
    pub const META_META: Self = Self(2);

    pub fn new(level: u8) -> Self {
        Self(level)
    }

    pub fn object() -> Self {
        Self::OBJECT
    }

    pub fn meta() -> Self {
        Self::META
    }

    pub fn meta_meta() -> Self {
        Self::META_META
    }

    pub fn level(self) -> u8 {
        self.0
    }

    pub fn next(self) -> Option<Self> {
        self.0.checked_add(1).map(Self)
    }

    pub fn stage(self) -> MetaStage {
        match self.0 {
            0 => MetaStage::Object,
            1 => MetaStage::Meta,
            2 => MetaStage::MetaMeta,
            n => MetaStage::Higher(n),
        }
    }

    pub fn is_strictly_above(self, object_level: Self) -> bool {
        self.0 > object_level.0
    }
}

impl fmt::Display for MetaLevelIndex {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "M{}", self.0)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MetaStage {
    Object,
    Meta,
    MetaMeta,
    Higher(u8),
}

impl fmt::Display for MetaStage {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetaStage::Object => write!(f, "object"),
            MetaStage::Meta => write!(f, "meta"),
            MetaStage::MetaMeta => write!(f, "meta-meta"),
            MetaStage::Higher(level) => write!(f, "higher-meta<M{level}>")
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MetaAccess {
    Syntax,
    Value,
    Provability,
    Truth,
    SelfReference,
}

impl fmt::Display for MetaAccess {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MetaAccess::Syntax => write!(f, "syntax"),
            MetaAccess::Value => write!(f, "value"),
            MetaAccess::Provability => write!(f, "provability"),
            MetaAccess::Truth => write!(f, "truth"),
            MetaAccess::SelfReference => write!(f, "self-reference"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MetaLevelContext {
    pub object_theory: String,
    pub object_level: MetaLevelIndex,
    pub observer_level: MetaLevelIndex,
}

impl MetaLevelContext {
    pub fn new(
        object_theory: impl Into<String>,
        object_level: MetaLevelIndex,
        observer_level: MetaLevelIndex,
    ) -> Self {
        Self {
            object_theory: object_theory.into(),
            object_level,
            observer_level,
        }
    }

    pub fn object_to_meta(object_theory: impl Into<String>) -> Self {
        Self::new(object_theory, MetaLevelIndex::object(), MetaLevelIndex::meta())
    }

    pub fn is_strict_lift(&self) -> bool {
        self.observer_level.is_strictly_above(self.object_level)
    }

    pub fn require_strict_lift(&self, access: MetaAccess, line: usize) -> Result<(), Diagnostic> {
        validate_meta_observer(self.observer_level, self.object_level, access, line)
    }
}

pub fn required_observer_level(object_level: MetaLevelIndex) -> Option<MetaLevelIndex> {
    object_level.next()
}

pub fn validate_meta_observer(
    observer_level: MetaLevelIndex,
    object_level: MetaLevelIndex,
    access: MetaAccess,
    line: usize,
) -> Result<(), Diagnostic> {
    if observer_level.is_strictly_above(object_level) {
        return Ok(());
    }

    Err(Diagnostic::error(
        DiagnosticKind::MetaLevelError,
        Some(line),
        format!(
            "{access} access to {object_level} requires a strict meta-level lift, but observer is {observer_level}"
        ),
    )
    .with_help(format!(
        "move the operation to at least {} before inspecting {access}; object-level code cannot inspect its own syntax/provability/truth directly",
        required_observer_level(object_level)
            .map(|level| level.to_string())
            .unwrap_or_else(|| "a higher meta level".to_string())
    )))
}

pub fn meta_level_passport(theory: &str, level: MetaLevelIndex) -> Passport {
    Passport {
        ty: TypeKind::MetaLevel { level: level.level() },
        construction: ConstructionMode::Literal,
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanMetaLevelReason,
            Capability::CanExtractDefinabilityMeta,
        ]),
        cost: CostClass::Trivial,
        trust: TrustLevel::Builtin,
        provenance: Provenance::BuiltinKnown,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new(theory),
        history: HistoryChain::from_event(format!("meta_level:create:{level}")),
        location: LocationContext::local(),
    }
}

pub fn object_level_passport(theory: &str) -> Passport {
    meta_level_passport(theory, MetaLevelIndex::object())
}

pub fn meta_quote_passport(
    source: &Passport,
    target_theory: &str,
    observer_level: MetaLevelIndex,
    line: usize,
) -> Result<Passport, Diagnostic> {
    validate_meta_observer(observer_level, MetaLevelIndex::object(), MetaAccess::Syntax, line)?;

    Ok(Passport {
        ty: TypeKind::Term {
            of_theory: source.theory.home.clone(),
            of_type: source.ty.to_string(),
        },
        construction: source.construction.max(ConstructionMode::Definable),
        capabilities: CapabilitySet::from([
            Capability::CanSymbolicPrint,
            Capability::CanInspectAst,
            Capability::CanCompareSyntax,
            Capability::CanMetaLevelReason,
        ]),
        cost: source.cost.max(CostClass::SmallFinite),
        trust: source.trust,
        provenance: source.provenance.max(Provenance::InternalDerived),
        validation: source.validation,
        theory: TheoryContext::new(target_theory),
        history: HistoryChain::from_source(
            &source.history,
            format!("meta:quote:{}:to:{observer_level}", source.theory.home),
        ),
        location: LocationContext::local(),
    })
}
