use dlm_core::*;

#[test]
fn list_type_and_value_are_finite_collections_not_proofs_or_truth() {
    let nat = Passport::literal_nat("Core");
    let list = list_type("Nat", &[&nat], 1).unwrap();
    let value = list_value(&list, &[&nat, &nat], 2).unwrap();
    assert_eq!(value.len, 2);
    assert_eq!(value.item_type, "Nat");

    let list_passport = list_type_passport("Core", &list, &[&nat]);
    let value_passport = list_value_passport("Core", &value, &[&nat, &nat]);
    assert!(matches!(list_passport.ty, TypeKind::ListType { .. }));
    assert!(matches!(value_passport.ty, TypeKind::ListValue { .. }));
    assert!(!matches!(value_passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn empty_list_is_typed_and_does_not_create_hidden_any_or_infinity() {
    let nat = Passport::literal_nat("Core");
    let list = list_type("Nat", &[&nat], 1).unwrap();
    let empty = list_value(&list, &[], 2).unwrap();
    assert_eq!(empty.len, 0);
    assert_eq!(empty.item_type, "Nat");
    assert!(empty.items.is_empty());
    let passport = list_value_passport("Core", &empty, &[]);
    assert!(matches!(passport.ty, TypeKind::ListValue { item, len } if item == "Nat" && len == 0));
}

#[test]
fn list_and_sequence_items_must_match_declared_type_exactly() {
    let nat = Passport::literal_nat("Core");
    let text = Passport {
        ty: TypeKind::Text,
        construction: ConstructionMode::Literal,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::Trivial,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalLiteral,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new("Core"),
        history: HistoryChain::from_event("test:text"),
        location: LocationContext::local(),
    };
    let list = list_type("Nat", &[&nat], 1).unwrap();
    let err = list_value(&list, &[&text], 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::SequenceTypeError);

    let sequence = sequence_type("Text", &[&text], 3).unwrap();
    let err = sequence_value(&sequence, &[&nat], 4).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::SequenceTypeError);
}

#[test]
fn sequence_index_is_explicit_and_returns_optional_boundary_not_runtime_exception() {
    let nat = Passport::literal_nat("Core");
    let sequence = sequence_type("Nat", &[&nat], 1).unwrap();
    let value = sequence_value(&sequence, &[&nat, &nat], 2).unwrap();

    let in_bounds = sequence_index(&value, 1, 3).unwrap();
    let out_of_bounds = sequence_index(&value, 5, 4).unwrap();
    assert_eq!(in_bounds.status, SequenceIndexStatus::InBounds);
    assert_eq!(out_of_bounds.status, SequenceIndexStatus::OutOfBounds);
    assert_eq!(in_bounds.result_type, "Option<Nat>");
    assert_eq!(out_of_bounds.result_type, "Option<Nat>");
    assert!(in_bounds.value.is_some());
    assert!(out_of_bounds.value.is_none());

    let passport = sequence_index_passport("Core", &out_of_bounds, &[]);
    assert!(matches!(passport.ty, TypeKind::SequenceIndex { .. }));
}

#[test]
fn option_and_result_values_can_be_finite_collection_items_when_explicitly_typed() {
    let nat = Passport::literal_nat("Core");
    let option = option_type("Nat", &[&nat], 1).unwrap();
    let some = option_some(&option, &nat, 2).unwrap();
    let some_passport = option_value_passport("Core", &some, &[&nat]);

    let list = list_type(some_passport.ty.to_string(), &[&some_passport], 3).unwrap();
    let value = list_value(&list, &[&some_passport], 4).unwrap();
    assert_eq!(value.len, 1);

    let result = result_type("Nat", "Nat", &[&nat], 5).unwrap();
    let ok = result_ok(&result, &nat, 6).unwrap();
    let ok_passport = result_value_passport("Core", &ok, &[&nat]);
    let seq = sequence_type(ok_passport.ty.to_string(), &[&ok_passport], 7).unwrap();
    let seq_value = sequence_value(&seq, &[&ok_passport], 8).unwrap();
    assert_eq!(seq_value.len, 1);
}

#[test]
fn proof_truth_theorem_and_runtime_objects_are_not_collection_items() {
    let term = Passport::proof_term("Meta", "intro", None);
    let proof = Passport::static_proof("Meta", "P", &term);
    let prop = Passport::proposition("Meta", "P", Some(&proof), "test:prop");
    let theorem = theorem_from_static_proof("Meta", "thm", &prop, &proof, 1).unwrap();
    let runtime = Passport::runtime_witness("Meta", "P", &proof);
    let provable = Passport::provable_claim("Meta", "Meta", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Meta", "P", &provable);
    let list = list_type("StaticProof<P>", &[&proof], 1).unwrap();

    for bad in [&proof, &prop, &theorem, &runtime, &truth] {
        assert!(list_value(&list, &[bad], 2).is_err());
    }
}

#[test]
fn finite_collections_preserve_axiom_oracle_and_unsafe_taint() {
    let base = Passport::literal_nat("Core");
    let oracle = Passport {
        ty: TypeKind::Nat,
        construction: ConstructionMode::Oracle,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::OracleRequired,
        trust: TrustLevel::Oracle,
        provenance: Provenance::OracleInput,
        validation: ValidationState::Assumed,
        theory: TheoryContext::new("Core"),
        history: HistoryChain::from_event("oracle:nat"),
        location: LocationContext::local(),
    };
    let unsafe_value = Passport {
        ty: TypeKind::Nat,
        construction: ConstructionMode::Unsafe,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::UnsafeUnknown,
        trust: TrustLevel::Unsafe,
        provenance: Provenance::UnsafeExternal,
        validation: ValidationState::Assumed,
        theory: TheoryContext::new("Core"),
        history: HistoryChain::from_event("unsafe:nat"),
        location: LocationContext::local(),
    };

    let list = list_type("Nat", &[&base], 1).unwrap();
    let list_value_report = list_value(&list, &[&oracle], 2).unwrap();
    assert!(list_value_report.has_oracle_taint);
    assert_eq!(list_value_report.max_trust, TrustLevel::Oracle);

    let sequence = sequence_type("Nat", &[&base], 3).unwrap();
    let sequence_value_report = sequence_value(&sequence, &[&unsafe_value], 4).unwrap();
    assert!(sequence_value_report.has_unsafe_taint);
    assert_eq!(sequence_value_report.max_trust, TrustLevel::Unsafe);
}

#[test]
fn sequence_exports_are_stable_and_order_sensitive() {
    let nat = Passport::literal_nat("Core");
    let list = list_type("Nat", &[&nat], 1).unwrap();
    let list_value_report = list_value(&list, &[&nat, &nat], 2).unwrap();
    let sequence = sequence_type("Nat", &[&nat], 3).unwrap();
    let sequence_value_report = sequence_value(&sequence, &[&nat], 4).unwrap();
    let index = sequence_index(&sequence_value_report, 0, 5).unwrap();

    assert_eq!(export_list_type(&list), export_list_type(&list));
    assert!(export_list_value(&list_value_report).contains("list_value_report: v1"));
    assert!(export_sequence_type(&sequence).contains("sequence_type_report: v1"));
    assert!(export_sequence_value(&sequence_value_report).contains("len: 1"));
    assert!(export_sequence_index(&index).contains("status: in_bounds"));

    let text = Passport {
        ty: TypeKind::Text,
        construction: ConstructionMode::Literal,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::Trivial,
        trust: TrustLevel::Checked,
        provenance: Provenance::InternalLiteral,
        validation: ValidationState::StaticChecked,
        theory: TheoryContext::new("Core"),
        history: HistoryChain::from_event("test:text"),
        location: LocationContext::local(),
    };
    let text_sequence = sequence_type("Text", &[&text], 6).unwrap();
    assert_ne!(sequence.fingerprint, text_sequence.fingerprint);
}
