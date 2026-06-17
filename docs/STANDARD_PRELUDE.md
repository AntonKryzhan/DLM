# DLM Standard Algebraic Prelude Foundation (v0.66)

This document specifies the first checked algebraic prelude boundary.

The standard prelude is not a magic runtime library and not a proof kernel. It is an auditable layer of canonical contracts for ordinary operations over the already-introduced core algebraic types.

## Law

```text
StandardPreludeContract != Proof
StandardPreludeContract != Theorem
StandardPreludeContract != TruthClaim
StandardPreludeContract != RuntimeWitness
StandardPreludeContract != hidden compiler magic
```

## Covered operations

Scalar operations:

```text
nat.add      : ProductType<Nat*Nat> -> Nat
nat.eq       : ProductType<Nat*Nat> -> Bool
bool.and     : ProductType<Bool*Bool> -> Bool
bool.not     : Bool -> Bool
```

Algebraic operations:

```text
option.map   : ProductType<OptionType<A>*FunctionType<A->B>> -> OptionType<B>
result.map   : ProductType<ResultType<A,E>*FunctionType<A->B>> -> ResultType<B,E>
```

Finite collection operations:

```text
list.length      : ListType<A> -> Nat
sequence.length  : SequenceType<A> -> Nat
sequence.index   : ProductType<SequenceType<A>*Nat> -> OptionType<A>
list.map         : ProductType<ListType<A>*FunctionType<A->B>> -> ListType<B>
sequence.map     : ProductType<SequenceType<A>*FunctionType<A->B>> -> SequenceType<B>
list.fold        : ProductType<ListType<A>*ProductType<Acc*FunctionType<ProductType<Acc*A>->Acc>>> -> Acc
sequence.fold    : ProductType<SequenceType<A>*ProductType<Acc*FunctionType<ProductType<Acc*A>->Acc>>> -> Acc
```

## Requirements

A standard prelude contract is `verified_checked` only when all of the following hold:

```text
1. canonical signature matches exactly;
2. FunctionContract status is verified;
3. function purity is pure;
4. function totality is total;
5. function effects list is empty;
6. collection traversal/fold operations have an explicit verified_unified TerminationBudgetReport;
7. Axiom/Oracle/Unsafe taint is absent.
```

If any function contract, budget, or source carries Axiom/Oracle/Unsafe taint, the prelude contract preserves it and downgrades the guarantee.

## Why this exists

The prelude is the first bridge from isolated type foundations into a usable standard library. It prevents the compiler from silently treating primitive-looking operations as trusted magic. Each operation has an explicit signature, contract, status, obligations and fingerprint.
