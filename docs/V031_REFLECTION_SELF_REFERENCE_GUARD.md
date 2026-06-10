# v0.31 — Reflection / Self-Reference Guard

DLM v0.31 adds the next metamathematical safety boundary after the provability/truth and consistency/proof boundaries.

## Core law

Reflection and self-reference are not implicit.

```text
Reflection<T.phi> != Truth(phi)
SelfReference<T.phi> != Proof(phi)
SelfReference<T.phi> != Truth(phi)
```

A program may construct an explicit claim object, but it cannot silently turn that claim into truth or proof. Any intentional lift must be visible as an axiom-tainted path.

## Safe symbolic forms

```text
reflection_claim(provable)        // requires bridge kind = reflection
reflection_axiom(claim)           // explicit axiom-tainted lift
self_reference(prop)              // constructs claim object only
godel_sentence()                  // constructs claim object only
self_reference_axiom(claim)       // explicit axiom-tainted lift
```

## Forbidden dangerous forms

```text
reflect(...)
reflect_provable(...)
prove_self_reference(...)
truth_of_self_reference(...)
truth_of_own_truth()
truth_of_self()
says_unprovable_self()
liar_sentence()
diagonalize(...)
fixed_point(...)
```

These forms now fail with `ReflectionBoundaryError` rather than falling through to `NameError`.

## Bridge rule

`reflection_claim(...)` requires an explicit bridge:

```text
bridge Core_reflection : Core -> Core {
  kind = reflection
}
```

This is intentionally stricter than ordinary term quotation. `quote(...)` preserves syntax; it does not automatically authorize reflective truth/proof reasoning.

## Trust accounting

`reflection_axiom(...)` and `self_reference_axiom(...)` return axiom-tainted symbolic truth claims. The important invariant is monotonicity:

```text
Axiom never silently becomes Checked.
```

`dlm explain` should therefore make intentional reflection/self-reference assumptions visible through trust/history output.
## v0.31 fix note — StaticProof to Provable boundary

`reflection_claim(...)` intentionally accepts `Provable`, not raw `StaticProof`.

Correct pipeline:

```text
let proof = prove(7 > 0)       // StaticProof
let p = provable_of(proof)     // Provable
let reflected = reflection_claim(p)
let accepted = reflection_axiom(reflected)
```

This preserves the v0.29/v0.30 separation: a checked proof object is not silently treated as a metatheoretic provability claim.
## v0.31 fix note — checker-only proofs and runtime-safe symbolic demo

`prove(...)` is not executable by `dlm run`; it is a static checker operation that produces a `StaticProof` during checking. Reflection examples using this chain should be validated with `dlm check` and `dlm explain`:

```text
StaticProof -> provable_of(...) -> Provable -> reflection_claim(...) -> reflection_axiom(...)
```

For runtime smoke testing, use `examples/valid/reflection_runtime_symbolic_guard.dlm`, which constructs a symbolic `SelfReferenceClaim` from a proposition and prints it symbolically. This keeps the proof/runtime boundary intact.
