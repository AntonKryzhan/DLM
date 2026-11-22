# Option / Result / Partiality Type Foundation

Version: v0.61.0

This document defines the first explicit partiality layer for ordinary DLM mathematics.

## Core law

```text
Option<T> != Truth(T)
Option<T> != Theorem(T)
Option<T> != StaticProof(T)
Result<T,E> != exception magic
PartialityReport != ProofCertificate
```

DLM must not encode partial functions through hidden nulls, hidden panics, hidden exceptions or implicit runtime failure. Partiality has to be part of the typed semantic object layer.

## Added objects

```text
OptionType<T>
OptionValue<some:T>
OptionValue<none:T>
ResultType<Ok,Err>
ResultValue<ok:value>
ResultValue<err:error>
PartialityReport<subject:status>
```

## Design intent

This layer connects ordinary mathematics with the function contract layer from v0.58:

```text
FunctionContract::Partial
  -> result should be represented as Option<T> or Result<T,E>
  -> absence/error is explicit
  -> trust/provenance taint remains visible
```

## Rules

- `some(value)` must match the declared `Option<T>` item type.
- `none<T>` must still carry the declared type `T`.
- `ok(value)` must match the declared `Result<Ok, Err>` ok type.
- `err(value)` must match the declared error type.
- `ProofTerm`, `StaticProof`, `Theorem`, `TruthClaim`, `RuntimeWitness`, equality proofs and certificates are not ordinary Option/Result values.
- `Axiom`, `Oracle` and `Unsafe` taint is preserved.
- Option/Result/Partiality reports are stable audit objects, not proof objects.

## Architectural laws reinforced

```text
5. Pure core deterministic
6. Explicit effect boundary
9. Trust monotonicity
18. Cost-class is part of model
25. Honest status downgrade
29. Bounded normalization / termination budget
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language

Local readiness:         42–50% -> 48–56%
Architectural readiness: 60–72% -> 64–76%
Fundamental readiness:   42–54% -> 46–58%
```
