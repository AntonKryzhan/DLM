# Dense Runtime Descriptor Boundary

`v0.71` adds the first dense runtime descriptor layer for the Stage 2 ordinary-mathematics lowering chain.

The new pipeline is:

```text
BackendLayoutReport
        +
DenseRuntimeDescriptor
        ->
DenseRuntimeReport
```

This layer fixes a strict boundary between semantic value descriptions and hot runtime payloads.

## Core law

```text
Semantic value != arbitrary memory region.
Passport/audit/proof metadata != hot runtime payload.
Dense runtime descriptor != RuntimeWitness.
```

A dense runtime descriptor is a compact, auditable description of how a checked value may be represented at runtime. It is not a proof, theorem, truth claim, or runtime witness.

## Runtime representations

```text
scalar_value
 tagged_value
 dense_vector
 slice_view
 gpu_region
 remote_region
 audit_descriptor_only
```

The representation must match the layout container:

```text
scalar       -> scalar_value
tagged_union -> tagged_value
dense_array  -> dense_vector
slice_view   -> slice_view
gpu_buffer   -> gpu_region
remote_buffer -> remote_region
audit_only_descriptor -> audit_descriptor_only
```

## Ownership modes

```text
owned_unique
borrowed_read_only
shared_immutable
gpu_resident_handle
remote_handle
audit_only
```

GPU and remote targets require explicit handle-style ownership. They may not silently become ordinary owned CPU values.

## Dense constraints

The descriptor rejects:

- zero-sized runtime payloads;
- zero elements outside audit-only mode;
- non-power-of-two alignment;
- strides smaller than element size;
- dense_vector with stride different from element size;
- scalar representations with more than one element;
- target/operation/layout mismatches;
- representation/container mismatches;
- full-passport or per-element passport metadata in hot runtime paths.

## Statuses

```text
verified_dense
symbolic_dense
downgraded_tainted
rejected_layout
rejected_representation
rejected_ownership
```

Symbolic bounded computations, such as `list.map`, `sequence.map`, `list.fold`, and `sequence.fold`, remain `symbolic_dense` unless later stages prove a concrete executable function body. This prevents hidden execution of arbitrary user code.

## Taint preservation

A dense runtime descriptor never cleans taint. If the upstream layout plan has Axiom/Oracle/Unsafe taint, the dense runtime report is downgraded and keeps that taint visible.

## New Rust surface

```text
RuntimeRepresentationKind
RuntimeOwnershipMode
DenseRuntimeStatus
DenseRuntimeDescriptor
DenseRuntimeReport

dense_runtime_descriptor(...)
validate_dense_runtime(...)
require_verified_dense_runtime(...)
dense_runtime_descriptor_passport(...)
dense_runtime_report_passport(...)
export_dense_runtime_descriptor(...)
export_dense_runtime_report(...)
```

## Diagnostic

```text
E0942 DenseRuntimeError
```
