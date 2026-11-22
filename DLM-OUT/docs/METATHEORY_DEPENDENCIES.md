# Metatheory Dependency / Axiom Registry Foundation

Version: v0.48.0

This layer closes an important gap in the metamathematical foundation: proofs,
certificates, rewrite traces, module interfaces and induction results must not be
trusted as isolated artifacts. They need a stable dependency inventory.

## Core law

```text
AxiomRegistry<T> + ordered DependencyEntry[] => MetatheoryDependencyAuditReport
```

The report is an audit object, not a theorem and not a proof. It records whether a
subject depends on checked, axiom-tainted, oracle-tainted or unsafe artifacts.

## New passport kinds

```text
AxiomRegistry<T>
MetatheoryDependencyAudit<subject:status>
```

These are metatheory audit contracts. They must not be used as `Theorem`,
`StaticProof`, `TruthClaim` or runtime evidence.

## Axiom registry

An `AxiomRegistry` is a per-theory inventory of explicit assumptions:

```text
AxiomDecl {
  theory,
  name,
  proposition,
  kind,
  trust,
  provenance,
  reason,
  fingerprint
}
```

Duplicate axiom names are rejected. Cross-theory axioms are rejected unless later
represented through explicit bridge/import layers.

## Dependency audit

Dependency audit entries preserve:

```text
label
kind
type identity
trust
provenance
validation
history order
fingerprint
```

The audit computes:

```text
max_trust
has_axiom_taint
has_oracle_taint
has_unsafe_taint
registry_fingerprint
audit_fingerprint
```

## Soundness boundary

The new layer enforces:

```text
undeclared axiom != valid dependency
empty audit != closure evidence
unchecked dependency != verified audit
unsafe taint must remain visible
history order changes fingerprint
```

This prepares the project for later proof-kernel hardening: the kernel should be
able to ask not only “does this proof check?” but also “which assumptions, axioms,
bridges and external dependencies does this proof rely on?”
