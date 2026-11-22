# v0.59.0 — Product / Sum / Record Type Foundation

This document defines the first structural ordinary-mathematics layer for DLM / ЯРД.

It introduces products, sums and records as explicit structural objects. They are ordinary mathematical structure carriers, not proofs, not truth claims and not runtime witnesses.

## Core law

```text
ProductType/ProductTerm != Theorem
SumType/SumInjection     != StaticProof
RecordType/RecordTerm   != TruthClaim
RecordProjection        != ProofCertificate
```

Structural objects are value/form/type-level objects. They must not silently consume proof evidence or truth evidence.

## Objects

```text
ProductType<A * B>
ProductTerm<(a,b):A*B>

SumType<A + B>
SumInjection<left:a:A+B>
SumInjection<right:b:A+B>

RecordType<Name { field: Type, ... }>
RecordTerm<Name { field: value, ... }>
RecordProjection<record.field : Type>
```

## Design constraints

### Explicit components

Product and sum type sides must be explicit. DLM does not infer hidden `Any`, `Unknown`, `Object` or erased variants.

### Exact field set

Record construction requires exactly the declared field set. Missing fields are rejected. Duplicate fields are rejected. Field names are semantic identifiers and future layout keys.

### No proof/truth/runtime smuggling

The following objects are rejected as ordinary structural values:

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
EqProof
RewriteRule
RewriteCertificate
```

This preserves the existing laws:

```text
proof != value
truth != value
runtime witness != static proof
certificate != ordinary data
```

### Taint preservation

Axiom, Oracle and Unsafe taint from component passports is preserved by structural construction.

```text
Axiom-tainted component  => Axiom-tainted product/sum/record
Unsafe component         => Unsafe structural value
```

### Order-sensitive records

Record field order is fingerprint-sensitive in this MVP. This is intentional because field order may become a layout/ABI concern later.

If DLM later introduces order-neutral record equivalence, it must be represented by a separate normalization/equivalence report, not by silently sorting fields inside the constructor.

## Relation to architectural laws

This layer implements or prepares these laws:

```text
1. Separate semantic layers.
2. Passport-governed operations.
9. Trust only worsens or is explicitly proven.
13. Semantic objects need source/span mapping in future parser integration.
20. Cache checked meaning, not raw text.
22. Explain results backward.
27. Stable ABI / layout contract.
```

## Current limitation

This is a core Rust API foundation. It is not yet integrated into `.dlm` surface syntax.

Future work:

```text
structural syntax;
record field resolver IDs;
field spans;
layout contract;
structural pattern matching;
record update;
product/sum elimination rules;
proof rules for structural equality;
compiler lowering to dense layout.
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language

Local readiness:         30–38% -> 36–44%
Architectural readiness: 50–62% -> 56–68%
Fundamental readiness:   34–46% -> 38–50%
```
