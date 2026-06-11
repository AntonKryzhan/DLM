# DLM v0.65 — Termination / Normalization Budget Unification

`v0.65` unifies the bounded-computation surface introduced by earlier layers:

```text
rewrite normalization fuel
traversal fuel
recursion fuel
        ↓
unified computation budget
```

The rule is strict:

```text
bounded computation != hidden recursion
bounded computation != unbounded normalization
bounded computation != theorem
bounded computation != proof
bounded computation != truth
bounded computation != runtime witness
```

## Objects

```text
ComputationBudgetContract
BudgetUseReport
TerminationBudgetReport
```

A budget contract declares separate limits and a total limit:

```text
rewrite_limit
traversal_limit
recursion_limit
total_limit
```

The declared domain limits must fit inside the total budget. This prevents a program from being locally bounded per subsystem while globally unbounded.

## Status values

```text
verified_unified
open
downgraded
rejected_budget_exceeded
rejected_inconsistent
```

`verified_unified` requires every participating subsystem to stay inside its own budget and inside the total budget.

`open` means there is an explicit unresolved termination/fuel obligation, for example fuel-only recursion.

`downgraded` preserves Axiom/Oracle/Unsafe taint and effectful/partial traversal or recursion boundaries.

`rejected_budget_exceeded` means the computation consumed more steps than its declared budget.

`rejected_inconsistent` means a supplied report is internally inconsistent.

## Accounting model

```text
rewrite_used   = sum(rewrite_normalization.step_count)
traversal_used = sum(map.len) + sum(fold.len)
recursion_used = sum(recursive_call.fuel_before - recursive_call.fuel_after)
total_used     = rewrite_used + traversal_used + recursion_used
```

This gives the optimizer and future native backends a single bounded-computation ledger instead of three unrelated fuel systems.

## Soundness boundary

A budget report is an audit artifact. It is not a proof of the program result and it is not a theorem.

```text
TerminationBudgetReport != StaticProof
TerminationBudgetReport != Theorem
TerminationBudgetReport != TruthClaim
```

The report says only that the involved bounded computation stayed inside the declared budget, and that all taint remains visible.

## Why this matters

Without a unified budget, the language can accidentally become bounded in each local subsystem while still unbounded globally:

```text
rewrite fuel ok
traversal fuel ok
recursion fuel ok
but combined computation explodes
```

`v0.65` prevents that class of architectural drift.
