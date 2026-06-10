# Equality Proof / Rewrite Foundation

`v0.43.0` adds a core equality/rewrite layer without changing `.dlm` syntax, checker orchestration, or runtime behavior.

## Purpose

The layer separates three concepts that must not collapse into each other:

```text
Bool equality result != EqProof<A,B> != RewriteCertificate<A,B>
```

A boolean comparison may say that a comparison evaluated successfully, but it is not static proof evidence for rewriting. Rewriting requires an explicit `EqProof`, and applying an equality requires first deriving a named `RewriteRule`.

## Core artifacts

- `EqProof { lhs, rhs }` — static equality evidence.
- `RewriteRule { name, lhs, rhs }` — a named rule derived from `EqProof`.
- `RewriteStep` — one ordered application of a rewrite rule.
- `RewriteTrace` — an ordered sequence of rewrite steps.
- `RewriteCertificate { from, to }` — an audit passport for the final rewrite result.

## Construction paths

```text
StaticProof<Eq(lhs,rhs)> -> EqProof<lhs,rhs> -> RewriteRule<name:lhs->rhs>
RewriteRule applications -> RewriteTrace<from,to> -> RewriteCertificate<from,to>
```

A reflexive proof can also be produced as a trusted builtin proof:

```text
reflexive_eq_proof(theory, term) -> EqProof<term,term>
```

Explicit axiom equality is supported only with visible taint:

```text
axiom_eq_proof(...) -> trust=Axiom
```

That taint is preserved through rewrite rules, traces, and rewrite certificates.

## Directionality

A rewrite rule derived from `EqProof<lhs,rhs>` can be applied:

- `Forward`: `lhs -> rhs`;
- `Reverse`: `rhs -> lhs`.

The source term must match exactly. This first foundation deliberately avoids implicit unification, normalization, pattern matching, or beta-reduction. Those can be introduced later as explicit passes.

## Soundness invariants

1. `Bool` is not rewrite evidence.
2. `RuntimeWitness` is not static equality evidence.
3. Raw `ProofTerm` must be kernel-checked before it can become equality evidence.
4. `EqProof` must become a `RewriteRule` before application.
5. Rewrite traces preserve order and multiplicity.
6. Rewrite certificates do not lower trust or erase axiom/oracle/unsafe taint.

## Non-goals in v0.43.0

- No parser syntax.
- No theorem prover integration.
- No HIR rewrite pass.
- No runtime rewrite execution.
- No term-pattern matcher.

The patch is a semantic foundation for future proof automation.
