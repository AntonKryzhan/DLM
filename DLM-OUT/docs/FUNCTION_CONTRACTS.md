# Function Contract / Purity / Totality Boundary

`v0.58.0` adds the first contract layer above ordinary functions.

The previous layer introduced `FunctionType`, `LambdaTerm` and `ApplicationTerm`. This layer does not turn those objects into proofs. It attaches an explicit contract that records whether a function is pure, effectful, total, partial, unknown within budget, verified, open, downgraded or rejected.

## Main law

```text
FunctionContract != Theorem
FunctionContract != StaticProof
FunctionContract != ProofCertificate
FunctionContract != TruthClaim
```

A function contract is an audit/control object. It may justify later optimization or scheduling, but only if it is verified by explicit evidence and remains clean with respect to trust.

## Contract dimensions

```text
FunctionPurity:
  Pure
  Effectful

FunctionTotality:
  Total
  Partial
  UnknownWithinBudget

FunctionContractStatus:
  Verified
  Open
  Downgraded
  Rejected
```

## Effect boundaries

All effects must be explicit:

```text
Runtime
Io
Network
Filesystem
Clock
Randomness
Oracle
UnsafeExternal
GpuExecution
RemoteExecution
```

A pure contract with any explicit effect is rejected. An effectful contract without a named boundary is open. Oracle and unsafe effects preserve visible taint.

## Totality evidence

A `Total` contract requires static evidence:

```text
StaticProof
Theorem
```

Without such evidence the contract remains `Open`. Runtime witnesses do not prove totality.

## Honest downgrade

A contract becomes `Downgraded` when it is internally consistent but weaker than the maximum guarantee:

```text
Effectful function
Partial function
UnknownWithinBudget function
Axiom-tainted evidence
Oracle-tainted effect
Unsafe external effect
```

This implements the architectural law: if maximum assurance is impossible, the status must be honestly downgraded.

## Why this matters

This layer connects Stage 2 ordinary mathematics with later runtime/compiler layers:

```text
Pure + Total + Verified       => future verified optimization candidate
Effectful + explicit boundary => future scheduler/runtime route
Partial/Unknown               => bounded normalization/proof-search aware
Oracle/Unsafe                 => visible trusted-base and assurance downgrade
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         24–32% -> 30–38%
Architectural readiness: 44–56% -> 50–62%
Fundamental readiness:   28–40% -> 34–46%
```
