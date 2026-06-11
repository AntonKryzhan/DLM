use dlm_core::*;

fn nat_to_nat_function() -> Passport {
    let report = function_type("Nat", "Nat", true, true, &[], 1).expect("function type");
    function_type_passport("Logic", &report, &[])
}

fn totality_proof() -> Passport {
    let proof_term = Passport::proof_term("Logic", "totality_intro", None);
    Passport::static_proof("Logic", "forall x:Nat. terminates(f x)", &proof_term)
}

#[test]
fn pure_total_contract_requires_static_evidence_before_verified_status() {
    let function = nat_to_nat_function();
    let open = function_contract(
        "succ_contract",
        &function,
        FunctionPurity::Pure,
        FunctionTotality::Total,
        vec![],
        &[],
        1,
    )
    .unwrap();
    assert_eq!(open.status, FunctionContractStatus::Open);
    assert!(require_verified_function_contract(&open, 1).is_err());

    let proof = totality_proof();
    let verified = function_contract(
        "succ_contract",
        &function,
        FunctionPurity::Pure,
        FunctionTotality::Total,
        vec![],
        &[&proof],
        1,
    )
    .unwrap();
    assert_eq!(verified.status, FunctionContractStatus::Verified);
    assert!(verified.open_obligations.is_empty());
    require_verified_function_contract(&verified, 1).unwrap();

    let passport = function_contract_passport("Logic", &verified, &[&function, &proof]);
    assert!(matches!(passport.ty, TypeKind::FunctionContract { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_)));
}

#[test]
fn pure_contract_rejects_explicit_effect_boundaries() {
    let function = nat_to_nat_function();
    let proof = totality_proof();
    let effect = function_effect(FunctionEffectKind::Runtime, "runtime_input_boundary", 1).unwrap();
    let rejected = function_contract(
        "bad_pure_contract",
        &function,
        FunctionPurity::Pure,
        FunctionTotality::Total,
        vec![effect],
        &[&proof],
        1,
    )
    .unwrap();
    assert_eq!(rejected.status, FunctionContractStatus::Rejected);
    assert!(rejected.open_obligations.iter().any(|o| o.contains("pure contract")));
}

#[test]
fn effectful_and_partial_contracts_are_honestly_downgraded() {
    let function = nat_to_nat_function();
    let effect = function_effect(FunctionEffectKind::Io, "stdio", 1).unwrap();
    let report = function_contract(
        "read_then_compute",
        &function,
        FunctionPurity::Effectful,
        FunctionTotality::Partial,
        vec![effect],
        &[],
        1,
    )
    .unwrap();
    assert_eq!(report.status, FunctionContractStatus::Downgraded);
    assert!(report.open_obligations.iter().any(|o| o.contains("effectful")));
    assert!(report.open_obligations.iter().any(|o| o.contains("partial")));
    assert!(require_verified_function_contract(&report, 1).is_err());
}

#[test]
fn proof_truth_runtime_and_application_objects_are_not_contract_subjects() {
    let function = nat_to_nat_function();
    let nat = Passport::literal_nat("Logic");
    let app = application_term(&function, &nat, "0", 1).unwrap();
    let app_passport = application_term_passport("Logic", &app, &[&function, &nat]);
    assert!(function_contract("app_contract", &app_passport, FunctionPurity::Pure, FunctionTotality::Total, vec![], &[], 1).is_err());

    let proof_term = Passport::proof_term("Logic", "intro", None);
    let proof = Passport::static_proof("Logic", "P", &proof_term);
    assert!(function_contract("proof_contract", &proof, FunctionPurity::Pure, FunctionTotality::Total, vec![], &[&proof], 1).is_err());

    let provable = Passport::provable_claim("Logic", "Logic", "P", &proof);
    let truth = Passport::axiom_truth_from_provable("Logic", "P", &provable);
    assert!(function_contract("truth_contract", &truth, FunctionPurity::Pure, FunctionTotality::Total, vec![], &[&proof], 1).is_err());

    let runtime_nat = Passport::runtime_nat_from_input("Logic");
    let witness = Passport::runtime_witness("Logic", "P", &runtime_nat);
    assert!(function_contract("runtime_contract", &witness, FunctionPurity::Pure, FunctionTotality::Total, vec![], &[], 1).is_err());
}

#[test]
fn oracle_and_unsafe_effects_preserve_visible_taint() {
    let unsafe_source = Passport::unsafe_nat("Logic");
    let report = function_type("Nat", "Nat", true, false, &[&unsafe_source], 1).unwrap();
    let function = function_type_passport("Logic", &report, &[&unsafe_source]);
    let oracle = function_effect(FunctionEffectKind::Oracle, "external_oracle", 1).unwrap();
    let unsafe_effect = function_effect(FunctionEffectKind::UnsafeExternal, "ffi_pointer", 1).unwrap();
    let contract = function_contract(
        "foreign_compute",
        &function,
        FunctionPurity::Effectful,
        FunctionTotality::UnknownWithinBudget,
        vec![oracle, unsafe_effect],
        &[],
        1,
    )
    .unwrap();
    assert_eq!(contract.status, FunctionContractStatus::Downgraded);
    assert!(contract.has_oracle_taint);
    assert!(contract.has_unsafe_taint);
    assert!(contract.max_trust >= TrustLevel::Unsafe);

    let passport = function_contract_passport("Logic", &contract, &[&function]);
    assert!(passport.trust >= TrustLevel::Unsafe);
    assert!(passport.history.contains_event("function:contract"));
}

#[test]
fn contract_exports_are_stable_and_order_sensitive() {
    let function = nat_to_nat_function();
    let proof = totality_proof();
    let a = function_effect(FunctionEffectKind::GpuExecution, "gpu_batch", 1).unwrap();
    let b = function_effect(FunctionEffectKind::RemoteExecution, "remote_worker", 1).unwrap();
    let first = function_contract(
        "scheduled_compute",
        &function,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![a.clone(), b.clone()],
        &[&proof],
        1,
    )
    .unwrap();
    let first_again = function_contract(
        "scheduled_compute",
        &function,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![a, b.clone()],
        &[&proof],
        1,
    )
    .unwrap();
    let second = function_contract(
        "scheduled_compute",
        &function,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![b],
        &[&proof],
        1,
    )
    .unwrap();
    assert_eq!(first.fingerprint, first_again.fingerprint);
    assert_ne!(first.fingerprint, second.fingerprint);
    let exported = export_function_contract(&first);
    assert!(exported.contains("function_contract_report: v1"));
    assert!(exported.contains("purity: effectful"));
    assert!(exported.contains("status: downgraded"));
}
