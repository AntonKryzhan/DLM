use dlm_core::*;

fn evaluation_report(
    name: &str,
    operation: PreludeOperationKind,
    status: PreludeEvalStatus,
    result: Option<PreludeEvalValue>,
) -> PreludeEvaluationReport {
    PreludeEvaluationReport {
        name: name.to_string(),
        operation,
        contract: format!("{name}_contract"),
        input_type: match operation {
            PreludeOperationKind::ListMap => "ProductType<ListType<Nat>*FunctionType<Nat->Bool>>".to_string(),
            _ => "ProductType<Nat*Nat>".to_string(),
        },
        output_type: match operation {
            PreludeOperationKind::NatEq => "Bool".to_string(),
            PreludeOperationKind::ListMap => "ListType<Bool>".to_string(),
            _ => "Nat".to_string(),
        },
        input_render: format!("input:{name}"),
        result,
        status,
        steps_used: 1,
        fuel_limit: 8,
        open_obligations: Vec::new(),
        max_trust: TrustLevel::Checked,
        max_provenance: Provenance::InternalDerived,
        has_axiom_taint: false,
        has_oracle_taint: false,
        has_unsafe_taint: false,
        fingerprint: format!("eval-fp-{name}"),
    }
}

fn native_scalar_lowering(name: &str) -> PreludeLoweringReport {
    let eval = evaluation_report(name, PreludeOperationKind::NatAdd, PreludeEvalStatus::Evaluated, Some(PreludeEvalValue::Nat(42)));
    lower_prelude_evaluation(
        format!("{name}_lowering"),
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        1,
    )
    .unwrap()
}

fn full_caps(target: PreludeLoweringTarget) -> Vec<BackendCapability> {
    required_backend_capabilities(target).into_iter().collect()
}

#[test]
fn native_scalar_backend_accepts_verified_erased_lowering_without_becoming_proof() {
    let lowering = native_scalar_lowering("nat_add_eval");
    assert_eq!(lowering.status, PreludeLoweringStatus::VerifiedErased);

    let backend = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        10,
    )
    .unwrap();
    assert_eq!(backend.status, BackendCapabilityStatus::Verified);

    let plan = validate_backend_lowering("x86_nat_add_plan", &lowering, &backend, 11).unwrap();
    assert_eq!(plan.status, BackendLoweringStatus::VerifiedAccepted);
    require_verified_backend_lowering(&plan, 12).unwrap();
    assert!(plan.representation.contains("native_scalar"));
    assert!(plan.descriptor.contains("lower_fp="));

    let passport = backend_lowering_report_passport("Backend", &plan, &[]);
    assert!(matches!(&passport.ty, TypeKind::BackendLoweringReport { .. }));
    assert!(!matches!(&passport.ty, TypeKind::StaticProof(_)));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(&passport.ty, TypeKind::TruthClaim { .. }));
    assert!(!matches!(&passport.ty, TypeKind::RuntimeWitness(_)));
}

#[test]
fn missing_backend_capability_rejects_contract_and_plan() {
    let lowering = native_scalar_lowering("nat_add_missing_caps");
    let backend = backend_capability_contract(
        "broken_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        vec![BackendCapability::Deterministic, BackendCapability::Pure],
        20,
    )
    .unwrap();

    assert_eq!(backend.status, BackendCapabilityStatus::RejectedCapability);
    assert!(backend.missing_capabilities.contains(&BackendCapability::NoAlloc));
    assert!(backend.missing_capabilities.contains(&BackendCapability::ValuePreserving));

    let plan = validate_backend_lowering("broken_scalar_plan", &lowering, &backend, 21).unwrap();
    assert_eq!(plan.status, BackendLoweringStatus::RejectedCapability);
    assert!(plan.open_obligations.iter().any(|o| o.contains("missing required capability")));
    assert!(require_verified_backend_lowering(&plan, 22).is_err());
}

#[test]
fn gpu_batch_backend_accepts_symbolic_collection_lowering_but_keeps_it_visible() {
    let eval = evaluation_report(
        "list_map_eval",
        PreludeOperationKind::ListMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::List {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
    );
    let lowering = lower_prelude_evaluation(
        "list_map_gpu_lowering",
        &eval,
        PreludeLoweringTarget::GpuBatch,
        PreludeErasureMode::PassportErasedWithDescriptor,
        30,
    )
    .unwrap();
    assert_eq!(lowering.status, PreludeLoweringStatus::SymbolicLowered);

    let backend = backend_capability_contract(
        "cuda_batch_backend",
        PreludeLoweringTarget::GpuBatch,
        full_caps(PreludeLoweringTarget::GpuBatch),
        31,
    )
    .unwrap();
    let plan = validate_backend_lowering("cuda_list_map_plan", &lowering, &backend, 32).unwrap();

    assert_eq!(plan.status, BackendLoweringStatus::SymbolicAccepted);
    assert!(plan.accepted_capabilities.contains(&BackendCapability::Batchable));
    assert!(plan.accepted_capabilities.contains(&BackendCapability::GpuResident));
    assert!(plan.open_obligations.iter().any(|o| o.contains("symbolic bounded lowering")));
    assert!(require_verified_backend_lowering(&plan, 33).is_err());
}

#[test]
fn target_mismatch_is_rejected_even_when_backend_has_good_capabilities() {
    let lowering = native_scalar_lowering("nat_add_target_mismatch");
    let backend = backend_capability_contract(
        "cuda_backend_for_wrong_target",
        PreludeLoweringTarget::GpuBatch,
        full_caps(PreludeLoweringTarget::GpuBatch),
        40,
    )
    .unwrap();

    let plan = validate_backend_lowering("target_mismatch_plan", &lowering, &backend, 41).unwrap();
    assert_eq!(plan.status, BackendLoweringStatus::RejectedTarget);
    assert!(plan.open_obligations.iter().any(|o| o.contains("targets")));
}

#[test]
fn tainted_lowering_is_preserved_as_downgraded_backend_plan() {
    let mut eval = evaluation_report(
        "tainted_eval",
        PreludeOperationKind::NatEq,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Bool(true)),
    );
    eval.max_trust = TrustLevel::Axiom;
    eval.max_provenance = Provenance::BuiltinKnown;
    eval.has_axiom_taint = true;

    let lowering = lower_prelude_evaluation(
        "tainted_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        50,
    )
    .unwrap();
    assert_eq!(lowering.status, PreludeLoweringStatus::DowngradedTainted);

    let backend = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        51,
    )
    .unwrap();
    let plan = validate_backend_lowering("tainted_backend_plan", &lowering, &backend, 52).unwrap();

    assert_eq!(plan.status, BackendLoweringStatus::DowngradedTainted);
    assert!(plan.has_axiom_taint);
    assert_eq!(plan.max_trust, TrustLevel::Axiom);
    assert!(plan.open_obligations.iter().any(|o| o.contains("Axiom/Oracle/Unsafe")));
}

#[test]
fn rejected_lowering_is_not_accepted_by_backend_contract() {
    let eval = evaluation_report("bad_eval", PreludeOperationKind::NatAdd, PreludeEvalStatus::RejectedFuel, None);
    let lowering = lower_prelude_evaluation(
        "bad_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        60,
    )
    .unwrap();
    assert_eq!(lowering.status, PreludeLoweringStatus::RejectedEvaluation);

    let backend = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        61,
    )
    .unwrap();
    let plan = validate_backend_lowering("bad_backend_plan", &lowering, &backend, 62).unwrap();

    assert_eq!(plan.status, BackendLoweringStatus::RejectedLowering);
    assert!(plan.open_obligations.iter().any(|o| o.contains("cannot preserve value semantics")));
}

#[test]
fn backend_exports_are_stable_and_capability_sensitive() {
    let lowering = native_scalar_lowering("nat_add_export");
    let verified_backend = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        70,
    )
    .unwrap();
    let verified_backend_again = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        70,
    )
    .unwrap();
    let broken_backend = backend_capability_contract(
        "x86_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        vec![BackendCapability::Deterministic, BackendCapability::Pure],
        70,
    )
    .unwrap();

    assert_eq!(verified_backend.fingerprint, verified_backend_again.fingerprint);
    assert_ne!(verified_backend.fingerprint, broken_backend.fingerprint);

    let plan = validate_backend_lowering("export_plan", &lowering, &verified_backend, 71).unwrap();
    let exported_contract = export_backend_capability_contract(&verified_backend);
    let exported_plan = export_backend_lowering_report(&plan);

    assert!(exported_contract.contains("backend_capability_contract: v1"));
    assert!(exported_contract.contains("target: native_scalar"));
    assert!(exported_plan.contains("backend_lowering_report: v1"));
    assert!(exported_plan.contains("status: verified_accepted"));
    assert!(exported_plan.contains("deterministic"));
}
