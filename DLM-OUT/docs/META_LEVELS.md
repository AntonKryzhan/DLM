# Meta-Level Stratification

`v0.37.0` adds the first explicit meta-level foundation for DLM.

This is not yet a new user-facing `.dlm` syntax layer. It is a core API and test layer that records the law needed before reflection, theorem declarations, HIR and future proof-goal passes grow further.

## Levels

```text
M0 — object level
M1 — meta level
M2 — meta-meta level
M n — higher meta level
```

The central law is:

```text
an operation that observes syntax, provability, truth, or self-reference of level N
must run at a strict observer level > N.
```

So object-level code cannot inspect its own truth/provability directly.

## Core API

The new module is:

```text
crates/dlm_core/src/meta_level.rs
```

It defines:

```text
MetaLevelIndex
MetaStage
MetaAccess
MetaLevelContext
validate_meta_observer(...)
required_observer_level(...)
meta_level_passport(...)
object_level_passport(...)
meta_quote_passport(...)
```

## Boundary rule

This is rejected:

```text
observer = M0
object   = M0
access   = truth/provability/self-reference
```

because it would let a theory inspect itself without an explicit lift.

This is accepted:

```text
observer = M1
object   = M0
access   = syntax/provability/truth/self-reference
```

because it is a strict meta-level lift.

## Quote rule

`meta_quote_passport(...)` produces:

```text
Term<T.theory.type>
```

It does not produce:

```text
TruthClaim
StaticProof
Provable
```

This preserves the existing DLM law:

```text
syntax is not value
syntax is not proof
syntax is not truth
```

## Trust rule

Meta-quote does not clean taint.

If the quoted object is:

```text
trust=Axiom
```

then the produced term is still:

```text
trust=Axiom
```

No meta-level operation may silently turn an axiom/oracle/unsafe path into `Checked`.

## Diagnostics

The new diagnostic kind is:

```text
MetaLevelError[E0908]
```

It is used when an observer level is not strictly above the object level.

## Tests

Regression tests live in:

```text
crates/dlm_core/tests/meta_levels.rs
```

They check:

```text
level ordering;
strict-lift requirements;
object-level self-observation rejection;
meta quote produces Term only;
meta quote does not create TruthClaim or StaticProof;
meta quote preserves existing trust taint.
```
