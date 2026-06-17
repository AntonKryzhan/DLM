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
            PreludeOperationKind::BoolNot => "Bool".to_string(),
            PreludeOperationKind::SequenceIndex => "ProductType<SequenceType<Nat>*Nat>".to_string(),
            PreludeOperationKind::ListMap => "ProductType<ListType<Nat>*FunctionType<Nat->Bool>>".to_string(),
            _ => "ProductType<Nat*Nat>".to_string(),
        },
        output_type: match operation {
            PreludeOperationKind::NatEq | PreludeOperationKind::BoolNot => "Bool".to_string(),
            PreludeOperationKind::SequenceIndex => "OptionType<Nat>".to_string(),
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

#[test]
fn native_scalar_lowering_erases_proof_metadata_without_becoming_proof_or_truth() {
    let eval = evaluation_report(
        "nat_add_eval",
        PreludeOperationKind::NatAdd,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Nat(5)),
    );

    let report = lower_prelude_evaluation(
        "nat_add_native",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        10,
    )
    .unwrap();

    assert_eq!(report.status, PreludeLoweringStatus::VerifiedErased);
    assert!(report.proof_erased);
    assert!(report.passport_erased);
    assert!(report.representation.contains("native-scalar-op"));
    assert!(report.descriptor.contains("eval_fp=eval-fp-nat_add_eval"));
    require_verified_lowering(&report, 10).unwrap();

    let passport = prelude_lowering_passport("Std", &report, &[]);
    assert!(matches!(&passport.ty, TypeKind::PreludeLoweringReport { .. }));
    assert!(!matches!(&passport.ty, TypeKind::StaticProof(_)));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(&passport.ty, TypeKind::TruthClaim { .. }));
    assert!(!matches!(&passport.ty, TypeKind::RuntimeWitness(_)));
}

#[test]
fn rejected_evaluation_is_not_lowered_as_value_preserving_runtime_code() {
    let eval = evaluation_report(
        "bad_eval",
        PreludeOperationKind::NatAdd,
        PreludeEvalStatus::RejectedFuel,
        None,
    );

    let report = lower_prelude_evaluation(
        "bad_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        20,
    )
    .unwrap();

    assert_eq!(report.status, PreludeLoweringStatus::RejectedEvaluation);
    assert!(report.open_obligations.iter().any(|item| item.contains("RejectedFuel") || item.contains("rejected_fuel")));
    assert!(require_verified_lowering(&report, 20).is_err());
}

#[test]
fn proof_truth_and_runtime_evidence_cannot_be_erased_into_runtime_artifact() {
    let eval = evaluation_report(
        "evidence_eval",
        PreludeOperationKind::BoolNot,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Evidence {
            description: "fake theorem result".to_string(),
            kind: "Theorem".to_string(),
        }),
    );

    let report = lower_prelude_evaluation(
        "evidence_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        30,
    )
    .unwrap();

    assert_eq!(report.status, PreludeLoweringStatus::RejectedEvidenceBoundary);
    assert!(report.open_obligations.iter().any(|item| item.contains("proof/theorem/truth/runtime")));
}

#[test]
fn gpu_batch_lowering_accepts_collection_symbolic_map_but_rejects_scalar_gpu_launch() {
    let scalar = evaluation_report(
        "scalar_eval",
        PreludeOperationKind::NatAdd,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Nat(3)),
    );
    let rejected = lower_prelude_evaluation(
        "scalar_gpu",
        &scalar,
        PreludeLoweringTarget::GpuBatch,
        PreludeErasureMode::PassportErasedWithDescriptor,
        40,
    )
    .unwrap();
    assert_eq!(rejected.status, PreludeLoweringStatus::RejectedTarget);
    assert!(rejected.open_obligations.iter().any(|item| item.contains("gpu_batch")));

    let mapped = evaluation_report(
        "list_map_eval",
        PreludeOperationKind::ListMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::List {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
    );
    let accepted = lower_prelude_evaluation(
        "list_map_gpu",
        &mapped,
        PreludeLoweringTarget::GpuBatch,
        PreludeErasureMode::PassportErasedWithDescriptor,
        41,
    )
    .unwrap();
    assert_eq!(accepted.status, PreludeLoweringStatus::SymbolicLowered);
    assert!(accepted.representation.contains("gpu-batch-kernel-candidate"));
    assert!(accepted.open_obligations.iter().any(|item| item.contains("symbolic")));
}

#[test]
fn axiom_oracle_unsafe_taint_is_preserved_and_downgrades_clean_lowering() {
    let mut eval = evaluation_report(
        "tainted_eval",
        PreludeOperationKind::NatEq,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Bool(true)),
    );
    eval.max_trust = TrustLevel::Axiom;
    eval.max_provenance = Provenance::BuiltinKnown;
    eval.has_axiom_taint = true;

    let report = lower_prelude_evaluation(
        "tainted_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        50,
    )
    .unwrap();

    assert_eq!(report.status, PreludeLoweringStatus::DowngradedTainted);
    assert_eq!(report.max_trust, TrustLevel::Axiom);
    assert!(report.has_axiom_taint);
    assert!(report.open_obligations.iter().any(|item| item.contains("Axiom/Oracle/Unsafe")));
}

#[test]
fn lowering_exports_are_stable_and_target_sensitive() {
    let eval = evaluation_report(
        "len_eval",
        PreludeOperationKind::SequenceLength,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Nat(2)),
    );

    let scalar = lower_prelude_evaluation(
        "len_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        60,
    )
    .unwrap();
    let interpreter = lower_prelude_evaluation(
        "len_lowering",
        &eval,
        PreludeLoweringTarget::Interpreter,
        PreludeErasureMode::ProofErased,
        60,
    )
    .unwrap();

    let export = export_prelude_lowering(&scalar);
    assert!(export.contains("prelude_lowering: v1"));
    assert!(export.contains("target: native_scalar"));
    assert!(export.contains("proof_erased: true"));
    assert_ne!(scalar.fingerprint, interpreter.fingerprint);
}
