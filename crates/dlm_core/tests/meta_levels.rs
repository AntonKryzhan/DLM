use dlm_core::{
    meta_quote_passport, object_level_passport, required_observer_level, validate_meta_observer,
    Capability, DiagnosticKind, MetaAccess, MetaLevelContext, MetaLevelIndex, MetaStage, Passport,
    TrustLevel, TypeKind,
};

#[test]
fn meta_level_indices_are_ordered_and_named() {
    assert_eq!(MetaLevelIndex::object().level(), 0);
    assert_eq!(MetaLevelIndex::meta().level(), 1);
    assert_eq!(MetaLevelIndex::meta_meta().level(), 2);
    assert_eq!(MetaLevelIndex::object().stage(), MetaStage::Object);
    assert_eq!(MetaLevelIndex::meta().stage(), MetaStage::Meta);
    assert_eq!(MetaLevelIndex::meta_meta().stage(), MetaStage::MetaMeta);
    assert_eq!(MetaLevelIndex::new(7).stage(), MetaStage::Higher(7));
    assert_eq!(MetaLevelIndex::object().next(), Some(MetaLevelIndex::meta()));
    assert_eq!(required_observer_level(MetaLevelIndex::meta()), Some(MetaLevelIndex::meta_meta()));
    assert!(MetaLevelIndex::meta().is_strictly_above(MetaLevelIndex::object()));
    assert!(!MetaLevelIndex::object().is_strictly_above(MetaLevelIndex::object()));
}

#[test]
fn object_level_cannot_observe_its_own_truth_or_provability() {
    let truth_err = validate_meta_observer(
        MetaLevelIndex::object(),
        MetaLevelIndex::object(),
        MetaAccess::Truth,
        12,
    )
    .unwrap_err();
    assert_eq!(truth_err.kind, DiagnosticKind::MetaLevelError);
    assert_eq!(truth_err.line, Some(12));
    assert!(truth_err.message.contains("requires a strict meta-level lift"));

    let provability_err = validate_meta_observer(
        MetaLevelIndex::object(),
        MetaLevelIndex::object(),
        MetaAccess::Provability,
        13,
    )
    .unwrap_err();
    assert_eq!(provability_err.kind, DiagnosticKind::MetaLevelError);
}

#[test]
fn meta_level_context_accepts_strict_lifts_only() {
    let ok = MetaLevelContext::object_to_meta("PA");
    assert!(ok.is_strict_lift());
    assert!(ok.require_strict_lift(MetaAccess::Syntax, 1).is_ok());

    let bad = MetaLevelContext::new("PA", MetaLevelIndex::meta(), MetaLevelIndex::meta());
    assert!(!bad.is_strict_lift());
    assert_eq!(
        bad.require_strict_lift(MetaAccess::SelfReference, 2).unwrap_err().kind,
        DiagnosticKind::MetaLevelError,
    );
}

#[test]
fn meta_level_passport_is_a_meta_object_not_a_truth_claim() {
    let level = object_level_passport("Meta");
    assert_eq!(level.ty, TypeKind::MetaLevel { level: 0 });
    assert!(level.capabilities.contains(Capability::CanMetaLevelReason));
    assert!(level.history.contains_event("meta_level:create:M0"));
}

#[test]
fn meta_quote_requires_strict_lift_and_produces_term_only() {
    let source = Passport::literal_nat("ObjectTheory");

    let object_level_err = meta_quote_passport(&source, "Meta", MetaLevelIndex::object(), 4).unwrap_err();
    assert_eq!(object_level_err.kind, DiagnosticKind::MetaLevelError);

    let quoted = meta_quote_passport(&source, "Meta", MetaLevelIndex::meta(), 4).unwrap();
    assert!(matches!(
        &quoted.ty,
        TypeKind::Term { of_theory, of_type }
            if of_theory == "ObjectTheory" && of_type == "Nat"
    ));
    assert!(!matches!(&quoted.ty, TypeKind::TruthClaim { .. } | TypeKind::StaticProof(_)));
    assert!(quoted.capabilities.contains(Capability::CanInspectAst));
    assert!(quoted.capabilities.contains(Capability::CanCompareSyntax));
    assert!(quoted.capabilities.contains(Capability::CanMetaLevelReason));
    assert_eq!(quoted.trust, TrustLevel::Checked);
    assert!(quoted.history.contains_event("meta:quote:ObjectTheory:to:M1"));
}

#[test]
fn meta_quote_preserves_existing_taint_instead_of_cleaning_it() {
    let mut source = Passport::literal_nat("ObjectTheory");
    source.trust = TrustLevel::Axiom;

    let quoted = meta_quote_passport(&source, "Meta", MetaLevelIndex::meta(), 5).unwrap();
    assert_eq!(quoted.trust, TrustLevel::Axiom);
    assert!(quoted.trust >= source.trust);
}
