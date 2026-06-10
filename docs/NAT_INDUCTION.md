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
