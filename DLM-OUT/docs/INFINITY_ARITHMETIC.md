# DLM/ЯРД v0.28 — Extended Infinity Mathematics

This document records the MVP rules for typed infinities beyond the original
`Infinity<cardinal>` / `Infinity<ordinal>` layer.

## Law: no bare infinity

The untyped form is intentionally rejected:

```dlm
let x = infinity() // ERROR
```

Every infinite object must state its mode explicitly.

## Constructors

```dlm
aleph0()              // Infinity<cardinal>
omega()               // Infinity<ordinal>
limit_omega()         // Infinity<limit>
potential_infinity()  // Infinity<potential>
class_infinity(c)     // c must be Class<U n>
universe_infinity(u)  // u must be Universe<U n>
```

`Infinity<class>` and `Infinity<universe>` deliberately require explicit
`Class`/`Universe` inputs. They are not created from a vague “all objects” form.

## Arithmetic modes

```dlm
cardinal_add(a, b) // both operands must be Infinity<cardinal>
ordinal_add(a, b)  // both operands must be Infinity<ordinal>
```

No implicit conversion is performed between cardinals and ordinals. A cardinal
sum and an ordinal sum are different operations with different capabilities.

## Potential infinity

```dlm
let p = potential_infinity()
let p2 = potential_step(p)
```

`Infinity<potential>` models a process-style infinite object. It is not a
completed set, cardinal, ordinal, class, or universe.

## Passport events

v0.28 adds these `HistoryChain` events:

```text
created:infinity_limit
created:infinity_potential
created:infinity_class
created:infinity_universe
infinity:cardinal_add
infinity:ordinal_add
derived:infinity_succ
```
