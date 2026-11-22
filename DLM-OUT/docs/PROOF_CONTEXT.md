# Proof Context Foundation

`v0.39.0` adds the first internal proof-context layer. It does not add new `.dlm` surface syntax yet.

The goal is to make future proof-assistant work explicit before any tactic language is exposed.

## Core objects

```text
ProofContext
HypothesisSet
HypothesisId
TacticStep
ProofObligation
ProofClosure
```

A proof context is opened from a `Goal<P>` passport only. A `Statement<P>` is not an open proof state, and a `Theorem<name:P>` is not a goal.

## Closing law

The strict closing invariant is:

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```

All three propositions must match exactly. A static proof for `Q` cannot close a goal for `P`.

## Hypotheses

Hypotheses are ordered entries in a `HypothesisSet`:

```text
h0 : Hypothesis<P>
h1 : Hypothesis<Q>
```

The order and multiplicity are semantically meaningful. Repeated hypotheses are not deduplicated.

A hypothesis never becomes a theorem implicitly.

## Axiom admission

A proof may also be closed by explicit axiom admission:

```text
Goal<P> + Statement<P> + AdmitAxiom(reason) => Theorem<name:P> with trust=Axiom
```

This is intentionally visible in the resulting theorem passport. It must not be silently cleaned into `Checked` trust.

## Current integration level

`v0.39.0` is still an internal foundation layer:

```text
AST/parser: unchanged
CLI syntax: unchanged
legacy checker semantics: unchanged
proof context API: added
```

Later versions can lower theorem syntax and tactic blocks into this API through HIR/ProofIR.
