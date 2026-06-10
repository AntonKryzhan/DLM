# DLM/ЯРД v0.22 — Universe Levels / Set vs Class

This document defines the first MVP layer of the mathematical universe hierarchy.
The goal is to prevent set-theoretic paradoxes caused by untyped universes and
uncontrolled self-collection.

## Core law

There is no bare universe.

```text
universe()              // rejected
U0(), U1(), U2()        // explicit universe levels
universe_succ(U0())     // U1
```

A universe level is a mathematical context, not an untyped global container.

## Set formation

A set formed from objects of `U n` lives one level higher:

```text
set_of(U0()) : Set<U0 -> U1>
set_of(U1()) : Set<U1 -> U2>
```

This encodes the rule:

```text
Set<U n> is represented in U(n+1)
```

So a set cannot live in exactly the same universe that it collects over.

## Class formation

A class is a meta-level view over one explicit universe:

```text
class_of(U0()) : Class<U0>
```

A `Class<U n>` is not the same kind of object as `Set<U n -> U n+1>`.
The MVP keeps this distinction at the passport/capability level.

## Rejected constructions

```text
set_of_all_sets()       // rejected
russell_set()           // rejected
set_of(set_of(U0()))    // rejected: set_of expects Universe, not Set
class_level(U0())       // rejected: class_level expects Class
```

The system does not treat these as false values. They are ill-typed universe-level
operations.

## New capabilities

```text
can_universe_level
can_form_set
can_form_class
can_lift_universe
can_set_reason
can_class_reason
```

## New diagnostic

```text
UniverseLevelError[E0901]
```

## MVP functions

```text
U0() / universe0()
U1() / universe1()
U2() / universe2()
universe_succ(u)
set_of(u)
class_of(u)
set_lives_in(set)
class_level(class)
```

`set_lives_in(set_of(U0()))` returns exact Nat `1` in the MVP runtime.
`class_level(class_of(U0()))` returns exact Nat `0`.

## Future work

- typed universe declarations in source syntax;
- `Set<T : U n>` rather than only `set_of(U n)`;
- proper class semantics;
- cumulative vs non-cumulative universes;
- universe-polymorphic definitions;
- formal metatheory for universe soundness.
