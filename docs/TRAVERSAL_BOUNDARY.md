# DLM v0.63 — Fold / Map / Traversal Boundary

`v0.63` adds the first finite traversal layer over the explicit finite collection model from `v0.62`.

The key boundary is:

```text
map/fold/traverse over finite collections != hidden recursion
map/fold/traverse over finite collections != unbounded normalization
map/fold/traverse over finite collections != proof/theorem/truth
```

A traversal is an audited static object. It describes a bounded pass over a `List<T>` or `Sequence<T>` and records:

- source kind;
- input item type;
- output item or accumulator type;
- collection length;
- explicit fuel;
- function contract used by the traversal;
- result type;
- status;
- taint summary;
- stable fingerprint.

## Added objects

```text
MapTraversal
FoldTraversal
TraversalReport
```

## Added status lattice

```text
VerifiedBounded
Downgraded
Open
RejectedFuelExceeded
RejectedContract
```

`VerifiedBounded` is intentionally narrow. A traversal reaches it only when:

1. the collection is finite;
2. fuel is at least the collection length;
3. the function contract domain matches exactly;
4. the function contract is verified;
5. the function contract is pure;
6. the function contract is total;
7. the function contract has no effects;
8. no Axiom/Oracle/Unsafe taint is present.

Anything weaker is either downgraded/open/rejected, not silently promoted.

## Map boundary

```text
map : Sequence<A> × FunctionContract<A -> B> × fuel -> Sequence<B>
```

For `List<A>` the result is `List<B>`.

Rules:

- function contract domain must be exactly `A`;
- requested output type must be exactly the function contract codomain;
- `fuel < len` rejects the traversal;
- effectful/partial/open contracts do not become verified traversals;
- proof/theorem/truth/runtime objects are not ordinary traversal payloads.

## Fold boundary

```text
fold : Sequence<A> × Acc × FunctionContract<ProductType<Acc*A> -> Acc> × fuel -> Acc
```

For `List<A>` the same rule applies.

Rules:

- initial accumulator must be an ordinary value of exactly the declared accumulator type;
- step contract domain must be exactly `ProductType<Acc*A>`;
- step contract codomain must be exactly `Acc`;
- `fuel < len` rejects the fold;
- the fold result is an audited result type, not an implicit runtime exception or proof.

## Soundness law

```text
Traversal does not prove totality.
Traversal consumes totality evidence from FunctionContract.
Traversal does not erase effects.
Traversal does not erase taint.
Traversal does not add hidden recursion.
Traversal does not add hidden normalization.
```

This connects directly to the architectural laws:

```text
Pure core deterministic.
Explicit effect boundary.
Capabilities as computation router.
Cost-class as part of model.
Honest status downgrade.
Bounded normalization / termination budget.
```

## Current MVP limitation

This layer is still a static/audit-level foundation. It does not execute arbitrary user lambdas over runtime arrays. Execution lowering belongs to later runtime/compiler stages.

The current layer answers a narrower question:

```text
Is this traversal structurally finite, explicitly fueled, contract-compatible, and soundness-auditable?
```
