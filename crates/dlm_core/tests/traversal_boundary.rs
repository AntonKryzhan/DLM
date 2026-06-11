use dlm_core::*;

fn text_passport() -> Passport {
    Passport {
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
    }
}

fn proof_evidence() -> Passport {
    let term = Passport::proof_term("Meta", "totality_intro", None);
    Passport::static_proof("Meta", "totality", &term)
}

fn verified_contract(name: &str, domain: &str, codomain: &str) -> FunctionContractReport {
    let nat = Passport::literal_nat("Core");
    let fty = function_type(domain, codomain, true, true, &[&nat], 1).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let evidence = proof_evidence();
    function_contract(
        name,
        &fpass,
        FunctionPurity::Pure,
        FunctionTotality::Total,
        vec![],
        &[&evidence],
        2,
    )
    .unwrap()
}

#[test]
fn map_sequence_is_bounded_and_not_a_proof_or_truth() {
    let nat = Passport::literal_nat("Core");
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let seq = sequence_value(&seq_ty, &[&nat, &nat], 2).unwrap();
    let contract = verified_contract("nat_to_text", "Nat", "Text");

    let report = map_sequence(&seq, &contract, "Text", 2, 3).unwrap();
    assert_eq!(report.status, TraversalStatus::VerifiedBounded);
    assert_eq!(report.result_collection_type, "Sequence<Text>");
    require_verified_bounded_map(&report, 4).unwrap();

    let passport = map_traversal_passport("Core", &report, &[&nat]);
    assert!(matches!(passport.ty, TypeKind::MapTraversal { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn map_and_fold_reject_contract_domain_mismatch_without_implicit_coercion() {
    let nat = Passport::literal_nat("Core");
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let seq = sequence_value(&seq_ty, &[&nat], 2).unwrap();

    let wrong_map = verified_contract("bool_to_text", "Bool", "Text");
    let err = map_sequence(&seq, &wrong_map, "Text", 1, 3).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TraversalError);

    let wrong_fold = verified_contract("bad_step", "ProductType<Text*Nat>", "Text");
    let err = fold_sequence(&seq, &nat, "Nat", &wrong_fold, 1, 4).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TraversalError);
}

#[test]
fn fold_sequence_is_explicitly_fuel_bounded() {
    let nat = Passport::literal_nat("Core");
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let seq = sequence_value(&seq_ty, &[&nat, &nat], 2).unwrap();
    let step = verified_contract("nat_sum_step", "ProductType<Nat*Nat>", "Nat");

    let ok = fold_sequence(&seq, &nat, "Nat", &step, 2, 3).unwrap();
    assert_eq!(ok.status, TraversalStatus::VerifiedBounded);
    assert_eq!(ok.result_type, "Nat");
    require_verified_bounded_fold(&ok, 4).unwrap();

    let rejected = fold_sequence(&seq, &nat, "Nat", &step, 1, 5).unwrap();
    assert_eq!(rejected.status, TraversalStatus::RejectedFuelExceeded);
    assert!(require_verified_bounded_fold(&rejected, 6).is_err());
}

#[test]
fn effectful_or_partial_contracts_downgrade_traversals() {
    let nat = Passport::literal_nat("Core");
    let list_ty = list_type("Nat", &[&nat], 1).unwrap();
    let list = list_value(&list_ty, &[&nat], 2).unwrap();
    let fty = function_type("Nat", "Nat", false, false, &[&nat], 3).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let effect = function_effect(FunctionEffectKind::Runtime, "runtime_counter", 4).unwrap();
    let contract = function_contract(
        "runtime_partial_step",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Partial,
        vec![effect],
        &[],
        5,
    )
    .unwrap();

    let report = map_list(&list, &contract, "Nat", 1, 6).unwrap();
    assert_eq!(report.status, TraversalStatus::Downgraded);
    assert!(report.open_obligations.iter().any(|o| o.contains("downgraded")));
}

#[test]
fn fold_rejects_proof_truth_theorem_and_runtime_accumulators() {
    let nat = Passport::literal_nat("Core");
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let seq = sequence_value(&seq_ty, &[&nat], 2).unwrap();
    let step = verified_contract("nat_step", "ProductType<Nat*Nat>", "Nat");

    let term = Passport::proof_term("Meta", "intro", None);
    let proof = Passport::static_proof("Meta", "P", &term);
    let prop = Passport::proposition("Meta", "P", Some(&proof), "test:prop");
    let theorem = theorem_from_static_proof("Meta", "thm", &prop, &proof, 1).unwrap();
    let runtime = Passport::runtime_witness("Meta", "P", &proof);
    let provable = Passport::provable_claim("Meta", "Meta", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Meta", "P", &provable);

    for bad in [&proof, &theorem, &runtime, &truth] {
        assert!(fold_sequence(&seq, bad, "Nat", &step, 1, 3).is_err());
    }
}

#[test]
fn traversal_reports_preserve_axiom_oracle_and_unsafe_taint() {
    let nat = Passport::literal_nat("Core");
    let oracle_nat = Passport {
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
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let seq = sequence_value(&seq_ty, &[&oracle_nat], 2).unwrap();

    let fty = function_type("Nat", "Nat", true, false, &[&nat], 3).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let unsafe_effect = function_effect(FunctionEffectKind::UnsafeExternal, "ffi_callback", 4).unwrap();
    let contract = function_contract(
        "unsafe_map",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![unsafe_effect],
        &[&proof_evidence()],
        5,
    )
    .unwrap();

    let report = map_sequence(&seq, &contract, "Nat", 1, 6).unwrap();
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert_eq!(report.max_trust, TrustLevel::Unsafe);
}

#[test]
fn traversal_exports_are_stable_and_order_sensitive() {
    let nat = Passport::literal_nat("Core");
    let text = text_passport();
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let one = sequence_value(&seq_ty, &[&nat], 2).unwrap();
    let two = sequence_value(&seq_ty, &[&nat, &nat], 3).unwrap();
    let contract = verified_contract("nat_to_text", "Nat", "Text");

    let map_one = map_sequence(&one, &contract, "Text", 1, 4).unwrap();
    let map_two = map_sequence(&two, &contract, "Text", 2, 5).unwrap();
    assert_eq!(export_map_traversal(&map_one), export_map_traversal(&map_one));
    assert_ne!(map_one.fingerprint, map_two.fingerprint);
    assert!(export_map_traversal(&map_one).contains("map_traversal_report: v1"));

    let fold_contract = verified_contract("text_nat_fold", "ProductType<Text*Nat>", "Text");
    let fold = fold_sequence(&one, &text, "Text", &fold_contract, 1, 6).unwrap();
    assert!(export_fold_traversal(&fold).contains("fold_traversal_report: v1"));

    let passport = traversal_report_passport("Core", "map_one", map_one.status, &[&nat]);
    assert!(matches!(passport.ty, TypeKind::TraversalReport { .. }));
}
