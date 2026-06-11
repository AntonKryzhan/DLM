use dlm_core::*;

fn proof_evidence() -> Passport {
    let term = Passport::proof_term("Meta", "budget_intro", None);
    Passport::static_proof("Meta", "budget_proof", &term)
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

fn verified_map(len: usize) -> MapTraversalReport {
    let nat = Passport::literal_nat("Core");
    let seq_ty = sequence_type("Nat", &[&nat], 1).unwrap();
    let items: Vec<&Passport> = std::iter::repeat(&nat).take(len).collect();
    let seq = sequence_value(&seq_ty, &items, 2).unwrap();
    let contract = verified_contract("nat_to_nat", "Nat", "Nat");
    map_sequence(&seq, &contract, "Nat", len, 3).unwrap()
}

fn verified_recursion_call() -> (RecursionSchemeReport, RecursiveCallReport) {
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
    let call = recursive_call(&scheme, &nat, "Nat", 4, 3, 2, 4).unwrap();
    (scheme, call)
}

#[test]
fn unified_budget_accepts_rewrite_traversal_and_recursion_without_becoming_proof() {
    let rewrite = normalize_with_rewrite_rules("Core", "x", &[], 4, 1).unwrap();
    let map = verified_map(2);
    let (scheme, call) = verified_recursion_call();
    let contract = computation_budget_contract("core_budget", 1, 2, 1, 4, 5).unwrap();

    let report = unify_termination_budget(
        &contract,
        &[&rewrite],
        &[&map],
        &[],
        &[&scheme],
        &[&call],
        6,
    )
    .unwrap();
    assert_eq!(report.status, TerminationBudgetStatus::VerifiedUnified);
    assert_eq!(report.rewrite_used, 0);
    assert_eq!(report.traversal_used, 2);
    assert_eq!(report.recursion_used, 1);
    assert_eq!(report.total_used, 3);
    require_verified_unified_budget(&report, 7).unwrap();

    let passport = termination_budget_report_passport("Core", &report, &[]);
    assert!(matches!(passport.ty, TypeKind::TerminationBudgetReport { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::TruthClaim { .. }));
}

#[test]
fn domain_and_total_limits_reject_exceeded_budgets() {
    let map = verified_map(2);
    let tight_traversal = computation_budget_contract("tight_traversal", 0, 1, 1, 2, 1).unwrap();
    let report = unify_termination_budget(&tight_traversal, &[], &[&map], &[], &[], &[], 2).unwrap();
    assert_eq!(report.status, TerminationBudgetStatus::RejectedBudgetExceeded);
    assert!(report.open_obligations.iter().any(|o| o.contains("traversal used 2")));
    assert!(require_verified_unified_budget(&report, 3).is_err());
}

#[test]
fn incoherent_budget_contract_is_rejected_before_use() {
    let err = computation_budget_contract("bad_contract", 3, 3, 3, 4, 1).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TerminationBudgetError);

    let err = computation_budget_contract("bad name", 0, 0, 0, 1, 2).unwrap_err();
    assert_eq!(err.kind, DiagnosticKind::TerminationBudgetError);
}

#[test]
fn open_and_downgraded_computations_remain_visible_in_unified_budget() {
    let nat = Passport::literal_nat("Core");
    let contract = verified_contract("nat_rec_step", "Nat", "Nat");
    let fuel_only = recursion_scheme(
        "fuel_only_rec",
        &contract,
        RecursionMeasureKind::FuelOnly,
        4,
        &[],
        1,
    )
    .unwrap();
    let open_call = recursive_call(&fuel_only, &nat, "Nat", 5, 5, 2, 2).unwrap();
    let budget = computation_budget_contract("open_budget", 0, 0, 1, 1, 3).unwrap();
    let report = unify_termination_budget(&budget, &[], &[], &[], &[&fuel_only], &[&open_call], 4).unwrap();
    assert_eq!(report.status, TerminationBudgetStatus::Open);
    assert!(report.open_obligations.iter().any(|o| o.contains("fuel-only")));

    let fty = function_type("Nat", "Nat", false, false, &[&nat], 5).unwrap();
    let fpass = function_type_passport("Core", &fty, &[&nat]);
    let effect = function_effect(FunctionEffectKind::Runtime, "runtime_counter", 6).unwrap();
    let effectful = function_contract(
        "effectful_map",
        &fpass,
        FunctionPurity::Effectful,
        FunctionTotality::Partial,
        vec![effect],
        &[],
        7,
    )
    .unwrap();
    let seq_ty = sequence_type("Nat", &[&nat], 8).unwrap();
    let seq = sequence_value(&seq_ty, &[&nat], 9).unwrap();
    let map = map_sequence(&seq, &effectful, "Nat", 1, 10).unwrap();
    let budget = computation_budget_contract("downgraded_budget", 0, 1, 0, 1, 11).unwrap();
    let report = unify_termination_budget(&budget, &[], &[&map], &[], &[], &[], 12).unwrap();
    assert_eq!(report.status, TerminationBudgetStatus::Downgraded);
}

#[test]
fn tainted_inputs_downgrade_unified_budget_and_preserve_taint() {
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
    let contract = verified_contract("nat_id", "Nat", "Nat");
    let map = map_sequence(&seq, &contract, "Nat", 1, 3).unwrap();
    let budget = computation_budget_contract("tainted_budget", 0, 1, 0, 1, 4).unwrap();
    let report = unify_termination_budget(&budget, &[], &[&map], &[], &[], &[], 5).unwrap();

    assert_eq!(report.status, TerminationBudgetStatus::Downgraded);
    assert!(report.has_oracle_taint);
    assert_eq!(report.max_trust, TrustLevel::Oracle);
}

#[test]
fn budget_exports_are_stable_and_order_sensitive() {
    let one = verified_map(1);
    let two = verified_map(2);
    let budget_one = computation_budget_contract("budget_one", 0, 1, 0, 1, 1).unwrap();
    let budget_two = computation_budget_contract("budget_two", 0, 2, 0, 2, 2).unwrap();
    let report_one = unify_termination_budget(&budget_one, &[], &[&one], &[], &[], &[], 3).unwrap();
    let report_two = unify_termination_budget(&budget_two, &[], &[&two], &[], &[], &[], 4).unwrap();

    assert_eq!(export_termination_budget_report(&report_one), export_termination_budget_report(&report_one));
    assert_ne!(report_one.fingerprint, report_two.fingerprint);
    assert!(export_termination_budget_report(&report_one).contains("termination_budget_report: v1"));
    assert!(export_termination_budget_report(&report_one).contains("budget_uses:"));
}

#[test]
fn budget_contract_passport_is_a_contract_not_theorem_or_runtime_witness() {
    let budget = computation_budget_contract("passport_budget", 1, 1, 1, 3, 1).unwrap();
    let passport = computation_budget_passport("Core", &budget);
    assert!(matches!(passport.ty, TypeKind::ComputationBudget { .. }));
    assert!(!matches!(passport.ty, TypeKind::Theorem { .. } | TypeKind::StaticProof(_) | TypeKind::RuntimeWitness(_)));
    assert_eq!(passport.trust, TrustLevel::Checked);
}
