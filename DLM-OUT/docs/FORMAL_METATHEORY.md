# DLM/ЯРД Formal Metatheory Sketch v0.26

This document records the formal-metatheory target for DLM/ЯРД.
The Rust implementation is currently ahead of the written proof; this document
states what future proof work must establish.

## 1. Metatheory choice

For MVP engineering, the meta-level is ordinary Rust implementation logic plus
tests. For academic formalization, the intended target is a small typed calculus
with:

- typed terms;
- passports as product-lattice annotations;
- theory contexts;
- bridge rules;
- trust levels;
- proof terms and checked static proofs;
- runtime witnesses separated from static proofs.

The eventual proof can be mechanized in Lean/Coq/Agda or a future DLM proof
kernel once it is strong enough.

## 2. Core judgments

The core checker should correspond to judgments of the form:

```text
Γ; Θ ⊢ e ⇓ v : Type @ Passport
Γ; Θ ⊢ bridge B : TheoryA -> TheoryB
Passport ⊢ capability c
Trust(Passport) <= Policy
```

Where:

- `Γ` is a value environment;
- `Θ` is the theory/bridge environment;
- `e` is a source expression;
- `v` is an abstract value/passport result.

## 3. Target theorems

### Capability preservation

If `dlm check` accepts an expression using operation `op`, then every argument
passport contains the capabilities required by `op`.

### Trust monotonicity

If a result depends on an Axiom, Oracle or Unsafe source, its trust/history must
record that dependency and strict policies must reject it.

### Theory boundary safety

If an expression uses a value from a different theory, then either it is only a
qualified reference to a source object or it crosses through an explicit bridge.

### Static/runtime separation

A value whose provenance is `RuntimeInput` cannot be used to construct a
`ProofTerm` or `StaticProof` unless an explicit unsafe/axiom/oracle rule is used.

### Proof-kernel soundness, MVP form

If `check_proof(t)` returns `StaticProof<kernel_checked:r>`, then `t` was created
by an accepted proof-kernel constructor for rule `r` and did not depend on
runtime input.

## 4. Non-goals of MVP

MVP v0.26 does not prove global consistency of all possible DLM theories.
It also does not provide a full dependent type checker, a complete semantics of
all bridge kinds, or a mechanized proof of Rust implementation correctness.


## v0.27 Bridge metatheory contract

Bridge declarations are not casts. They are metatheoretic commitments.

For each bridge `B : S -> T`, the soundness layer assigns a preservation tuple:

```text
Preserves(B) = <syntax, value, proof, truth>
```

The key rule is:

```text
truth may only cross a bridge marked as truth-preserving, and soundness-style truth preservation is Axiom-tainted in MVP.
```

In particular:

```text
quote(PA.x)       gives Term<PA.T>, not T truth in Meta.
transport(PA.x)   moves value role, not proof/truth.
soundness(PA.p)   gives StaticProof in target only with Axiom taint.
reflection         is explicit and never implicit.
unsafe             is never clean.
```
