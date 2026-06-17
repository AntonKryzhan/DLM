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
            PreludeOperationKind::ListMap | PreludeOperationKind::SequenceMap => {
                "ProductType<ListType<Nat>*FunctionType<Nat->Bool>>".to_string()
            }
            _ => "ProductType<Nat*Nat>".to_string(),
        },
        output_type: match operation {
            PreludeOperationKind::NatEq => "Bool".to_string(),
            PreludeOperationKind::ListMap | PreludeOperationKind::SequenceMap => "ListType<Bool>".to_string(),
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

fn full_caps(target: PreludeLoweringTarget) -> Vec<BackendCapability> {
    required_backend_capabilities(target).into_iter().collect()
}

fn backend_plan(
    name: &str,
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    status: PreludeEvalStatus,
    result: Option<PreludeEvalValue>,
) -> BackendLoweringReport {
    let eval = evaluation_report(name, operation, status, result);
    let lowering = lower_prelude_evaluation(
        format!("{name}_lowering"),
        &eval,
        target,
        PreludeErasureMode::PassportErasedWithDescriptor,
        1,
    )
    .unwrap();
    let backend = backend_capability_contract(
        format!("{name}_backend"),
        target,
        full_caps(target),
        2,
    )
    .unwrap();
    validate_backend_lowering(format!("{name}_backend_plan"), &lowering, &backend, 3).unwrap()
}

fn nat_add_native_plan(name: &str) -> BackendLoweringReport {
    backend_plan(
        name,
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Nat(42)),
    )
}

#[test]
fn native_scalar_layout_is_explicit_abi_descriptor_not_proof_or_runtime_witness() {
    let plan = nat_add_native_plan("nat_add_layout");
    assert_eq!(plan.status, BackendLoweringStatus::VerifiedAccepted);

    let layout = backend_layout_descriptor(
        "nat64_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        8,
        LayoutMetadataPolicy::ErasedWithAuditFingerprint,
        10,
    )
    .unwrap();
    assert_eq!(layout.status, BackendLayoutStatus::VerifiedLayout);

    let report = validate_backend_layout("nat_add_abi_plan", &plan, &layout, 11).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::VerifiedLayout);
    require_verified_backend_layout(&report, 12).unwrap();
    assert!(report.runtime_descriptor.contains("abi=nat64"));
    assert!(report.runtime_descriptor.contains("metadata=erased_with_audit_fingerprint"));

    let passport = backend_layout_report_passport("Backend", &report, &[]);
    assert!(matches!(&passport.ty, TypeKind::BackendLayoutReport { .. }));
    assert!(!matches!(&passport.ty, TypeKind::StaticProof(_)));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(&passport.ty, TypeKind::TruthClaim { .. }));
    assert!(!matches!(&passport.ty, TypeKind::RuntimeWitness(_)));
}

#[test]
fn full_passport_or_per_element_metadata_is_rejected_for_runtime_layout() {
    let plan = nat_add_native_plan("nat_add_bad_metadata");
    let layout = backend_layout_descriptor(
        "bad_full_passport_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        8,
        LayoutMetadataPolicy::FullPassport,
        20,
    )
    .unwrap();

    assert_eq!(layout.status, BackendLayoutStatus::RejectedAbi);
    assert!(layout.open_obligations.iter().any(|o| o.contains("compact descriptor")));

    let report = validate_backend_layout("bad_metadata_plan", &plan, &layout, 21).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::RejectedAbi);
    assert!(require_verified_backend_layout(&report, 22).is_err());
}

#[test]
fn target_and_operation_mismatches_are_rejected_before_runtime_descriptor_acceptance() {
    let plan = nat_add_native_plan("nat_add_mismatch");
    let target_mismatch = backend_layout_descriptor(
        "gpu_layout_for_scalar_plan",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::GpuBuffer,
        "Nat",
        8,
        8,
        LayoutMetadataPolicy::CompactDescriptor,
        30,
    )
    .unwrap();
    let report = validate_backend_layout("target_mismatch_layout_plan", &plan, &target_mismatch, 31).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::RejectedTarget);
    assert!(report.open_obligations.iter().any(|o| o.contains("targets")));

    let op_mismatch = backend_layout_descriptor(
        "bool_layout_for_nat_add",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::BoolNot,
        AbiScalarKind::Bool1,
        LayoutContainerKind::Scalar,
        "Bool",
        1,
        1,
        LayoutMetadataPolicy::CompactDescriptor,
        32,
    )
    .unwrap();
    let report = validate_backend_layout("operation_mismatch_layout_plan", &plan, &op_mismatch, 33).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::RejectedTarget);
    assert!(report.open_obligations.iter().any(|o| o.contains("is for")));
}

#[test]
fn gpu_batch_layout_requires_gpu_buffer_and_preserves_symbolic_status() {
    let plan = backend_plan(
        "list_map_gpu_layout",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::ListMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::List {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
    );
    assert_eq!(plan.status, BackendLoweringStatus::SymbolicAccepted);

    let layout = backend_layout_descriptor(
        "gpu_bool_dense_layout",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::ListMap,
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::GpuBuffer,
        "Bool",
        1,
        4,
        LayoutMetadataPolicy::CompactDescriptor,
        40,
    )
    .unwrap();
    assert_eq!(layout.status, BackendLayoutStatus::SymbolicLayout);

    let report = validate_backend_layout("gpu_layout_report", &plan, &layout, 41).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::SymbolicLayout);
    assert!(report.runtime_descriptor.contains("container=gpu_buffer"));
    assert!(report.open_obligations.iter().any(|o| o.contains("symbolic bounded")));
    assert!(require_verified_backend_layout(&report, 42).is_err());
}

#[test]
fn native_vector_layout_requires_dense_or_slice_container_not_scalar_boxing() {
    let plan = backend_plan(
        "sequence_map_vector_layout",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::Sequence {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
    );
    assert_eq!(plan.status, BackendLoweringStatus::SymbolicAccepted);

    let scalar_boxed = backend_layout_descriptor(
        "scalar_layout_for_vector_map",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::Scalar,
        "Bool",
        1,
        16,
        LayoutMetadataPolicy::CompactDescriptor,
        50,
    )
    .unwrap();
    assert_eq!(scalar_boxed.status, BackendLayoutStatus::RejectedAbi);

    let dense = backend_layout_descriptor(
        "dense_vector_bool_layout",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::DenseArray,
        "Bool",
        1,
        16,
        LayoutMetadataPolicy::CompactDescriptor,
        51,
    )
    .unwrap();
    let report = validate_backend_layout("dense_vector_layout_report", &plan, &dense, 52).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::SymbolicLayout);
    assert!(report.runtime_descriptor.contains("container=dense_array"));
}

#[test]
fn tainted_backend_plan_downgrades_layout_without_cleaning_taint() {
    let mut eval = evaluation_report(
        "tainted_nat_eq_layout",
        PreludeOperationKind::NatEq,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Bool(true)),
    );
    eval.max_trust = TrustLevel::Axiom;
    eval.max_provenance = Provenance::BuiltinKnown;
    eval.has_axiom_taint = true;
    let lowering = lower_prelude_evaluation(
        "tainted_nat_eq_lowering",
        &eval,
        PreludeLoweringTarget::NativeScalar,
        PreludeErasureMode::PassportErasedWithDescriptor,
        60,
    )
    .unwrap();
    let backend = backend_capability_contract(
        "tainted_scalar_backend",
        PreludeLoweringTarget::NativeScalar,
        full_caps(PreludeLoweringTarget::NativeScalar),
        61,
    )
    .unwrap();
    let plan = validate_backend_lowering("tainted_backend_plan", &lowering, &backend, 62).unwrap();
    assert_eq!(plan.status, BackendLoweringStatus::DowngradedTainted);

    let layout = backend_layout_descriptor(
        "bool_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatEq,
        AbiScalarKind::Bool1,
        LayoutContainerKind::Scalar,
        "Bool",
        1,
        1,
        LayoutMetadataPolicy::ErasedWithAuditFingerprint,
        63,
    )
    .unwrap();
    let report = validate_backend_layout("tainted_layout_report", &plan, &layout, 64).unwrap();
    assert_eq!(report.status, BackendLayoutStatus::DowngradedTainted);
    assert!(report.has_axiom_taint);
    assert_eq!(report.max_trust, TrustLevel::Axiom);
    assert!(report.open_obligations.iter().any(|o| o.contains("Axiom/Oracle/Unsafe")));
}

#[test]
fn layout_exports_are_stable_and_abi_sensitive() {
    let plan = nat_add_native_plan("nat_add_export_layout");
    let layout = backend_layout_descriptor(
        "nat64_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        8,
        LayoutMetadataPolicy::CompactDescriptor,
        70,
    )
    .unwrap();
    let layout_again = backend_layout_descriptor(
        "nat64_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        8,
        LayoutMetadataPolicy::CompactDescriptor,
        70,
    )
    .unwrap();
    let different_alignment = backend_layout_descriptor(
        "nat64_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        4,
        LayoutMetadataPolicy::CompactDescriptor,
        70,
    )
    .unwrap();

    assert_eq!(layout.fingerprint, layout_again.fingerprint);
    assert_ne!(layout.stable_abi_hash, different_alignment.stable_abi_hash);

    let report = validate_backend_layout("export_layout_report", &plan, &layout, 71).unwrap();
    let descriptor_text = export_backend_layout_descriptor(&layout);
    let report_text = export_backend_layout_report(&report);

    assert!(descriptor_text.contains("backend_layout_descriptor: v1"));
    assert!(descriptor_text.contains("stable_abi_hash:"));
    assert!(report_text.contains("backend_layout_report: v1"));
    assert!(report_text.contains("runtime_descriptor:"));
    assert!(report_text.contains("status: verified_layout"));
}
