# Nat Induction MVP

`v0.45.0` introduces the first core-level Nat induction foundation.

This is deliberately not new `.dlm` syntax yet. It is an internal proof object layer that future theorem/tactic syntax can target.

## Core artifacts

```text
InductionScheme<Nat,P>
BaseCase<P(0)>
StepCase<forall n:Nat. P(n) -> P(succ(n))>
InductionProof<forall n:Nat. P(n)>
```

## Protected law

```text
InductionScheme<Nat,P>
+ StaticProof<P(0)>
+ StaticProof<forall n:Nat. P(n) -> P(succ(n))>
=> InductionProof<forall n:Nat. P(n)>
```

The scheme, base case and step case must reference the same proposition family `P`.

## Boundary rules

- `RuntimeWitness<P>` is not a Nat induction case.
- raw `ProofTerm<P>` must be kernel-checked into `StaticProof<P>` first.
- `BaseCase` and `StepCase` are not theorems.
- `InductionProof` is a proof object, not silently a theorem.
- converting `InductionProof<P>` to `Theorem<name:P>` requires an explicit matching `Statement<P>`.
- Axiom taint in either case is preserved by the final induction proof and theorem.

## Current limitation

The MVP supports only finite Nat induction over string-named proposition families. It does not parse full dependent propositions or introduce surface proof syntax yet.

## Tests

```powershell
cargo test -p dlm_core --test nat_induction
```

## v0.77 surface syntax

`theorem name(n: Nat, ...): P by induction n` builds this layer's objects from `.dlm`. The proposition family is
`P_name`, base and step static proofs come from the symbolic kernel, and the `InductionProof` is instantiated to the
statement body. See `docs/PROOF_SURFACE.md`.

## v0.78 — List induction

Structural induction over `List<T>` has its own core objects: `list_induction_scheme`, `list_induction_base_case`
(`P([])`), `list_induction_step_case` (`forall h:T, t:List<T>. P(t) -> P(cons(h, t))`) and `list_induction_proof`
(`forall xs:List<T>. P(xs)`). A Nat scheme is not accepted where a list scheme is required. From `.dlm`,
`by induction xs` on a `List<Nat>` variable uses them; see `docs/PROOF_SURFACE.md` (v0.78 section).

## v0.79 — Step induction

`nat_step_induction_scheme(P, s)` (`s >= 2`) needs the base cases `P(0) .. P(s-1)` in order
(`nat_step_induction_base_case`) and the step `forall n:Nat. P(n) and .. and P(n+s-1) -> P(n+s)`
(`nat_step_induction_step_case`); `nat_step_induction_proof` concludes `forall n:Nat. P(n)`. A plain Nat scheme is not
accepted as a step scheme. From `.dlm`: `by induction n step s`.
