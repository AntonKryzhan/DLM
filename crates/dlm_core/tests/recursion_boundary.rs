use dlm_core::*;

fn proof_evidence() -> Passport {
    let term = Passport::proof_term("Meta", "wf_intro", None);
    Passport::static_proof("Meta", "wf_measure", &term)
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
fn verified_recursion_scheme_is_well_founded_not_proof_or_truth() {
    let nat = Passport::literal_nat("Core");
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");
    let wf = proof_evidence();

    let scheme = recursion_scheme(
        "nat_rec",
        &contract,
        RecursionMeasureKind::NatDecreasing,
        8,
        &[&wf],
        3,
    )
    .unwrap();
    assert_eq!(scheme.status, RecursionStatus::VerifiedWellFounded);
    require_verified_well_founded_recursion(&scheme, 4).unwrap();

    let passport = recursion_scheme_passport("Core", &scheme, &[&nat, &wf]);
    assert!(matches!(passport.ty, TypeKind::RecursionScheme { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn recursive_calls_require_strict_measure_decrease_and_fuel() {
    let nat = Passport::literal_nat("Core");
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");
    let wf = proof_evidence();
    let scheme = recursion_scheme(
        "nat_rec",
        &contract,
        RecursionMeasureKind::NatDecreasing,
        4,
        &[&wf],
        3,
    )
    .unwrap();

    let ok = recursive_call(&scheme, &nat, "Nat", 4, 3, 2, 4).unwrap();
    assert_eq!(ok.status, RecursionStatus::VerifiedWellFounded);
    assert_eq!(ok.fuel_after, 1);
    require_accepted_recursive_call(&ok, 5).unwrap();

    let bad_measure = recursive_call(&scheme, &nat, "Nat", 3, 3, 2, 6).unwrap();
    assert_eq!(bad_measure.status, RecursionStatus::RejectedMeasure);
    assert!(require_accepted_recursive_call(&bad_measure, 7).is_err());

    let bad_fuel = recursive_call(&scheme, &nat, "Nat", 3, 2, 0, 8).unwrap();
    assert_eq!(bad_fuel.status, RecursionStatus::RejectedFuelExceeded);
    assert!(require_accepted_recursive_call(&bad_fuel, 9).is_err());
}

#[test]
fn recursion_rejects_unknown_measure_and_opens_fuel_only_measure() {
    let nat = Passport::literal_nat("Core");
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");

    let unknown = recursion_scheme(
        "unknown_rec",
        &contract,
        RecursionMeasureKind::Unknown,
        3,
        &[&proof_evidence()],
        3,
    )
    .unwrap();
    assert_eq!(unknown.status, RecursionStatus::RejectedMeasure);
    assert!(require_verified_well_founded_recursion(&unknown, 4).is_err());

    let fuel_only = recursion_scheme(
        "fuel_rec",
        &contract,
        RecursionMeasureKind::FuelOnly,
        3,
        &[],
        5,
    )
    .unwrap();
    assert_eq!(fuel_only.status, RecursionStatus::Open);
    let call = recursive_call(&fuel_only, &nat, "Nat", 10, 10, 3, 6).unwrap();
    assert_eq!(call.status, RecursionStatus::Open);
    assert!(call.open_obligations.iter().any(|o| o.contains("fuel-only")));
}

#[test]
fn effectful_or_partial_contracts_downgrade_recursion() {
    let nat = Passport::literal_nat("Core");
    let fty = function_type("Nat", "Nat", false, false, &[&nat], 1).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let effect = function_effect(FunctionEffectKind::Runtime, "runtime_counter", 2).unwrap();
    let contract = function_contract(
        "runtime_rec",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Partial,
        vec![effect],
        &[],
        3,
    )
    .unwrap();

    let scheme = recursion_scheme(
        "runtime_recursion",
        &contract,
        RecursionMeasureKind::NatDecreasing,
        4,
        &[&proof_evidence()],
        4,
    )
    .unwrap();
    assert_eq!(scheme.status, RecursionStatus::Downgraded);
    assert!(scheme.open_obligations.iter().any(|o| o.contains("not pure total")));
}

#[test]
fn recursion_rejects_proof_truth_theorem_and_runtime_arguments() {
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");
    let wf = proof_evidence();
    let scheme = recursion_scheme(
        "nat_rec",
        &contract,
        RecursionMeasureKind::NatDecreasing,
        4,
        &[&wf],
        3,
    )
    .unwrap();

    let term = Passport::proof_term("Meta", "intro", None);
    let proof = Passport::static_proof("Meta", "P", &term);
    let prop = Passport::proposition("Meta", "P", Some(&proof), "test:prop");
    let theorem = theorem_from_static_proof("Meta", "thm", &prop, &proof, 1).unwrap();
    let runtime = Passport::runtime_witness("Meta", "P", &proof);
    let provable = Passport::provable_claim("Meta", "Meta", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Meta", "P", &provable);

    for bad in [&proof, &theorem, &runtime, &truth] {
        assert!(recursive_call(&scheme, bad, "Nat", 4, 3, 2, 5).is_err());
    }
}

#[test]
fn recursion_preserves_axiom_oracle_and_unsafe_taint() {
    let nat = Passport::literal_nat("Core");
    let oracle_evidence = Passport {
        ty: TypeKind::StaticProof("oracle_wf".to_string()),
        construction: ConstructionMode::Oracle,
        capabilities: CapabilitySet::from([Capability::CanSymbolicPrint]),
        cost: CostClass::OracleRequired,
        trust: TrustLevel::Oracle,
        provenance: Provenance::OracleInput,
        validation: ValidationState::Assumed,
        theory: TheoryContext::new("Meta"),
        history: HistoryChain::from_event("oracle:wf"),
        location: LocationContext::local(),
    };

    let fty = function_type("Nat", "Nat", true, false, &[&nat], 1).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let unsafe_effect = function_effect(FunctionEffectKind::UnsafeExternal, "ffi_rec", 2).unwrap();
    let contract = function_contract(
        "unsafe_rec_step",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![unsafe_effect],
        &[&proof_evidence()],
        3,
    )
    .unwrap();

    let scheme = recursion_scheme(
        "unsafe_recursion",
        &contract,
        RecursionMeasureKind::StructuralSubterm,
        8,
        &[&oracle_evidence],
        4,
    )
    .unwrap();
    assert!(scheme.has_oracle_taint);
    assert!(scheme.has_unsafe_taint);
    assert_eq!(scheme.max_trust, TrustLevel::Unsafe);
}

#[test]
fn recursion_exports_are_stable_and_order_sensitive() {
    let nat = Passport::literal_nat("Core");
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");
    let wf = proof_evidence();
    let scheme = recursion_scheme(
        "nat_rec",
        &contract,
        RecursionMeasureKind::NatDecreasing,
        4,
        &[&wf],
        3,
    )
    .unwrap();
    let call_one = recursive_call(&scheme, &nat, "Nat", 4, 3, 2, 4).unwrap();
    let call_two = recursive_call(&scheme, &nat, "Nat", 5, 4, 2, 5).unwrap();

    assert_eq!(export_recursion_scheme(&scheme), export_recursion_scheme(&scheme));
    assert!(export_recursion_scheme(&scheme).contains("recursion_scheme_report: v1"));
    assert!(export_recursive_call(&call_one).contains("recursive_call_report: v1"));
    assert_ne!(call_one.fingerprint, call_two.fingerprint);

    let scheme_passport = recursion_scheme_passport("Core", &scheme, &[&nat, &wf]);
    assert!(matches!(scheme_passport.ty, TypeKind::RecursionScheme { .. }));
    let call_passport = recursive_call_passport("Core", &call_one, &[&nat]);
    assert!(matches!(call_passport.ty, TypeKind::RecursiveCall { .. }));
    let report_passport = recursion_report_passport("Core", "nat_rec", scheme.status, &[&scheme_passport]);
    assert!(matches!(report_passport.ty, TypeKind::RecursionReport { .. }));
}
