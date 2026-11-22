# v0.69 — Backend Capability / Lowering Target Contracts

`v0.69` adds the first explicit backend capability contract layer between prelude lowering and future runtime/compiler backends.

The layer exists because a lowered prelude artifact is not automatically safe for every backend. A backend must explicitly declare operational capabilities before it can consume a lowering report.

## Main law

```text
BackendCapabilityContract != Proof
BackendCapabilityContract != Theorem
BackendCapabilityContract != TruthClaim
BackendCapabilityContract != RuntimeWitness
BackendLoweringReport != hidden backend execution
```

A backend plan is an audit/control object. It records whether a lowered prelude artifact may be consumed by a backend target, and which capabilities justify that decision.

## Capabilities

```text
deterministic
pure
no_alloc
no_alias
vectorizable
batchable
gpu_resident
remote_serializable
value_preserving
descriptor_preserving
```

## Target requirements

```text
audit_only    -> deterministic + descriptor_preserving
interpreter   -> deterministic + pure + value_preserving
native_scalar -> deterministic + pure + no_alloc + value_preserving
native_vector -> deterministic + pure + no_alloc + no_alias + vectorizable + value_preserving
gpu_batch     -> deterministic + pure + no_alloc + no_alias + batchable + gpu_resident + descriptor_preserving
remote_batch  -> deterministic + pure + no_alloc + batchable + remote_serializable + descriptor_preserving
```

## Statuses

Backend contract status:

```text
verified
rejected_capability
```

Backend lowering status:

```text
verified_accepted
symbolic_accepted
downgraded_tainted
rejected_lowering
rejected_target
rejected_capability
```

## Boundary rules

```text
A target mismatch is rejected.
Missing backend capability is rejected.
Rejected prelude lowering is rejected.
Symbolic lowering remains visible as symbolic_accepted, not verified_accepted.
Axiom/Oracle/Unsafe taint remains visible as downgraded_tainted.
No backend can silently clean trust taint.
No backend can turn proof/truth/runtime evidence into ordinary runtime data.
```

## Test entry

```powershell
cargo test -p dlm_core --test backend_capability
```
