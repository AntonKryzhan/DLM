# DLM/ЯРД v0.29 — Provability / Truth Boundary

This document defines the first explicit object-language boundary between propositions, provability claims and truth-like use.

## Law

```text
Provable_T(phi) is not phi.
```

A checked proof can produce a provability claim:

```dlm
let term = proof_true()
let checked = check_proof(term)
let claim = provable_of(checked)
```

But the following is rejected:

```dlm
let truth = truth_from_provable(claim)
```

because using provability as truth requires an explicit soundness bridge or an axiom-tainted lift.

## Types

```text
Prop<name>
Provable<Theory.proposition>
StaticProof<kernel_checked:rule>
```

## Constructors

```text
prop_true()          -> Prop<true>
prop_gt(a, b)        -> Prop<gt>
provable_of(proof)   -> Provable<T.proposition>
```

`prop_gt(a, b)` is static only in MVP. It rejects runtime-dependent values and directs users to `require(...)` for runtime witnesses.

## Truth boundary

```text
truth_from_provable(provable)
```

is rejected with `TruthBoundaryError` / `TheoryBridgeError` semantics.

For experiments only:

```text
truth_from_provable_axiom(provable)
```

creates an Axiom-tainted `StaticProof<truth_from_provable:...>` and is rejected by `--trusted-only`.

## Soundness summary

`dlm explain` now reports:

```text
propositions
provability claims
axiom truth lifts from provability
```

This makes Tarski/Gödel-style boundaries visible to the checker instead of leaving them as comments or convention.
