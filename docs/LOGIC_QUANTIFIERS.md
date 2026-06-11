# LOGIC_QUANTIFIERS.md — DLM v0.55

## Purpose

`v0.55.0` begins stage **2) Ordinary mathematics of the language**.

This layer adds first-class logical formula and quantifier objects without allowing them to collapse into theorem, proof, truth, provability or runtime witness objects.

## New foundation objects

```text
LogicalFormula<form>
QuantifiedFormula<forall x:T. P(x)>
QuantifiedFormula<exists x:T. P(x)>
```

## Connectives

```text
and(P, Q)
or(P, Q)
implies(P, Q)
iff(P, Q)
not(P)
```

The MVP layer enforces arity and stable proposition identity.

## Quantifiers

```text
forall x:Domain. Body
exists x:Domain. Body
```

A bound variable must have an explicit identifier and an explicit domain. Full substitution, alpha-equivalence and introduction/elimination proof rules are intentionally not added in this patch; they are future layers.

## Central law

```text
LogicalFormula != Theorem
LogicalFormula != StaticProof
LogicalFormula != TruthClaim
LogicalFormula != RuntimeWitness
QuantifiedFormula != Theorem
QuantifiedFormula != StaticProof
```

Formula construction works on proposition-like objects only. It must not silently consume theorem evidence, proof terms, truth claims, provability claims, runtime witnesses, consistency claims or reflection/self-reference claims.

## Taint rule

Formula objects preserve the maximum trust taint of their sources:

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

No logical connective or quantifier can clean Axiom/Oracle/Unsafe taint.

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         5–10%  -> 12–18%
Architectural readiness: 30–40% -> 35–45%
Fundamental readiness:   20–30% -> 22–32%
```

This patch creates formula objects, not a full proof calculus.
