# Structural Elimination / Pattern Boundary

Version: v0.60.0

This document defines the first explicit elimination layer for structural values.

`v0.59` introduced construction of product, sum and record objects. `v0.60` adds the opposite direction: safe destructuring and case/pattern reports.

## Core law

```text
ProductElimination != Theorem
ProductElimination != StaticProof
ProductElimination != TruthClaim

SumElimination != Theorem
SumElimination != StaticProof
SumElimination != TruthClaim

RecordPattern != Theorem
RecordPattern != StaticProof
RecordPattern != TruthClaim
```

Structural elimination is value-level analysis. It does not prove a proposition and does not create theorem evidence.

## Product elimination

```text
ProductTerm<(a,b):A*B>
  -> ProductElimination<A*B => (A,B)>
```

The elimination report exposes the checked left and right component descriptors. It is explicit so future compiler lowering can decide whether the pair is erased, projected, kept packed, or lowered into ABI/layout-specific fields.

## Sum elimination

```text
SumInjection<left:a:A+B>
+ left_case_result:R
+ right_case_result:R
  -> SumElimination<A+B:left=>R>
```

Both branches must produce the same ordinary result type. If branch results diverge, elimination is rejected instead of silently inventing a common type.

## Record pattern

```text
RecordTerm<Point{x,y}>
+ pattern {x,y}
  -> RecordPattern<Point{x,y}>
```

Patterns bind existing fields only. Duplicate or missing fields are rejected. Pattern order is currently fingerprint-sensitive because future layout/ABI and pattern compilation must be explicit about order.

## Forbidden smuggling

Structural elimination must not consume or produce proof/truth/theorem/runtime evidence implicitly:

```text
StaticProof as product subject       rejected
TruthClaim as branch result          rejected
Theorem as branch result             rejected
RuntimeWitness as pattern subject    rejected
EqProof / RewriteCertificate         rejected
```

## Taint preservation

Axiom, Oracle and Unsafe taints are preserved through elimination reports and passports. Destructuring a tainted value does not clean it.

## Architectural laws enforced

```text
1. Separate semantic layers.
2. Passport-govern operations.
8. Bridge/preservation boundaries are explicit.
9. Trust only worsens or is explicitly proven.
13. Semantic objects need source/audit mapping.
22. Results must explain backward.
25. Honest downgrade instead of pretending success.
27. Stable ABI / layout contract.
29. Bounded normalization / elimination work.
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language

Local readiness:         36–44% -> 42–50%
Architectural readiness: 56–68% -> 60–72%
Fundamental readiness:   38–50% -> 42–54%
```
