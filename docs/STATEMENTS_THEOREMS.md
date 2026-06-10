# Statements and Theorems

`v0.38.0` adds the first explicit declaration layer for mathematical statements, goals, hypotheses and theorems.

This patch is intentionally a foundation layer. It does not add new surface `.dlm` syntax yet and it does not replace the legacy checker. It gives later HIR / ProofIR / PassportIR passes a typed vocabulary for theorem-like objects.

## Objects

```text
Statement<P>
Theorem<name:P>
Goal<P>
Hypothesis<P>
```

These are deliberately separate from existing proof and truth objects:

```text
Statement<P>   != Theorem<name:P>
Theorem<name:P> != StaticProof<P>
Goal<P>        != Theorem<name:P>
Hypothesis<P>  != StaticProof<P>
```

## Construction API

The foundation API lives in:

```text
crates/dlm_core/src/statement.rs
```

Important functions:

```rust
statement_passport(theory, proposition)
goal_passport(theory, proposition)
hypothesis_passport(theory, proposition, source)
theorem_from_static_proof(theory, name, statement, proof, line)
axiom_theorem(theory, name, statement, line)
```

## Main laws

### Statement is not theorem

A statement only records a proposition-shaped object. It does not prove it.

```text
Statement<P> does not imply StaticProof<P>
Statement<P> does not imply Theorem<n:P>
```

### Theorem requires checked proof or explicit axiom status

A theorem can be constructed from static proof evidence:

```text
Statement<P> + StaticProof<P> -> Theorem<n:P>
```

A theorem can also be admitted as an axiom, but then the passport is visibly axiom-tainted:

```text
Statement<P> -> Theorem<n:P> with trust=Axiom
```

### Runtime witness is not static proof

Runtime evidence remains a different kind of object:

```text
RuntimeWitness<P> != StaticProof<P>
```

`theorem_from_static_proof(...)` rejects runtime witnesses and raw proof terms.

### Hypothesis is assumption-local

A hypothesis is useful inside a future proof context, but it is not exported as a theorem by itself.

```text
Hypothesis<P> != Theorem<n:P>
```

The MVP marks hypothesis passports as assumption-tainted so future proof-context accounting can keep local assumptions visible.

## Why this matters

Before this layer, DLM had `Prop`, `Provable`, `StaticProof`, `TruthClaim`, `RuntimeWitness` and several axiom bridges, but no explicit declaration object for a human-facing theorem layer.

`v0.38.0` prepares the next proof-assistant steps:

```text
Statement -> Goal -> HypothesisSet -> ProofContext -> Theorem
```

without weakening the current proof kernel or trust model.
