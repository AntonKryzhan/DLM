# DLM/ЯРД v0.25 — Minimal Proof Kernel

This document defines the first checked proof-term layer of DLM/ЯРД.

## Status

Before v0.25, `StaticProof<P>` was primarily a passport-level object: the checker could mark a condition as statically proven when it was static-safe, but there was no separate proof-term object checked by a kernel.

v0.25 introduces a minimal proof kernel with explicit `ProofTerm` values and a `check_proof(...)` operation.

## New types

```text
ProofTerm<rule>
StaticProof<kernel_checked:rule>
```

A `ProofTerm` is not itself a theorem. It is a candidate proof object produced by a trusted kernel constructor.

A `StaticProof` is produced only after `check_proof(proof_term)` verifies that the term has `can_proof_kernel_check`.

## New constructors

```dlm
proof_true()
true_intro()
```

Constructs a proof term for the built-in `true_intro` rule.

```dlm
proof_gt(a, b)
gt_intro(a, b)
```

Constructs a proof term for direct static Nat comparison `a > b`.

In MVP this requires direct static comparability. Runtime values must use `require(...)`, not proof-kernel terms.

## New checker operation

```dlm
check_proof(term)
kernel_check(term)
verify_proof(term)
```

Consumes a `ProofTerm<rule>` and returns:

```text
StaticProof<kernel_checked:rule>
```

## Rejected forms

```dlm
fake_proof()
unchecked_proof()
bare_proof()
```

These are rejected by `ProofKernelError`. The language does not allow proof objects to appear from untyped strings or unchecked values.

## Capabilities

New capability:

```text
can_proof_kernel_check
```

Only proof terms produced by kernel constructors receive it.

## Key rule

```text
A StaticProof produced by the proof kernel must come from a ProofTerm that was constructed by a checked kernel rule.
```

This does not replace the older `prove(...)` MVP helper yet; it adds the first proper path toward mechanically checked proof terms.
