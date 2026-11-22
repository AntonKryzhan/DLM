# Function Type / Lambda / Application Foundation

Version: v0.57.0

This document defines the first ordinary-function layer of DLM / ЯРД.

The goal is deliberately narrow: introduce function type objects, lambda term objects and application reports without silently turning them into theorem, proof, truth or runtime evidence.

## Core law

```text
FunctionType != Theorem
FunctionType != StaticProof
LambdaTerm != StaticProof
ApplicationTerm != ProofCertificate
ApplicationTerm != TruthClaim
```

Function application is ordinary mathematical construction. It does not prove that a theorem is true, does not discharge a goal, does not check a proof term and does not convert runtime evidence into static evidence.

## Objects

```text
FunctionType<Domain -> Codomain>
LambdaTerm<parameter:Domain. body>
ApplicationTerm<function(argument) => result:status>
```

The MVP tracks:

```text
domain
codomain
parameter
body
captures
purity flag
totality flag
application status
trust/provenance taint
fingerprint
history
```

## Why this layer comes after substitution

Quantifiers and lambda terms both bind variables. Without the v0.56 substitution / alpha-equivalence foundation, lambda introduction would risk scope capture, binder confusion and unstable formula identity.

Therefore the order is intentional:

```text
logical formulas
-> quantifiers
-> bound/free variables
-> alpha-equivalence
-> capture-avoiding substitution
-> function type / lambda / application
```

## Rejected implicit transitions

The MVP rejects ordinary application over these evidence objects:

```text
Theorem
StaticProof
ProofTerm
RuntimeWitness
Provable
TruthClaim
ReflectionClaim
SelfReferenceClaim
ConsistencyClaim
```

Those objects require proof-kernel, theorem, extraction or audit rules. They cannot be consumed as ordinary function values.

## Status model

Application reports use explicit status:

```text
applied
rejected_domain_mismatch
```

A domain mismatch is a mathematical/application status, not a panic and not a proof failure. Later type-checker passes can choose whether to make rejected application fatal in surface `.dlm` code.

## Trust preservation

Function construction and application preserve source taint:

```text
Axiom source  -> Axiom-tainted function/application
Oracle source -> Oracle-tainted function/application
Unsafe source -> Unsafe-tainted function/application
```

This follows the architectural law:

```text
Trust only worsens or is explicitly proven.
```

## Runtime law

This layer is still source/compiler semantics. It does not imply runtime closure allocation, dynamic dispatch or hardware-level function objects.

Future performance work must erase checked proof/passport metadata before low-level execution:

```text
rich function semantics above
compact call/kernel representation below
```

## Readiness impact

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         +6–8%
Architectural readiness: +4–6%
Fundamental readiness:   +3–5%
```
