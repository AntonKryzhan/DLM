# v0.67 — Prelude Evaluation / Small-Step Algebraic Semantics

DLM now has a small-step evaluation boundary for the checked standard algebraic prelude.

This layer evaluates only canonical `StandardPreludeContract` operations that are already `verified_checked`:

```text
Nat / Bool / Option / Result / List / Sequence value
        + verified StandardPreludeContract
        + explicit fuel
        -> PreludeEvaluationReport
```

## Main law

```text
PreludeEvaluationReport != Proof
PreludeEvaluationReport != Theorem
PreludeEvaluationReport != TruthClaim
PreludeEvaluationReport != RuntimeWitness
PreludeEvaluationReport != hidden runtime execution
```

The evaluator is intentionally narrow. It performs deterministic algebraic steps for canonical prelude operations and records symbolic applications for user-supplied functions in `map`/`fold` boundaries. It does not execute arbitrary function bodies.

## Covered operations

```text
nat.add
nat.eq
bool.and
bool.not
option.map
result.map
list.length
sequence.length
sequence.index
list.map
sequence.map
list.fold
sequence.fold
```

## Statuses

```text
evaluated
symbolic_evaluated
rejected_contract
rejected_input
rejected_fuel
```

## Fuel model

Single-step primitive operations consume one step.

Collection traversal consumes fuel equal to collection length:

```text
list.map      steps = len(list)
sequence.map  steps = len(sequence)
list.fold     steps = len(list)
sequence.fold steps = len(sequence)
```

If `fuel_limit < required_steps`, the report is `rejected_fuel`.

## Explicit boundaries

`sequence.index` never throws an implicit runtime exception. It returns an explicit option boundary:

```text
sequence.index(seq, i) = Some(value) if in bounds
sequence.index(seq, i) = None<T>     if out of bounds
```

`option.map` and `result.map` preserve algebraic shape:

```text
option.map(Some(x), f) = Some(symbolic_apply(f, x))
option.map(None, f)    = None

result.map(Ok(x), f)   = Ok(symbolic_apply(f, x))
result.map(Err(e), f)  = Err(e)
```

## Why symbolic application

The standard prelude contract says that an operation is pure, total and budget-bounded. It does not install hidden compiler magic for arbitrary function bodies.

Therefore `map` and `fold` evaluation records bounded symbolic application unless a later kernel proves a concrete body reduction rule.

## Tests

```powershell
cargo test -p dlm_core --test prelude_eval
```

The test suite covers deterministic Nat/Bool evaluation, sequence length/index, option/result map shape preservation, explicit fuel rejection for collection map, contract rejection, proof/theorem/truth/runtime evidence rejection, taint preservation and stable exports.
