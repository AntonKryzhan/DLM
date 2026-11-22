# Sequence / List Type Foundation (v0.62)

This document defines the first finite collection layer for DLM / ЯРД.

The goal is not to introduce hidden arrays, implicit nulls, exceptions or infinite streams. The goal is to make finite collections explicit, typed, passport-aware and audit-visible.

## Core objects

```text
ListType<T>
ListValue<T; len=n>

SequenceType<T>
SequenceValue<T; len=n>
SequenceIndex<Sequence<T>[i]:status>
```

## Main law

```text
List<T> / Sequence<T> are ordinary finite mathematical values.
They are not proofs, theorems, truth claims or runtime witnesses.
```

## Explicit finite boundary

A list or sequence value must carry an explicit item type and explicit finite length.

```text
[] : List<Nat>        // allowed only when item type is explicit
[x,y] : List<Nat>     // all elements must be Nat
seq[i]                // returns explicit index report with Option<T> result boundary
```

There is no implicit infinity, hidden lazy stream, hidden Any, hidden null or runtime exception in this layer.

## Indexing law

Indexing is a reported boundary:

```text
in-bounds     => SequenceIndex<...:in_bounds>, result_type = Option<T>, value = Some(...)
out-of-bounds => SequenceIndex<...:out_of_bounds>, result_type = Option<T>, value = None
```

This is intentionally aligned with `v0.61 Option / Result / Partiality`.

## Rejections

Finite collection elements cannot silently consume:

```text
ProofTerm
StaticProof
Theorem
TruthClaim
RuntimeWitness
EqProof
RewriteCertificate
```

## Taint law

Axiom, Oracle and Unsafe taint is preserved through list/sequence construction and indexing reports.

## Current scope

This is a core semantic foundation only. It does not yet add `.dlm` source syntax, parser integration, runtime storage layouts, iterators, folds, maps, comprehensions or GPU/vector lowering.
