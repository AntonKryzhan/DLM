# DLM — Deductive Logic Machine

## Origin story

DLM began as an unusual research experiment.

The starting point was not a normal question about syntax, compilers, or software architecture. The starting point was a much deeper question:

> “If a neural network had to invent mathematics from scratch, how would it make mathematics better?”

Not “better” in the sense of replacing classical mathematics, but better in a different sense:

> more explicit;
> more traceable;
> more machine-readable;
> more verifiable;
> more honest about assumptions;
> more precise about the boundary between truth, proof, computation, and trust.

The project began with a request to an AI system: imagine mathematics rebuilt from the ground up, not as a collection of isolated formulas, but as a living structure where every object knows what it is, where it came from, how it was derived, what theory it belongs to, whether it was proven or assumed, and how much trust should be assigned to it.

From this experiment, the idea of DLM appeared.

At first, it was a philosophical and mathematical question. But gradually it became clear that such a system needed a concrete language. A language where mathematical objects would not be passive symbols, and programs would not be just executable instructions. They would also carry proof information, validation metadata, provenance, theory context, and derivation history.

That is how DLM became a programming language.

DLM is therefore not only a technical project. It is an attempt to explore a different view of mathematics: mathematics as a structured, auditable, proof-aware, machine-checkable system.

The central idea is simple:

> A result should not only exist.
> It should be able to explain itself.

DLM tries to answer questions that ordinary programming languages usually ignore:

> Where did this value come from?
> Was it computed, proven, assumed, imported, or derived?
> Which theory makes it valid?
> What is its proof status?
> What is its trust level?
> Can the system check it?
> Can the system explain how it was obtained?

This origin defines the whole language.

DLM grew out of the idea that a future mathematical programming language should not separate computation from meaning. It should connect code, logic, proof, trust, validation, and symbolic structure into one coherent system.

---

## What is DLM?

**DLM** is an experimental programming language and formal reasoning system built around one central idea:
programs should not only execute — they should also carry explicit mathematical meaning, proof status, trust level, provenance, validation history, and logical boundaries.

DLM is not just another general-purpose language. It is a research language for working with computation, logic, proof objects, symbolic structures, and a new style of constructive mathematics.

The project started as part of the **ЯРД / DLM** research line: an attempt to design a language where mathematical truth, program execution, proof construction, and validation metadata are treated as first-class concepts.

---

## Core idea

Most programming languages answer the question:

> “Can this program run?”

DLM also asks:

> “What does this program mean?”
> “What theory does it belong to?”
> “Was this value proven, assumed, derived, checked, or trusted?”
> “Can the system explain how this result was obtained?”
> “Where is the boundary between computation, proof, and truth?”

In DLM, a value is not only a runtime object. It may also have a **passport**: a structured description of its logical origin, proof construction, validation level, trust level, capabilities, theory context, and derivation history.

Example of the kind of metadata DLM is designed to reason about:

```text
ProofTerm<true_intro>
construction=ProofFinite
cost=ProofRequired
trust=Checked
provenance=InternalDerived
validation=StaticChecked
theory=Meta
caps={can_symbolic_print, can_proof_kernel_check}
history=[proof_kernel:term:true_intro]
```

This means that the language is not limited to computing values. It can also track how those values were justified.

---

## Why DLM exists

Modern programming languages are very powerful, but they usually separate several things that, mathematically, belong together:

* code;
* types;
* proofs;
* symbolic terms;
* validation;
* trust;
* execution;
* derivation history;
* theory boundaries.

DLM tries to combine these layers into one coherent system.

The goal is to build a language where mathematical objects, proofs, programs, and symbolic transformations can live in the same environment without losing their origin or logical meaning.

---

## A new view of mathematics

DLM explores a more constructive and operational view of mathematics.

Classical mathematics often describes objects abstractly:

> “There exists an object with these properties.”

DLM prefers a more explicit form:

> “Here is the object.”
> “Here is its type.”
> “Here is the theory where it is valid.”
> “Here is the proof term.”
> “Here is the trust level.”
> “Here is the validation path.”
> “Here is the history of how it was derived.”

This gives mathematics a machine-readable structure.

In this sense, DLM is not trying to replace mathematics. It tries to make mathematical reasoning more traceable, auditable, programmable, and mechanically checkable.

The language is based on the idea that mathematical truth, proof, and computation should not be mixed together silently. They should be represented explicitly.

A statement may be true but not proven inside a given system.
A value may be computed but not trusted.
A term may be derived but not validated.
A proof may exist only within a particular theory.
An assumption may be useful but should still be marked as an assumption.

DLM makes these distinctions visible.

---

## Main concepts

### 1. Proof-aware values

A value in DLM may carry information about whether it was:

* constructed;
* inferred;
* assumed;
* checked;
* externally imported;
* internally derived;
* statically validated;
* dynamically validated.

This makes the language suitable for systems where the origin of a result matters as much as the result itself.

---

### 2. Proof passports

A proof passport is metadata attached to a term or value.

It can describe:

* construction mode;
* proof cost;
* trust level;
* provenance;
* validation status;
* theory;
* source location;
* capabilities;
* derivation history.

This allows DLM to distinguish between a value that was merely computed and a value that was mathematically justified.

---

### 3. Theory-aware programming

DLM treats theories as explicit contexts.

A theorem, type, value, or proof term may belong to a specific theory. This makes it possible to reason about the boundaries between different mathematical systems.

For example, a statement valid in one theory may not automatically be valid in another. DLM is designed to make such boundaries visible.

---

### 4. Boundary between truth and provability

One of the key ideas of DLM is the distinction between:

* truth;
* provability;
* computability;
* validation;
* trust.

In ordinary programming, these distinctions are usually hidden. In DLM, they are part of the language design.

This is especially important for formal mathematics, theorem proving, symbolic computation, AI reasoning, and verifiable software.

---

### 5. Symbolic computation

DLM is designed to support symbolic terms and structured reasoning, not only low-level execution.

This makes it closer to proof assistants, theorem provers, and symbolic mathematics systems than to ordinary scripting languages.

---

## How DLM differs from other languages

### Compared to Rust

Rust focuses on memory safety, ownership, lifetimes, and zero-cost abstractions.

DLM focuses on proof metadata, logical structure, validation, and formal meaning.

Rust answers:

> “Is this memory-safe and efficient?”

DLM asks:

> “Is this logically justified, where did it come from, and under which theory is it valid?”

DLM is currently implemented in Rust, but its goals are different from Rust itself.

---

### Compared to Python

Python is flexible, practical, and easy to use.

DLM is stricter, more formal, and more mathematical.

Python is good for fast development and general automation.
DLM is aimed at formal reasoning, proof tracking, symbolic computation, and auditable mathematical structures.

Python usually trusts the programmer at runtime.
DLM tries to make trust itself part of the language model.

---

### Compared to Haskell

Haskell is based on strong static typing, purity, functional programming, and advanced type theory.

DLM shares some interest in types and formal structure, but it puts more explicit emphasis on proof passports, provenance, trust, validation, and theory boundaries.

Haskell has powerful abstractions.
DLM tries to make the logical status of every important object explicit.

---

### Compared to Lean, Coq, Agda, Isabelle

Proof assistants such as Lean, Coq, Agda, and Isabelle are mature systems for formal proof development.

DLM is not a replacement for them.

Instead, DLM explores a language design where proof metadata, symbolic execution, program structure, and trust information are integrated directly into the programming model.

DLM is more experimental and less mature, but it is also freer to explore new ideas.

---

## Advantages

DLM is designed to provide:

* explicit proof metadata;
* traceable derivation history;
* theory-aware validation;
* separation between truth, proof, trust, and computation;
* symbolic representation of mathematical terms;
* machine-checkable reasoning structures;
* a foundation for auditable mathematical and logical systems;
* a possible bridge between programming languages and proof systems;
* a new way to think about mathematics as executable, inspectable structure.

---

## Limitations

DLM is experimental.

Current limitations include:

* incomplete language ecosystem;
* limited tooling compared to mature languages;
* small standard library;
* evolving syntax and semantics;
* limited documentation;
* no large production ecosystem yet;
* research-oriented architecture;
* possible breaking changes during development.

DLM should currently be treated as a research prototype, not as a production replacement for established languages.

---

## What DLM can be useful for

Potential areas of use:

* formal logic experiments;
* proof-carrying computation;
* symbolic mathematics;
* theorem-proving research;
* verified DSLs;
* educational mathematical systems;
* AI reasoning audit layers;
* language design experiments;
* trust-aware computation;
* mathematical knowledge representation.

---

## Repository structure

```text
crates/      Rust crates implementing the DLM core and CLI
docs/        Documentation and design notes
examples/    Example DLM programs
tests/       Test cases
Cargo.toml   Rust workspace configuration
yard.toml    Project configuration
```

---

## Running checks

Example:

```bash
cargo run -p dlm_cli -- check examples/valid/provability_truth_boundary.dlm
```

Expected result:

```text
DLM check: examples/valid/provability_truth_boundary.dlm

OK
```

---

## Development status

DLM is under active development.

The current focus is on:

* core language checking;
* proof term representation;
* passports and validation metadata;
* symbolic printing;
* proof kernel checking;
* example programs;
* theory boundaries;
* static validation.

---

## Philosophy

DLM is based on the idea that future programming languages may need to carry more than executable instructions.

They may need to carry:

* meaning;
* proof;
* origin;
* trust;
* logical context;
* validation history.

A program should not only produce an answer.
It should also be able to explain the status of that answer.

DLM is an experiment in that direction.

---

## License

The license is currently defined by the repository owner.
## v0.31 — Reflection / Self-Reference Guard

DLM now rejects dangerous reflection and self-reference forms at the semantic boundary instead of allowing them to fall through as ordinary missing functions. Reflection is explicit: `reflection_claim(...)` requires a `kind = reflection` bridge, and any intentional reflective/self-referential truth lift must be axiom-tainted.
### v0.31 runtime note

Reflection examples that call `prove(...)` are checker-level examples. Run-time smoke testing for v0.31 uses `examples/valid/reflection_runtime_symbolic_guard.dlm`, because proof construction remains static-only.

## v0.34 — ID / Resolver Skeleton

DLM now has the first explicit ID and resolver foundation for future HIR / ResolvedHIR passes.

Added architectural pieces:

```text
FileId / ModuleId / TheoryId / ValueId / TypeId / BridgeId / ProofId
IdAllocator
Resolver
ResolvedModule / ResolvedTheory / ResolvedValue / ResolvedBridge
SymbolTable
```

This does not change the public `.dlm` syntax or checker behavior yet. It prepares the project to move away from string-only semantic references before imports, aliases and multi-file project checking are expanded.

## v0.35 — Checker Orchestration / First Pass Split

DLM now records an explicit checker pass pipeline in `CheckReport`:

```text
raw_ast_accepted -> name_resolution -> legacy_checker
```

The existing checker still performs the semantic work, but the frontend resolver is now part of the checked path. If name resolution fails, the legacy checker is skipped instead of running over an invalid symbol graph. This prepares the project for future HIR / ResolvedHIR / TypedIR / PassportIR layers.

## v0.36 — Property-Based Invariant Tests

DLM now has the first property-style invariant test layer for the semantic core.

The new tests enumerate the finite trust lattice and bridge taxonomy to verify that central laws remain stable:

```text
trust join is monotone and associative
policy thresholds are prefix-closed
BridgeProfile matches bridge_law
quote remains syntax-only
transport/migration/materialize do not preserve proof or truth by default
soundness remains Axiom-tainted
unsafe/unknown bridges remain Unsafe-tainted
passport derivations do not lower trust
HistoryChain preserves order and multiplicity
```

Command:

```bash
cargo test -p dlm_core --test property_invariants
```

## v0.37 — Meta-Level Stratification foundation

DLM now has an explicit internal meta-level foundation:

```text
M0 — object level
M1 — meta level
M2 — meta-meta level
```

The new core rule is that syntax/provability/truth/self-reference of a level may only be observed from a strictly higher level. This keeps reflection from becoming an implicit shortcut from object syntax to truth or proof.

`meta_quote_passport(...)` produces a `Term<...>` passport only. It does not create a `TruthClaim`, `Provable` or `StaticProof`, and it does not clean axiom/oracle/unsafe taint.

## v0.38 — Statement / Theorem foundation

DLM now has an internal declaration layer for `Statement`, `Theorem`, `Goal` and `Hypothesis` passports.

This does not yet add new `.dlm` syntax. It prepares the proof-assistant layer while preserving the existing law:

```text
Statement<P> != Theorem<name:P>
Theorem<name:P> != StaticProof<P>
RuntimeWitness<P> != StaticProof<P>
```

A theorem can be built from `StaticProof` evidence, or admitted explicitly as an axiom-tainted theorem. Raw proof terms and runtime witnesses do not close theorems.


### v0.39 proof-context foundation

DLM now includes an internal `ProofContext` foundation for future proof-assistant work. The public `.dlm` syntax is unchanged; the new API models goals, hypotheses, proof obligations and explicit theorem closure.


### v0.40 tactic-script foundation

`v0.40.0` adds an internal tactic-script layer above `ProofContext`.

It introduces `TacticScript`, `TacticCommand`, `TacticScriptReport`, and `execute_tactic_script(...)` for typed proof orchestration. The public `.dlm` syntax is unchanged.

The protected invariant is:

```text
closing tactic must be final
```

A script may keep a goal open with obligations, close it with exact `StaticProof`, or close it by explicit axiom admission that remains visibly `TrustLevel::Axiom`.

### v0.41 proof-certificate foundation

`v0.41.0` adds an internal proof-certificate layer above `ProofClosure` and `TacticScriptReport`.

It introduces `ProofCertificate`, `ProofCertificateStatus`, `certificate_from_closure(...)`, `certificate_from_tactic_report(...)` and `verify_certificate_against_theorem(...)`.

The protected invariant is:

```text
closed ProofClosure<Theorem<name:P>> => ProofCertificate<name:P>
```

Open goals and open obligations cannot emit certificates. Axiom-admitted closures remain visibly tainted as `AxiomAdmitted` and `trust>=Axiom`.


### v0.42.0 — Proof Certificate Audit / Export Foundation

`v0.42.0` adds a deterministic, dependency-free audit/export layer for proof certificates.

New internal API:

- `export_certificate_text(...)`
- `export_certificate_text_unchecked(...)`
- `audit_certificate_against_theorem(...)`
- `render_certificate_audit_report(...)`
- `CertificateAuditReport`
- `CertificateAuditStatus`

The layer gives certificates a stable textual representation and a structured audit report, while preserving the existing rule that certificates are audit artifacts rather than proofs.


### v0.43.0 — Equality Proof / Rewrite Foundation

`v0.43.0` adds a typed equality/rewrite foundation. It introduces `EqProof`, `RewriteRule`, `RewriteTrace`, and `RewriteCertificate` as core artifacts while leaving `.dlm` syntax and runtime behavior unchanged.

The key boundary is explicit:

```text
Bool equality result != EqProof<A,B> != RewriteCertificate<A,B>
```

Rewrite certificates preserve rule order and trust taint. Axiom-derived equality remains visibly `trust=Axiom` after rewriting.
