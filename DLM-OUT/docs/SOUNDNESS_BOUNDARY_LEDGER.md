# Soundness Boundary Ledger / Bridge Assumption Inventory

`v0.52.0` adds the next metatheory-foundation layer after the global theorem dependency graph.

The goal is to make every soundness-sensitive boundary explicit and auditable:

```text
soundness bridge
reflection bridge
truth-from-provability axiom lift
consistency assumption
self-reference axiom
conservative-extension boundary
oracle dependency
unsafe bridge
```

## Main law

```text
BoundaryAssumptionEntry[]
+ optional GlobalMetatheoryInventoryReport
=> SoundnessBoundaryLedgerReport
```

A verified ledger means:

```text
all soundness/reflection/consistency/oracle/unsafe boundaries are explicit,
non-duplicated,
fingerprinted,
and keep their Axiom/Oracle/Unsafe taint visible.
```

## Non-goals

This layer is not a proof kernel and not a theorem constructor.

```text
SoundnessBoundaryLedger != Theorem
SoundnessBoundaryLedger != StaticProof
SoundnessBoundaryLedger != Truth
```

It is an audit artifact for the trusted boundary.

## Important rules

Safe builtin bridges such as pure `quote` are not ledger assumptions.
They remain in `BridgeProfile`.

A bridge/profile enters the ledger only when it is soundness-sensitive:

```text
requires_axiom = true
or taint >= Axiom
or reflective/unsafe/oracle boundary
```

Passport evidence enters the ledger only when it is explicitly boundary-shaped:

```text
StaticProof<truth_from_provable:*>
StaticProof<reflection_axiom:*>
StaticProof<self_reference_axiom:*>
StaticProof<consistency_axiom:*>
ConservativeExtensionAudit<...>
Oracle-tainted passport
Unsafe-tainted passport
Axiom-tainted passport
```

## Status model

```text
Verified — every boundary entry is explicit and non-duplicated.
Open     — unknown boundary evidence remains open.
Rejected — duplicate/malformed entries or rejected global inventory.
```

## Why this matters

Before a real proof kernel can become small and trusted, the project needs an explicit ledger of everything outside or around the kernel that affects soundness.

This prevents hidden assumptions such as:

```text
Provable<P> silently used as Truth<P>
reflection used as a free truth bridge
consistency assumed without axiom taint
unsafe bridge result shown as checked
oracle data represented as internal checked data
```

The ledger is the first concrete accounting layer for the future trusted base.
