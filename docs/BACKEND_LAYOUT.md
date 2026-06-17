# DLM v0.70 — Backend Layout / ABI Descriptor Boundary

`v0.70` adds the explicit ABI/layout layer between backend lowering plans and future runtime/compiler artifacts.

The central rule is:

```text
semantic value != arbitrary memory layout
full audit passport != hot runtime payload
```

## Objects

```text
BackendLayoutDescriptor
BackendLayoutReport
AbiScalarKind
LayoutContainerKind
LayoutMetadataPolicy
BackendLayoutStatus
```

## Layout targets

```text
audit_only     -> audit_only_descriptor
interpreter    -> tagged_union/scalar
native_scalar  -> scalar
native_vector  -> dense_array/slice_view
gpu_batch      -> gpu_buffer
remote_batch   -> remote_buffer
```

## Metadata rule

Runtime/hot layouts cannot carry full semantic passports per value or per element.

Rejected in runtime layouts:

```text
full_passport
interleaved_per_element_passport
```

Allowed:

```text
none
compact_descriptor
erased_with_audit_fingerprint
```

## Status model

```text
verified_layout
symbolic_layout
downgraded_tainted
rejected_backend
rejected_target
rejected_abi
```

`symbolic_layout` is not a failure. It means the operation is bounded and descriptor-preserving, but not a fully concrete runtime value transformation.

## Soundness boundaries

`BackendLayoutReport` is not:

```text
Proof
Theorem
TruthClaim
RuntimeWitness
```

It is a backend/runtime descriptor boundary. It may be consumed by future compiler/runtime layers only under explicit capability, layout and taint checks.
