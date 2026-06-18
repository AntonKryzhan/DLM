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
    let backend = backend_capability_contract(format!("{name}_backend"), target, full_caps(target), 2).unwrap();
    validate_backend_lowering(format!("{name}_backend_plan"), &lowering, &backend, 3).unwrap()
}

fn layout_report(
    name: &str,
    target: PreludeLoweringTarget,
    operation: PreludeOperationKind,
    eval_status: PreludeEvalStatus,
    result: Option<PreludeEvalValue>,
    scalar: AbiScalarKind,
    container: LayoutContainerKind,
    element_type: &str,
    element_size: usize,
    alignment: usize,
) -> BackendLayoutReport {
    let plan = backend_plan(name, target, operation, eval_status, result);
    let layout = backend_layout_descriptor(
        format!("{name}_layout"),
        target,
        operation,
        scalar,
        container,
        element_type,
        element_size,
        alignment,
        LayoutMetadataPolicy::CompactDescriptor,
        10,
    )
    .unwrap();
    validate_backend_layout(format!("{name}_layout_report"), &plan, &layout, 11).unwrap()
}

fn nat_scalar_layout(name: &str) -> BackendLayoutReport {
    layout_report(
        name,
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        PreludeEvalStatus::Evaluated,
        Some(PreludeEvalValue::Nat(42)),
        AbiScalarKind::Nat64,
        LayoutContainerKind::Scalar,
        "Nat",
        8,
        8,
    )
}

#[test]
fn scalar_runtime_descriptor_is_dense_boundary_not_runtime_witness() {
    let layout = nat_scalar_layout("nat_add_runtime");
    assert_eq!(layout.status, BackendLayoutStatus::VerifiedLayout);

    let descriptor = dense_runtime_descriptor(
        "nat64_scalar_runtime",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        RuntimeRepresentationKind::ScalarValue,
        RuntimeOwnershipMode::OwnedUnique,
        "Nat",
        1,
        8,
        8,
        8,
        20,
    )
    .unwrap();
    assert_eq!(descriptor.status, DenseRuntimeStatus::VerifiedDense);

    let report = validate_dense_runtime("nat64_dense_report", &layout, &descriptor, 21).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::VerifiedDense);
    assert_eq!(report.byte_len, 8);
    require_verified_dense_runtime(&report, 22).unwrap();
    assert!(report.dense_descriptor.contains("repr=scalar_value"));

    let passport = dense_runtime_report_passport("Runtime", &report, &[]);
    assert!(matches!(&passport.ty, TypeKind::DenseRuntimeReport { .. }));
    assert!(!matches!(&passport.ty, TypeKind::RuntimeWitness(_)));
    assert!(!matches!(&passport.ty, TypeKind::StaticProof(_)));
    assert!(!matches!(&passport.ty, TypeKind::Theorem { .. }));
    assert!(!matches!(&passport.ty, TypeKind::TruthClaim { .. }));
}

#[test]
fn dense_vector_requires_dense_stride_not_scalar_boxing_or_strided_vector() {
    let layout = layout_report(
        "sequence_map_runtime",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::Sequence {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::DenseArray,
        "Bool",
        1,
        16,
    );
    assert_eq!(layout.status, BackendLayoutStatus::SymbolicLayout);

    let scalar_boxed = dense_runtime_descriptor(
        "scalar_boxed_vector_runtime",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        RuntimeRepresentationKind::ScalarValue,
        RuntimeOwnershipMode::OwnedUnique,
        "Bool",
        4,
        1,
        1,
        16,
        30,
    )
    .unwrap();
    assert_eq!(scalar_boxed.status, DenseRuntimeStatus::RejectedRepresentation);

    let strided_dense = dense_runtime_descriptor(
        "strided_dense_vector_runtime",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        RuntimeRepresentationKind::DenseVector,
        RuntimeOwnershipMode::OwnedUnique,
        "Bool",
        4,
        1,
        2,
        16,
        31,
    )
    .unwrap();
    assert_eq!(strided_dense.status, DenseRuntimeStatus::RejectedRepresentation);
    assert!(strided_dense.open_obligations.iter().any(|o| o.contains("stride equal")));

    let dense = dense_runtime_descriptor(
        "dense_vector_runtime",
        PreludeLoweringTarget::NativeVector,
        PreludeOperationKind::SequenceMap,
        RuntimeRepresentationKind::DenseVector,
        RuntimeOwnershipMode::OwnedUnique,
        "Bool",
        4,
        1,
        1,
        16,
        32,
    )
    .unwrap();
    let report = validate_dense_runtime("dense_vector_report", &layout, &dense, 33).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::SymbolicDense);
    assert_eq!(report.byte_len, 4);
    assert!(require_verified_dense_runtime(&report, 34).is_err());
}

#[test]
fn gpu_runtime_requires_gpu_region_and_gpu_resident_handle() {
    let layout = layout_report(
        "gpu_list_map_runtime",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::ListMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::List {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::GpuBuffer,
        "Bool",
        1,
        4,
    );
    assert_eq!(layout.status, BackendLayoutStatus::SymbolicLayout);

    let wrong_ownership = dense_runtime_descriptor(
        "gpu_region_bad_owner",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::ListMap,
        RuntimeRepresentationKind::GpuRegion,
        RuntimeOwnershipMode::OwnedUnique,
        "Bool",
        8,
        1,
        1,
        4,
        40,
    )
    .unwrap();
    assert_eq!(wrong_ownership.status, DenseRuntimeStatus::RejectedOwnership);

    let gpu_region = dense_runtime_descriptor(
        "gpu_region_runtime",
        PreludeLoweringTarget::GpuBatch,
        PreludeOperationKind::ListMap,
        RuntimeRepresentationKind::GpuRegion,
        RuntimeOwnershipMode::GpuResidentHandle,
        "Bool",
        8,
        1,
        1,
        4,
        41,
    )
    .unwrap();
    let report = validate_dense_runtime("gpu_dense_report", &layout, &gpu_region, 42).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::SymbolicDense);
    assert!(report.dense_descriptor.contains("repr=gpu_region"));
    assert!(report.open_obligations.iter().any(|o| o.contains("symbolic bounded")));
}

#[test]
fn representation_must_match_backend_layout_container() {
    let layout = nat_scalar_layout("nat_add_representation_mismatch");
    let descriptor = dense_runtime_descriptor(
        "wrong_dense_vector_for_scalar_layout",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        RuntimeRepresentationKind::DenseVector,
        RuntimeOwnershipMode::OwnedUnique,
        "Nat",
        1,
        8,
        8,
        8,
        50,
    )
    .unwrap();
    assert_eq!(descriptor.status, DenseRuntimeStatus::RejectedRepresentation);

    let mut descriptor = descriptor;
    descriptor.status = DenseRuntimeStatus::VerifiedDense;
    let report = validate_dense_runtime("mismatched_runtime_report", &layout, &descriptor, 51).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::RejectedRepresentation);
    assert!(report.open_obligations.iter().any(|o| o.contains("cannot be represented")));
}

#[test]
fn tainted_layout_downgrades_dense_runtime_without_cleaning_taint() {
    let mut layout = nat_scalar_layout("tainted_nat_runtime");
    layout.has_axiom_taint = true;
    layout.max_trust = TrustLevel::Axiom;
    layout.max_provenance = Provenance::BuiltinKnown;
    layout.status = BackendLayoutStatus::DowngradedTainted;

    let descriptor = dense_runtime_descriptor(
        "tainted_nat64_runtime",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        RuntimeRepresentationKind::ScalarValue,
        RuntimeOwnershipMode::OwnedUnique,
        "Nat",
        1,
        8,
        8,
        8,
        60,
    )
    .unwrap();
    let report = validate_dense_runtime("tainted_dense_report", &layout, &descriptor, 61).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::DowngradedTainted);
    assert!(report.has_axiom_taint);
    assert_eq!(report.max_trust, TrustLevel::Axiom);
    assert!(require_verified_dense_runtime(&report, 62).is_err());
}

#[test]
fn remote_runtime_uses_remote_region_and_remote_handle() {
    let layout = layout_report(
        "remote_sequence_map_runtime",
        PreludeLoweringTarget::RemoteBatch,
        PreludeOperationKind::SequenceMap,
        PreludeEvalStatus::SymbolicEvaluated,
        Some(PreludeEvalValue::Sequence {
            item_type: "Bool".to_string(),
            items: vec![PreludeEvalValue::Symbolic { expr: "f(1)".to_string(), ty: "Bool".to_string() }],
        }),
        AbiScalarKind::OpaqueSymbolic,
        LayoutContainerKind::RemoteBuffer,
        "Bool",
        1,
        8,
    );
    let descriptor = dense_runtime_descriptor(
        "remote_bool_region_runtime",
        PreludeLoweringTarget::RemoteBatch,
        PreludeOperationKind::SequenceMap,
        RuntimeRepresentationKind::RemoteRegion,
        RuntimeOwnershipMode::RemoteHandle,
        "Bool",
        16,
        1,
        1,
        8,
        70,
    )
    .unwrap();
    let report = validate_dense_runtime("remote_dense_report", &layout, &descriptor, 71).unwrap();
    assert_eq!(report.status, DenseRuntimeStatus::SymbolicDense);
    assert_eq!(report.byte_len, 16);
    assert!(report.dense_descriptor.contains("ownership=remote_handle"));
}

#[test]
fn dense_runtime_exports_are_stable_and_descriptor_sensitive() {
    let layout = nat_scalar_layout("export_nat_runtime");
    let descriptor = dense_runtime_descriptor(
        "export_nat64_runtime",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        RuntimeRepresentationKind::ScalarValue,
        RuntimeOwnershipMode::OwnedUnique,
        "Nat",
        1,
        8,
        8,
        8,
        80,
    )
    .unwrap();
    let report = validate_dense_runtime("export_dense_report", &layout, &descriptor, 81).unwrap();

    let descriptor_text_1 = export_dense_runtime_descriptor(&descriptor);
    let descriptor_text_2 = export_dense_runtime_descriptor(&descriptor);
    assert_eq!(descriptor_text_1, descriptor_text_2);
    assert!(descriptor_text_1.contains("dense_runtime_descriptor: v1"));
    assert!(descriptor_text_1.contains("representation: scalar_value"));

    let report_text = export_dense_runtime_report(&report);
    assert!(report_text.contains("dense_runtime_report: v1"));
    assert!(report_text.contains("byte_len: 8"));

    let changed = dense_runtime_descriptor(
        "export_nat64_runtime_changed_stride",
        PreludeLoweringTarget::NativeScalar,
        PreludeOperationKind::NatAdd,
        RuntimeRepresentationKind::ScalarValue,
        RuntimeOwnershipMode::OwnedUnique,
        "Nat",
        1,
        8,
        16,
        8,
        82,
    )
    .unwrap();
    assert_ne!(descriptor.fingerprint, changed.fingerprint);
}
