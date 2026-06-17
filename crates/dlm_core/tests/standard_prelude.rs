use dlm_core::*;

fn proof_evidence() -> Passport {
    let term = Passport::proof_term("Prelude", "totality_intro", None);
    Passport::static_proof("Prelude", "totality", &term)
}

fn verified_contract(name: &str, domain: &str, codomain: &str) -> FunctionContractReport {
    let fty = function_type(domain, codomain, true, true, &[], 1).unwrap();
    let fpass = function_type_passport("Prelude", &fty, &[]);
    let proof = proof_evidence();
    function_contract(
        name,
        &fpass,
        FunctionPurity::Pure,
        FunctionTotality::Total,
        vec![],
        &[&proof],
        2,
    )
    .unwrap()
}

fn params() -> PreludeSignatureParams {
    prelude_signature_params("Nat", "Text", "Text", "Nat", 1).unwrap()
}

fn verified_budget(name: &str) -> TerminationBudgetReport {
    let contract = computation_budget_contract(name, 0, 32, 0, 32, 1).unwrap();
    unify_termination_budget(&contract, &[], &[], &[], &[], &[], 1).unwrap()
}

#[test]
fn nat_add_checked_contract_is_prelude_not_proof_or_truth() {
    let params = params();
    let sig = prelude_operation_signature(PreludeOperationKind::NatAdd, &params, 1).unwrap();
    let contract = verified_contract("nat_add", "ProductType<Nat*Nat>", "Nat");

    let report = standard_prelude_contract("std_nat_add", &sig, &contract, None, &[], 2).unwrap();
    assert_eq!(report.status, PreludeContractStatus::VerifiedChecked);
    assert!(report.open_obligations.is_empty());
    require_verified_standard_prelude_contract(&report, 3).unwrap();

    let passport = standard_prelude_contract_passport("Prelude", &report, &[]);
    assert!(matches!(passport.ty, TypeKind::StandardPreludeContract { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
    assert!(passport.history.contains_event("standard_prelude:contract"));
}

#[test]
fn signature_mismatch_is_rejected_without_implicit_coercion() {
    let params = params();
    let sig = prelude_operation_signature(PreludeOperationKind::NatAdd, &params, 1).unwrap();
    let wrong = verified_contract("nat_to_nat", "Nat", "Nat");

    let report = standard_prelude_contract("bad_nat_add", &sig, &wrong, None, &[], 2).unwrap();
    assert_eq!(report.status, PreludeContractStatus::RejectedSignature);
    assert!(report.open_obligations.iter().any(|o| o.contains("domain mismatch")));
    assert!(require_verified_standard_prelude_contract(&report, 3).is_err());
}

#[test]
fn option_and_result_map_have_explicit_algebraic_signatures() {
    let params = params();
    let option_sig = prelude_operation_signature(PreludeOperationKind::OptionMap, &params, 1).unwrap();
    assert_eq!(option_sig.domain, "ProductType<OptionType<Nat>*FunctionType<Nat->Text>>");
    assert_eq!(option_sig.codomain, "OptionType<Text>");
    assert!(!option_sig.requires_budget);
    let option_contract = verified_contract("option_map", &option_sig.domain, &option_sig.codomain);
    let option_report = standard_prelude_contract("std_option_map", &option_sig, &option_contract, None, &[], 2).unwrap();
    assert_eq!(option_report.status, PreludeContractStatus::VerifiedChecked);

    let result_sig = prelude_operation_signature(PreludeOperationKind::ResultMap, &params, 1).unwrap();
    assert_eq!(result_sig.domain, "ProductType<ResultType<Nat,Text>*FunctionType<Nat->Text>>");
    assert_eq!(result_sig.codomain, "ResultType<Text,Text>");
    let result_contract = verified_contract("result_map", &result_sig.domain, &result_sig.codomain);
    let result_report = standard_prelude_contract("std_result_map", &result_sig, &result_contract, None, &[], 2).unwrap();
    assert_eq!(result_report.status, PreludeContractStatus::VerifiedChecked);
}

#[test]
fn collection_prelude_operations_require_explicit_verified_budget() {
    let params = params();
    let sig = prelude_operation_signature(PreludeOperationKind::SequenceMap, &params, 1).unwrap();
    assert!(sig.requires_budget);
    let contract = verified_contract("sequence_map", &sig.domain, &sig.codomain);

    let open = standard_prelude_contract("std_sequence_map", &sig, &contract, None, &[], 2).unwrap();
    assert_eq!(open.status, PreludeContractStatus::Open);
    assert!(open.open_obligations.iter().any(|o| o.contains("termination budget")));

    let budget = verified_budget("std_sequence_map_budget");
    let verified = standard_prelude_contract("std_sequence_map", &sig, &contract, Some(&budget), &[], 2).unwrap();
    assert_eq!(verified.status, PreludeContractStatus::VerifiedChecked);
    assert_eq!(verified.budget_name.as_deref(), Some("std_sequence_map_budget"));
}

#[test]
fn downgraded_function_contract_remains_visible_in_prelude() {
    let params = params();
    let sig = prelude_operation_signature(PreludeOperationKind::BoolNot, &params, 1).unwrap();
    let fty = function_type(sig.domain.as_str(), sig.codomain.as_str(), true, false, &[], 1).unwrap();
    let fpass = function_type_passport("Prelude", &fty, &[]);
    let effect = function_effect(FunctionEffectKind::Io, "stdio", 1).unwrap();
    let contract = function_contract(
        "bool_not_io",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Partial,
        vec![effect],
        &[],
        2,
    )
    .unwrap();
    assert_eq!(contract.status, FunctionContractStatus::Downgraded);

    let report = standard_prelude_contract("std_bool_not", &sig, &contract, None, &[], 3).unwrap();
    assert_eq!(report.status, PreludeContractStatus::Downgraded);
    assert!(report.open_obligations.iter().any(|o| o.contains("effectful")));
    assert!(report.open_obligations.iter().any(|o| o.contains("partial")));
}

#[test]
fn prelude_contracts_preserve_axiom_oracle_and_unsafe_taint() {
    let params = params();
    let sig = prelude_operation_signature(PreludeOperationKind::NatEq, &params, 1).unwrap();
    let unsafe_source = Passport::unsafe_nat("Prelude");
    let fty = function_type(sig.domain.as_str(), sig.codomain.as_str(), true, true, &[&unsafe_source], 1).unwrap();
    let fpass = function_type_passport("Prelude", &fty, &[&unsafe_source]);
    let proof = proof_evidence();
    let oracle_effect = function_effect(FunctionEffectKind::Oracle, "foreign_eq_oracle", 2).unwrap();
    let unsafe_effect = function_effect(FunctionEffectKind::UnsafeExternal, "ffi_eq", 2).unwrap();
    let contract = function_contract(
        "nat_eq_foreign",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Total,
        vec![oracle_effect, unsafe_effect],
        &[&proof],
        3,
    )
    .unwrap();

    let report = standard_prelude_contract("std_nat_eq", &sig, &contract, None, &[&unsafe_source], 4).unwrap();
    assert_eq!(report.status, PreludeContractStatus::Downgraded);
    assert!(report.has_oracle_taint);
    assert!(report.has_unsafe_taint);
    assert!(report.max_trust >= TrustLevel::Unsafe);

    let passport = standard_prelude_contract_passport("Prelude", &report, &[&unsafe_source]);
    assert!(passport.trust >= TrustLevel::Unsafe);
}

#[test]
fn prelude_exports_are_stable_and_operation_sensitive() {
    let params = params();
    let add_sig = prelude_operation_signature(PreludeOperationKind::NatAdd, &params, 1).unwrap();
    let eq_sig = prelude_operation_signature(PreludeOperationKind::NatEq, &params, 1).unwrap();
    let add_contract = verified_contract("nat_add", &add_sig.domain, &add_sig.codomain);
    let eq_contract = verified_contract("nat_eq", &eq_sig.domain, &eq_sig.codomain);

    let first = standard_prelude_contract("std_nat_add", &add_sig, &add_contract, None, &[], 2).unwrap();
    let first_again = standard_prelude_contract("std_nat_add", &add_sig, &add_contract, None, &[], 2).unwrap();
    let second = standard_prelude_contract("std_nat_eq", &eq_sig, &eq_contract, None, &[], 2).unwrap();

    assert_eq!(first.fingerprint, first_again.fingerprint);
    assert_ne!(first.fingerprint, second.fingerprint);

    let exported = export_standard_prelude_contract(&first);
    assert!(exported.contains("standard_prelude_contract: v1"));
    assert!(exported.contains("operation: nat.add"));
    assert!(exported.contains("status: verified_checked"));
}
