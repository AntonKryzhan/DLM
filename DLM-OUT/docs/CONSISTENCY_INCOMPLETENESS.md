# v0.30 — Consistency / Incompleteness Boundary

This layer adds the first explicit passport boundary for consistency claims.

## Core law

```text
Consistency<T> is a claim object, not a proof.
```

A sufficiently expressive theory must not silently create a checked proof of its own consistency. In DLM/ЯРД this is enforced by separating:

```text
Consistency<T>                         -- the claim that T is consistent
StaticProof<consistency_axiom:T>       -- an explicit axiom-tainted assumption
```

There is deliberately no checked MVP constructor that turns `Consistency<T>` into a clean `StaticProof`.

## Builtins

```dlm
consistency_claim()
consistency_of_current()
consistent_current()
```

Create `Consistency<CurrentTheory>`.

```dlm
prove_consistency(claim)
prove_own_consistency(claim)
```

Rejected with `IncompletenessBoundaryError[E0906]` in MVP.

```dlm
assume_consistency(claim)
consistency_axiom(claim)
consistency_from_axiom(claim)
```

Create an explicit Axiom-tainted static proof:

```text
StaticProof<consistency_axiom:T>
```

This path is allowed in research mode but rejected by `--trusted-only`.

## Soundness/explain

`dlm explain` counts:

```text
consistency claims
axiom consistency assumptions
```

A program that uses `assume_consistency(...)` is not clean under passport soundness checks.

## Future work

Later versions may add a stronger meta-theory bridge that can prove consistency of a weaker object theory, but it must be explicit and must record its preservation law in the bridge soundness profile.
