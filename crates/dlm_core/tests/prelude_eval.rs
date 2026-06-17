use dlm_core::*;

fn proof_evidence() -> Passport {
    let term = Passport::proof_term("Prelude", "totality_intro", None);
    Passport::static_proof("Prelude", "totality", &term)
}

fn verified_function_contract(name: &str, domain: &str, codomain: &str) -> FunctionContractReport {
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

fn verified_budget(name: &str) -> TerminationBudgetReport {
    let contract = computation_budget_contract(name, 0, 64, 0, 64, 1).unwrap();
    unify_termination_budget(&contract, &[], &[], &[], &[], &[], 1).unwrap()
}

fn params() -> PreludeSignatureParams {
    prelude_signature_params("Nat", "Text", "Text", "Nat", 1).unwrap()
}

fn prelude_contract(operation: PreludeOperationKind) -> StandardPreludeContractReport {
    let params = params();
    let sig = prelude_operation_signature(operation, &params, 1).unwrap();
    let fc = verified_function_contract(&format!("contract_{}", operation.to_string().replace('.', "_")), &sig.domain, &sig.codomain);
    let budget = if sig.requires_budget { Some(verified_budget("eval_budget")) } else { None };
    standard_prelude_contract("std_eval", &sig, &fc, budget.as_ref(), &[], 2).unwrap()
}

#[test]
fn nat_and_bool_small_steps_are_deterministic_and_not_proofs() {
    let add = prelude_contract(PreludeOperationKind::NatAdd);
    let input = eval_product(eval_nat(2), eval_nat(40));
    let report = evaluate_standard_prelude("eval_add", &add, input, 1, &[], 3).unwrap();
    assert_eq!(report.status, PreludeEvalStatus::Evaluated);
    assert_eq!(report.steps_used, 1);
    assert_eq!(report.result, Some(eval_nat(42)));
    require_evaluated_prelude(&report, 4).unwrap();

    let passport = prelude_evaluation_passport("Prelude", &report, &[]);
    assert!(matches!(passport.ty, TypeKind::PreludeEvaluationReport { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. } | TypeKind::RuntimeWitness(_)));
    assert!(passport.history.contains_event("standard_prelude:evaluate"));

    let not = prelude_contract(PreludeOperationKind::BoolNot);
    let not_report = evaluate_standard_prelude("eval_not", &not, eval_bool(false), 1, &[], 5).unwrap();
    assert_eq!(not_report.status, PreludeEvalStatus::Evaluated);
    assert_eq!(not_report.result, Some(eval_bool(true)));
}

#[test]
fn sequence_length_and_index_are_explicit_option_boundaries() {
    let seq = eval_sequence("Nat", vec![eval_nat(7), eval_nat(8)], 1).unwrap();

    let length = prelude_contract(PreludeOperationKind::SequenceLength);
    let len_report = evaluate_standard_prelude("eval_seq_len", &length, seq.clone(), 1, &[], 2).unwrap();
    assert_eq!(len_report.status, PreludeEvalStatus::Evaluated);
    assert_eq!(len_report.result, Some(eval_nat(2)));

    let index = prelude_contract(PreludeOperationKind::SequenceIndex);
    let hit = evaluate_standard_prelude("eval_seq_index_hit", &index, eval_product(seq.clone(), eval_nat(1)), 1, &[], 3).unwrap();
    assert_eq!(hit.status, PreludeEvalStatus::Evaluated);
    assert_eq!(hit.result, Some(eval_option_some("Nat", eval_nat(8), 3).unwrap()));

    let miss = evaluate_standard_prelude("eval_seq_index_miss", &index, eval_product(seq, eval_nat(99)), 1, &[], 4).unwrap();
    assert_eq!(miss.status, PreludeEvalStatus::Evaluated);
    assert_eq!(miss.result, Some(eval_option_none("Nat", 4).unwrap()));
}

#[test]
fn option_and_result_map_are_bounded_symbolic_not_user_code_execution() {
    let f = eval_function_ref("to_text", "Nat", "Text", 1).unwrap();
    let option = eval_option_some("Nat", eval_nat(5), 1).unwrap();
    let option_map = prelude_contract(PreludeOperationKind::OptionMap);
    let option_report = evaluate_standard_prelude("eval_option_map", &option_map, eval_product(option, f.clone()), 1, &[], 2).unwrap();
    assert_eq!(option_report.status, PreludeEvalStatus::SymbolicEvaluated);
    assert!(option_report.open_obligations.iter().any(|o| o.contains("symbolic application")));
    assert_eq!(option_report.result.as_ref().unwrap().type_key(), "OptionType<Text>");

    let none = eval_option_none("Nat", 3).unwrap();
    let none_report = evaluate_standard_prelude("eval_option_map_none", &option_map, eval_product(none, f.clone()), 1, &[], 3).unwrap();
    assert_eq!(none_report.status, PreludeEvalStatus::Evaluated);
    assert_eq!(none_report.result, Some(eval_option_none("Text", 3).unwrap()));

    let err = eval_result_err("Nat", "Text", eval_text("bad"), 4).unwrap();
    let result_map = prelude_contract(PreludeOperationKind::ResultMap);
    let err_report = evaluate_standard_prelude("eval_result_map_err", &result_map, eval_product(err, f), 1, &[], 4).unwrap();
    assert_eq!(err_report.status, PreludeEvalStatus::Evaluated);
    assert_eq!(err_report.result.as_ref().unwrap().type_key(), "ResultType<Text,Text>");
}

#[test]
fn collection_map_requires_explicit_fuel_and_preserves_length_symbolically() {
    let map = prelude_contract(PreludeOperationKind::ListMap);
    let values = eval_list("Nat", vec![eval_nat(1), eval_nat(2), eval_nat(3)], 1).unwrap();
    let f = eval_function_ref("show", "Nat", "Text", 1).unwrap();
    let input = eval_product(values.clone(), f.clone());

    let rejected = evaluate_standard_prelude("eval_list_map_no_fuel", &map, input, 2, &[], 2).unwrap();
    assert_eq!(rejected.status, PreludeEvalStatus::RejectedFuel);
    assert!(rejected.open_obligations.iter().any(|o| o.contains("requires fuel 3")));
    assert!(require_evaluated_prelude(&rejected, 3).is_err());

    let accepted = evaluate_standard_prelude("eval_list_map", &map, eval_product(values, f), 3, &[], 3).unwrap();
    assert_eq!(accepted.status, PreludeEvalStatus::SymbolicEvaluated);
    assert_eq!(accepted.steps_used, 3);
    let Some(PreludeEvalValue::List { item_type, items }) = accepted.result else { panic!("expected list result") };
    assert_eq!(item_type, "Text");
    assert_eq!(items.len(), 3);
}

#[test]
fn unverified_contracts_and_evidence_values_are_rejected_before_evaluation() {
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
    let downgraded = standard_prelude_contract("std_bool_not", &sig, &contract, None, &[], 3).unwrap();
    let rejected_contract = evaluate_standard_prelude("eval_bad_contract", &downgraded, eval_bool(true), 1, &[], 4).unwrap();
    assert_eq!(rejected_contract.status, PreludeEvalStatus::RejectedContract);

    let add = prelude_contract(PreludeOperationKind::NatAdd);
    let proof_as_value = eval_evidence_boundary("p", "StaticProof<p>");
    let rejected_input = evaluate_standard_prelude("eval_proof_as_value", &add, eval_product(proof_as_value, eval_nat(1)), 1, &[], 5).unwrap();
    assert_eq!(rejected_input.status, PreludeEvalStatus::RejectedInput);
    assert!(rejected_input.open_obligations.iter().any(|o| o.contains("proof/theorem/truth/runtime")));
}

#[test]
fn tainted_sources_downgrade_evaluation_report_without_cleaning_taint() {
    let add = prelude_contract(PreludeOperationKind::NatAdd);
    let unsafe_source = Passport::unsafe_nat("Prelude");
    let report = evaluate_standard_prelude("eval_tainted_add", &add, eval_product(eval_nat(1), eval_nat(2)), 1, &[&unsafe_source], 1).unwrap();
    assert_eq!(report.status, PreludeEvalStatus::Evaluated);
    assert!(report.has_unsafe_taint);
    assert!(report.max_trust >= TrustLevel::Unsafe);
    assert!(report.open_obligations.iter().any(|o| o.contains("Axiom/Oracle/Unsafe")));
    let passport = prelude_evaluation_passport("Prelude", &report, &[&unsafe_source]);
    assert!(passport.trust >= TrustLevel::Unsafe);
}

#[test]
fn evaluation_exports_are_stable_and_operation_sensitive() {
    let add = prelude_contract(PreludeOperationKind::NatAdd);
    let eq = prelude_contract(PreludeOperationKind::NatEq);

    let first = evaluate_standard_prelude("eval_add", &add, eval_product(eval_nat(1), eval_nat(2)), 1, &[], 1).unwrap();
    let first_again = evaluate_standard_prelude("eval_add", &add, eval_product(eval_nat(1), eval_nat(2)), 1, &[], 1).unwrap();
    let second = evaluate_standard_prelude("eval_eq", &eq, eval_product(eval_nat(1), eval_nat(2)), 1, &[], 1).unwrap();

    assert_eq!(first.fingerprint, first_again.fingerprint);
    assert_ne!(first.fingerprint, second.fingerprint);

    let exported = export_prelude_evaluation(&first);
    assert!(exported.contains("prelude_evaluation: v1"));
    assert!(exported.contains("operation: nat.add"));
    assert!(exported.contains("result: Nat(3)"));
    assert!(exported.contains("status: evaluated"));
}
