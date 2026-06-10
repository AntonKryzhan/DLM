# DLM/ЯРД Passport Soundness v0.26

This document defines the current MVP meaning of **passport soundness**.
It is not a full academic proof of consistency. It is the engineering-formal
contract that `dlm check` currently enforces.

## 1. Passport soundness law

A checked value is sound with respect to its passport when every operation used
to produce that value had the required capabilities, did not cross a theory
boundary without an explicit bridge, and preserved or worsened trust instead of
silently improving it.

```text
operation(value) is allowed only if:
  required_capabilities ⊆ value.capabilities
  trust(value) <= active_policy.max_trust
  theory transition is justified by an explicit bridge
  runtime-dependent evidence is not used as StaticProof
```

## 2. Capability soundness

Capabilities are permissions over mathematical objects. They are intentionally
not simple type names. For example, a `Nat` may or may not have
`can_print_decimal` depending on whether it is explicit, compressed,
definable, uncomputable, remote, or GPU-resident.

The checker must reject an operation when the required capability is absent.
This is the core protection against treating `BB(1000)`, `TREE(3)`, `Term<PA.Nat>`,
`Remote<Nat@aarch64>`, or `GpuValue<Nat>` as ordinary printable natural numbers.

## 3. Trust soundness

Trust is monotone toward less trusted states:

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

A value that depends on an axiom, oracle, or unsafe source must not silently
become clean. The current MVP enforces this with policy checks and HistoryChain
summaries.

## 4. Theory bridge soundness

A value is valid inside its `TheoryContext`. Crossing from one theory to another
requires an explicit bridge. The important distinctions are:

- `quote`: turns an object into syntax and changes its capabilities.
- `transport`: preserves value-level meaning through an explicit bridge.
- `soundness`: lifts proof/truth across a dangerous boundary and is Axiom-tainted.
- `migration` and `materialize`: move distributed/runtime values but keep history.

## 5. Proof kernel soundness

MVP v0.25 introduced the first checked proof pipeline:

```text
ProofTerm<rule> -> check_proof(...) -> StaticProof<kernel_checked:rule>
```

This is not yet a full dependent type checker. It is a minimal kernel with a
small set of built-in proof constructors. The soundness guarantee is limited to
those constructors and the checker rules around them.

## 6. Runtime/static soundness

Runtime evidence is not static evidence. `read_nat()` may be checked by
`require(...)` and produce a `RuntimeWitness`, but it cannot produce a
`StaticProof` or kernel `ProofTerm` without an explicit unsafe/axiom/oracle path.

## 7. `dlm explain`

`dlm explain <file.dlm>` summarizes passport-soundness-relevant facts:

- values checked;
- proof terms;
- kernel-checked proofs;
- runtime witnesses;
- axiom/oracle/unsafe taint;
- quote/transport/soundness/migration/materialize/GPU history events;
- invariant issues detected by the summary pass.


## v0.27 Bridge soundness classification

Bridge preservation is now part of the soundness summary. A bridge declaration is classified before any value uses it.

The MVP checker distinguishes:

- `definitional`: preserves syntax, value, proof and truth by definition;
- `conservative`: preserves old-theory value/proof/truth without adding old-language theorems;
- `quote`: preserves syntax only;
- `transport`: preserves value role only;
- `soundness`: Axiom-tainted bridge from proof/provability toward truth;
- `reflection`: reflective metatheory, Axiom-tainted in MVP;
- `migration`: runtime location/architecture transport;
- `materialize`: explicit return from remote value to local value;
- `unsafe`: no safe preservation law.

`dlm explain` reports these profiles, so a module can be checked and then audited for which bridge assumptions its result depends on.
