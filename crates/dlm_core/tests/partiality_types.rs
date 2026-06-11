use dlm_core::*;

#[test]
fn option_type_and_values_are_partiality_objects_not_proofs_or_truth() {
    let nat = Passport::literal_nat("Core");
    let option = option_type("Nat", &[&nat], 1).unwrap();
    let some = option_some(&option, &nat, 2).unwrap();
    let none = option_none(&option, 3).unwrap();
    assert_eq!(some.kind, OptionValueKind::Some);
    assert_eq!(none.kind, OptionValueKind::None);

    let option_passport = option_type_passport("Core", &option, &[&nat]);
    let some_passport = option_value_passport("Core", &some, &[&nat]);
    let none_passport = option_value_passport("Core", &none, &[]);

    assert!(matches!(option_passport.ty, TypeKind::OptionType { .. }));
    assert!(matches!(some_passport.ty, TypeKind::OptionValue { .. }));
    assert!(matches!(none_passport.ty, TypeKind::OptionValue { .. }));
    assert!(!matches!(some_passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn option_some_requires_exact_declared_item_type() {
    let nat = Passport::literal_nat("Core");
    let option = option_type("Bool", &[&nat], 1).unwrap();
    let err = option_some(&option, &nat, 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::PartialityTypeError);
}

#[test]
fn result_type_and_values_track_ok_and_error_branches() {
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
    let result = result_type("Nat", "Text", &[&nat, &text], 1).unwrap();
    let ok = result_ok(&result, &nat, 2).unwrap();
    let err = result_err(&result, &text, 3).unwrap();
    assert_eq!(ok.kind, ResultValueKind::Ok);
    assert_eq!(err.kind, ResultValueKind::Err);
    assert_eq!(ok.result_type, "Nat,Text");

    let ok_passport = result_value_passport("Core", &ok, &[&nat]);
    assert!(matches!(ok_passport.ty, TypeKind::ResultValue { .. }));
}

#[test]
fn result_branches_reject_wrong_type_without_throwing_runtime_exception() {
    let nat = Passport::literal_nat("Core");
    let result = result_type("Bool", "Text", &[&nat], 1).unwrap();
    let err = result_ok(&result, &nat, 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::PartialityTypeError);
}

#[test]
fn partiality_reports_capture_open_and_error_carrying_status_without_becoming_theorem() {
    let nat = Passport::literal_nat("Core");
    let report = partiality_report(
        &nat,
        PartialityStatus::Optional,
        "division may have no value when denominator is zero",
        &[&nat],
        1,
    ).unwrap();
    let passport = partiality_report_passport("Core", &report, &[&nat]);
    assert_eq!(report.status, PartialityStatus::Optional);
    assert!(matches!(passport.ty, TypeKind::PartialityReport { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn proof_truth_theorem_and_runtime_objects_are_not_option_or_result_values() {
    let term = Passport::proof_term("Meta", "intro", None);
    let proof = Passport::static_proof("Meta", "P", &term);
    let prop = Passport::proposition("Meta", "P", Some(&proof), "test:prop");
    let theorem = theorem_from_static_proof("Meta", "thm", "P", &proof, 1).unwrap();
    let runtime = Passport::runtime_witness("Meta", "P", &proof);
    let provable = Passport::provable_claim("Meta", "Meta", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Meta", "P", &provable);
    let option = option_type("StaticProof<P>", &[&proof], 1).unwrap();

    for bad in [&proof, &prop, &theorem, &runtime, &truth] {
        assert!(option_some(&option, bad, 2).is_err());
    }
}

#[test]
fn option_result_and_partiality_reports_preserve_axiom_oracle_and_unsafe_taint() {
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
    let option = option_type("Nat", &[&base], 1).unwrap();
    let some = option_some(&option, &oracle, 2).unwrap();
    assert!(some.has_oracle_taint);
    assert_eq!(some.max_trust, TrustLevel::Oracle);

    let result = result_type("Nat", "Nat", &[&base], 3).unwrap();
    let err = result_err(&result, &unsafe_value, 4).unwrap();
    assert!(err.has_unsafe_taint);
    assert_eq!(err.max_trust, TrustLevel::Unsafe);
}

#[test]
fn exports_are_stable_and_order_sensitive() {
    let nat = Passport::literal_nat("Core");
    let option = option_type("Nat", &[&nat], 1).unwrap();
    let some = option_some(&option, &nat, 2).unwrap();
    let result = result_type("Nat", "Text", &[&nat], 3).unwrap();
    let ok = result_ok(&result, &nat, 4).unwrap();
    let report = partiality_report(&nat, PartialityStatus::Optional, "maybe", &[&nat], 5).unwrap();

    assert_eq!(export_option_type(&option), export_option_type(&option));
    assert!(export_option_value(&some).contains("option_value_report: v1"));
    assert!(export_result_type(&result).contains("ok_type: Nat"));
    assert!(export_result_value(&ok).contains("kind: ok"));
    assert!(export_partiality_report(&report).contains("status: optional"));

    let result_reversed = result_type("Text", "Nat", &[&nat], 6).unwrap();
    assert_ne!(result.fingerprint, result_reversed.fingerprint);
}
