# Prelude Lowering / Verified Erasure Boundary

Version: `v0.68.0`

This document defines the first explicit lowering boundary for standard prelude evaluation reports.

The layer connects:

```text
StandardPreludeContract
        +
PreludeEvaluationReport
        +
LoweringTarget
        +
ErasureMode
        ↓
PreludeLoweringReport
```

It is intentionally not a compiler backend yet. It is the semantic contract that a future backend must satisfy before a small-step prelude result can be represented as runtime/interpreter/native/GPU/remote code.

## Central law

```text
PreludeLoweringReport != Proof
PreludeLoweringReport != Theorem
PreludeLoweringReport != TruthClaim
PreludeLoweringReport != RuntimeWitness
PreludeLoweringReport != hidden compiler magic
```

Lowering is an audit object. It may describe erasure of proof/passport metadata into a compact descriptor, but it must not erase trust, provenance or obligations from the audit layer.

## Targets

```text
audit_only
interpreter
native_scalar
native_vector
gpu_batch
remote_batch
```

The important performance law is batch-first GPU/remote lowering:

```text
NatAdd -> gpu_batch      rejected_target
ListMap -> gpu_batch     symbolic_lowered or downgraded_tainted
SequenceFold -> gpu_batch symbolic_lowered or downgraded_tainted
```

Scalar GPU launches are rejected unless a later explicit bridge justifies them.

## Erasure modes

```text
audit_only
proof_erased
passport_erased_with_descriptor
```

`passport_erased_with_descriptor` means hot runtime data may be dense, but the audit layer keeps a descriptor linking the lowered artifact back to the evaluated prelude report.

## Statuses

```text
verified_erased
symbolic_lowered
downgraded_tainted
rejected_evaluation
rejected_target
rejected_evidence_boundary
```

A lowering is `verified_erased` only when:

```text
PreludeEvaluationReport.status = evaluated
operation is valid for target
result contains no proof/theorem/truth/runtime evidence
Axiom/Oracle/Unsafe taint is absent
```

A symbolic prelude evaluation, such as `list.map` with bounded symbolic function application, remains `symbolic_lowered`.

A tainted evaluation remains `downgraded_tainted`; lowering never cleans trust.

## Evidence boundary

The following values are not runtime data and cannot be erased into lowered prelude artifacts:

```text
Proof
ProofTerm
StaticProof
Theorem
TruthClaim
RuntimeWitness
ProofCertificate
EqProof
```

If such evidence is found inside the evaluated value tree, the lowering report becomes `rejected_evidence_boundary`.

## Artifact shape

A lowering report contains:

```text
name
operation
target
erasure
input_type
output_type
evaluation
evaluation_status
representation
status
proof_erased
passport_erased
descriptor
open_obligations
max_trust
max_provenance
has_axiom_taint
has_oracle_taint
has_unsafe_taint
fingerprint
```

The descriptor is the bridge between dense runtime representation and full audit explainability.

## Not yet done

This layer does not emit executable machine code. It only defines the checked lowering contract that future runtime/compiler backends must consume.

The next natural layer is backend-specific verified lowering reports for interpreter/native/GPU targets.
