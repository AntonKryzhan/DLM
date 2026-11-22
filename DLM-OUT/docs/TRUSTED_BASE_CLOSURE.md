# Trusted Base Closure / Final Metatheory Foundation Gate

`v0.53.0` adds the final gate for the current metamathematical foundation track.

Earlier layers answer local questions:

```text
AxiomRegistry                 — which assumptions exist?
MetatheoryDependencyAudit     — which dependencies are used?
MetatheoryClosureReport       — are local obligations closed?
ConservativeExtensionAudit    — did an extension preserve old theorems?
GlobalMetatheoryInventory     — how are theorem/audit artifacts connected?
SoundnessBoundaryLedger       — where are soundness/reflection/consistency/oracle/unsafe boundaries?
```

The trusted-base closure report answers the global question:

```text
Is the metatheoretical trusted base closed as one auditable foundation?
```

## Main law

```text
AxiomRegistry
+ MetatheoryDependencyAudit
+ MetatheoryClosureReport
+ GlobalMetatheoryInventory
+ SoundnessBoundaryLedger
+ optional ConservativeExtensionAudit[]
=> TrustedBaseClosureReport
```

## Status law

```text
all required evidence closed      => TrustedBaseClosure<closed>
any required evidence open        => TrustedBaseClosure<open>
any rejected evidence             => TrustedBaseClosure<rejected>
missing required evidence         => TrustedBaseClosure<rejected>
duplicate evidence id/fingerprint => TrustedBaseClosure<rejected>
```

## Required evidence kinds

A final metatheory gate must include:

```text
axiom_registry
dependency_audit
metatheory_closure
global_inventory
soundness_boundary_ledger
```

Conservative-extension audits are not required for a single-theory foundation, but when present they contribute trust taint and fingerprints.

## Taint law

The trusted-base closure never hides taint:

```text
Axiom  taint in any evidence => report.has_axiom_taint = true
Oracle taint in any evidence => report.has_oracle_taint = true
Unsafe taint in any evidence => report.has_unsafe_taint = true
```

The resulting passport uses the maximum trust level of all evidence.

## Boundary

`TrustedBaseClosure` is an audit artifact. It is not a theorem, not a proof, and not a truth claim. It says that the project has accounted for its metatheoretical foundation under the current registry, dependency, closure, inventory, and soundness-boundary reports.
