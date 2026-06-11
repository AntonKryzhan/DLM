# DLM v0.64 — Recursion / Well-Founded Fuel Boundary

This layer introduces explicit recursion boundaries for DLM.

The core law is:

```text
recursion != hidden infinite computation
recursion != unbounded normalization
recursion != proof
recursion != theorem
recursion != truth
recursion != runtime witness
```

A recursion boundary must name:

```text
FunctionContract
measure kind
initial fuel
well-founded evidence when claiming mathematical termination
```

Supported measure classes:

```text
nat_decreasing
structural_subterm
lexicographic
fuel_only
unknown
```

`nat_decreasing`, `structural_subterm`, and `lexicographic` require explicit
StaticProof/Theorem evidence before the scheme can be `verified_well_founded`.

`fuel_only` is allowed as an operational budget, but remains `open`, not a
mathematical totality proof.

`unknown` is rejected.

Recursive calls are checked with:

```text
argument type == scheme argument type
fuel_before > 0
next_measure < previous_measure for well-founded measures
```

A rejected recursive call is represented as an audit status, not as a runtime
exception.

## Status lattice

```text
verified_well_founded
  < downgraded
  < open
  < rejected_fuel_exceeded
  < rejected_measure
  < rejected_contract
```

## Trust rule

Axiom/Oracle/Unsafe taint is preserved and visible. A tainted recursion scheme
cannot be silently upgraded to clean checked recursion.
