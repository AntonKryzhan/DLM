# PROPERTY_INVARIANTS.md — DLM / ЯРД

## v0.36.0 — Property-Style Invariant Tests

This document records the first property-style invariant layer for DLM.

`v0.36.0` intentionally does not add a new external testing dependency yet. Instead it adds deterministic generated/enumerative tests over the finite trust lattice and bridge taxonomy. This keeps the project dependency-free while moving the tests from example-only regression checks toward invariant checks.

## Why this layer exists

Example files are useful, but they do not cover whole semantic spaces. The most dangerous DLM bugs are lattice and preservation bugs:

```text
Axiom taint silently becoming Checked
Unsafe taint disappearing
quote preserving value or truth
transport preserving proof by accident
reflection producing truth without an axiom-tainted boundary
history being treated as a set instead of an ordered chain
```

The property layer encodes those laws directly.

## Test file

```text
crates/dlm_core/tests/property_invariants.rs
```

The tests currently cover:

```text
trust join idempotence
trust join commutativity
trust join associativity
trust join monotonicity
policy prefix-closure
BridgeProfile == bridge_law for every bridge kind
truth-preserving bridge implies proof preservation
axiom-requiring bridge implies Axiom-or-worse taint
quote syntax-only boundary
transport/migration/materialize value-only boundary
soundness Axiom-tainted truth/proof boundary
unsafe/unknown bridge Unsafe taint
binary passport derivations never lower source trust
source-derived passports preserve or raise trust
history order and multiplicity are preserved
```

## Current design choice

The tests are property-style but deterministic:

```text
for every TrustLevel pair/triple
for every bridge kind
for every source trust level
```

This is enough for the current finite semantic lattices. Later versions can add `proptest` or `quickcheck` when the project is ready to carry external dev-dependencies and generated random AST/passport inputs.

## Required command

```powershell
cargo test -p dlm_core --test property_invariants
```

## Invariant boundary

The new tests are not meant to replace example tests. They protect the central semantic contracts that must remain true across future refactors:

```text
checker.rs split
HIR / ResolvedHIR introduction
TypedIR / PassportIR introduction
bridgeck extraction
policy.rs expansion
future audit mode
```
