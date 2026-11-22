# DLM/ЯРД v0.24 — BigNumber Hierarchy

This document specifies the first MVP layer for huge finite numbers.

## Core law

A huge number is not just `Nat`.

A huge number must expose how it is generated and what access is permitted:

```text
BigNat = family + optional parameter + construction + cost + capabilities + history
```

This prevents the classic mistake:

```text
finite = writable = computable = printable
```

DLM separates these modes.

## MVP constructors

```dlm
Graham()          // BigNat<Graham>
TREE(3)           // BigNat<TREE(3)>
BB(1000)          // BigNat<BB(1000)>
fast_growing(5)   // BigNat<FGH(5)>
```

## Access model

Huge numbers normally support:

```text
can_symbolic_print
can_compare_by_proof
can_big_number_reason
can_extract_growth_class
```

They do not automatically support:

```text
can_print_decimal
can_compare_direct
```

Therefore this is valid:

```dlm
let t = TREE(3)
print_symbolic(t)
```

but this is rejected:

```dlm
let t = TREE(3)
print_decimal(t)
```

## Busy Beaver

`BB(n)` is finite for fixed `n`, but its exact value is not generally computable by a universal algorithm. In DLM it is represented as:

```text
BigNat<BB(n)>
construction = Definable
cost = Uncomputable
```

It can be reasoned about symbolically or by proof, but cannot be printed as a decimal value.

## TREE

`TREE(n)` is a proof-finite huge combinatorial number. In the MVP, its parameter must be a positive literal Nat so that the passport remains explicit.

## Graham

`Graham()` is represented as a recursive/non-expandable huge number.

## Bare huge numbers are forbidden

The following is rejected:

```dlm
big_number()
```

because the generation family is missing.

