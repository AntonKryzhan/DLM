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


## Stage readiness model

DLM development now tracks every major stage with three readiness dimensions:

```text
Local readiness        — does the current implementation compile, test and document correctly?
Architectural readiness — does it fit the long-term passport/proof/trust/audit design?
Fundamental readiness   — has it survived deeper mathematical, kernel, stdlib, runtime and compiler pressure?
```

This is important because `moving to the next stage` does not mean the previous stage is mathematically perfect. It means the previous stage has passed a strong enough MVP gate to be stress-tested by the next layer.

Current interpretation:

```text
Metamathematical foundation: strong MVP gate, not absolute final theory.
Ordinary mathematics: next active construction layer.
Future proof kernel, stdlib, runtime and compiler tracks will feed pressure back into the foundation.
```

See `docs/STAGE_READINESS_MODEL.md`.

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


## v0.44.0 — Rewrite Normalization / Audit Foundation

The core now has a bounded rewrite-normalization layer over typed equality proofs and rewrite rules. It can build stable normalization reports and audit that a rewrite certificate matches the report endpoints and taint state. No `.dlm` syntax or runtime behavior changed.


## v0.45.0 — Nat Induction MVP

DLM now has a core-level Nat induction foundation. This adds internal proof artifacts for `InductionScheme<Nat,P>`, `BaseCase<P(0)>`, `StepCase<forall n:Nat. P(n) -> P(succ(n))>` and `InductionProof<forall n:Nat. P(n)>`.

This is not yet public `.dlm` proof syntax. It is a checked core layer for future theorem/tactic automation.

The protected boundary is explicit: a runtime witness or raw proof term cannot be used as an induction case, and axiom-tainted base/step cases remain visible in the resulting induction proof.


### v0.46 — Module / Import System Foundation

DLM now has a core model for module manifests, import graphs, public/private exports, acyclic dependency validation, and export passports. This is a semantic foundation only: `.dlm` import syntax and project-level CLI wiring are intentionally left for a later patch.


## v0.47.0 — Module Interface / Import Audit Foundation

Added stable module interface artifacts on top of the v0.46 module/import system.

Changed/added files:

- `crates/dlm_core/src/module_interface.rs`
- `crates/dlm_core/tests/module_interfaces.rs`
- `docs/MODULE_INTERFACE_AUDIT.md`

New diagnostic kind:

- `ModuleInterfaceError[E0918]`

Protected laws:

- module interfaces are audit contracts, not theorem/proof/truth evidence;
- private interface entries cannot satisfy imports;
- import audits require explicit import edges in the resolved import graph;
- interface fingerprints are deterministic and change when exported evidence or visibility changes;
- exported trust taint is preserved in the interface summary.

No `.dlm` syntax, checker behavior or runtime behavior changed.


## v0.48.0 — Metatheory Dependency / Axiom Registry Foundation

Adds explicit axiom registries and dependency audit reports. The core law is now:

```text
AxiomRegistry<T> + ordered DependencyEntry[] => MetatheoryDependencyAuditReport
```

This makes axiom/oracle/unsafe dependencies visible before the later proof-kernel hardening phase.

## v0.49.0 — Metatheory Closure Report Foundation

This patch continues track **1) Metamathematical foundation** by adding a global closure report layer over verified dependency audits.

New core concepts:

- `MetatheoryClosureReport`;
- `MetatheoryClosureStatus::{Closed, Open, Rejected}`;
- `ClosureObligation`;
- `ClosureObligationKind`;
- `metatheory_closure_report(...)`;
- `require_closed_metatheory_closure(...)`;
- `metatheory_closure_report_passport(...)`;
- `export_metatheory_closure_report(...)`.

Main law:

```text
verified dependency audit + closed obligations => closed metatheory closure report
```

Open obligations keep closure open. Rejected dependency audits reject closure. Axiom/oracle/unsafe taint remains visible.

## v0.50.0 — Conservative Extension Audit Foundation

This release adds the first conservative-extension audit layer for the metamathematical foundation track.

New core objects:

- `ConservativeExtensionAuditReport`;
- `ConservativeExtensionStatus`;
- `PreservedTheorem`;
- `preserved_theorem(...)`;
- `audit_conservative_extension(...)`;
- `require_verified_conservative_extension_audit(...)`;
- `conservative_extension_audit_passport(...)`;
- `export_conservative_extension_audit_report(...)`.

Main invariant:

```text
Closed(base) + Closed(extension) + PreservedTheorem[] => ConservativeExtensionAudit<verified>
```

The audit rejects theorem renaming, proposition mutation, non-closed bases, empty preservation evidence, and duplicate preservation witnesses. New assumptions are allowed only as visible audit entries and preserve axiom/oracle/unsafe taint.

## v0.51 — Theorem Dependency Graph / Global Metatheory Inventory

DLM now has a global metatheory inventory layer. It collects theorem nodes, dependency-audit nodes, closure-report nodes, conservative-extension audit evidence and graph edges into a single fingerprinted report.

The new layer makes theorem foundations explicit:

```text
TheoremDependencyNode[] + TheoremDependencyEdge[] + ConservativeExtensionAudit[]
=> GlobalMetatheoryInventoryReport
```

The inventory is an audit object, not a theorem or proof. It preserves axiom/oracle/unsafe taint and rejects hidden graph dependencies.

## v0.52 — Soundness Boundary Ledger

DLM now has a dedicated soundness boundary ledger for explicit soundness/reflection/consistency/truth-lift/oracle/unsafe assumptions. The ledger is an audit artifact and does not become a theorem or static proof.

## Strategic direction — high-performance native compilation

DLM also has a long-term performance direction: a restricted `DLM-Fast` subset should eventually compile to native code at C/C++/Rust level.

The strategy is not to carry proof/passport/audit metadata through every hot runtime instruction. The strategy is:

```text
proof-carrying compile time;
proof-erased runtime.
```

DLM should use passports, proofs, capabilities, and invariants before code generation, then erase verified evidence and emit minimal machine-level representations for hot paths.

Future components:

```text
CoreIR / FastIR
OptimizationContractIR
ProofErasure
PassportErasure
Effect/capability inference
Ownership/region memory model
LLVM / MLIR / Cranelift backend
SIMD / PGO / LTO / ASM inspection
```

The potential advantage is proof-guided optimization: if DLM can prove bounds, noalias, no allocation, fixed shapes, sortedness, nonzero values, or static loop bounds, then a backend can remove checks and specialize code more aggressively than a normal compiler can from syntax alone.

This is a late-stage direction and does not replace the current priority: finishing the metamathematical foundation first.


### v0.53 — Trusted Base Closure / Final Metatheory Foundation Gate

DLM now has a final metatheory-foundation gate: `TrustedBaseClosure`. It combines the axiom registry, dependency audit, metatheory closure report, global theorem inventory, and soundness boundary ledger into one auditable trusted-base closure artifact. This does not create a theorem or proof; it records whether the metamathematical foundation is closed, open, or rejected under explicit evidence.

### v0.54 — Metatheory Foundation Exit Gate

DLM now has a final metatheory-foundation exit report. `MetatheoryFoundationExitReport` collects the required phase-1 criteria and determines whether the project can move from the metamathematical foundation phase into ordinary language mathematics. This is an audit gate, not a theorem or truth claim.


## v0.55 — Logical Connectives / Quantifier Foundation

DLM now begins the ordinary mathematics layer with first-class logical formula objects and quantifier objects. `LogicalFormula` and `QuantifiedFormula` are proposition-level objects only: they are not theorems, static proofs, truth claims or runtime witnesses. This preserves the existing proof/truth/runtime boundary while preparing the language for `forall`, `exists`, implication, conjunction, disjunction and negation.


## v0.56 — Substitution / Alpha-Equivalence Foundation

DLM now has the first safe variable-scope layer for ordinary mathematics. `VariableScopeReport`, `AlphaEquivalenceReport` and `SubstitutionReport` make bound/free variables, binder renaming and capture-avoiding substitution explicit audit objects instead of implicit string operations.

Main rule:

```text
substitution is not proof, not theorem, not rewrite certificate, and not truth
```

This prepares the language for function types, quantifier proof rules and dependent typing without letting variable capture become a hidden soundness hole.

<!-- RUNTIME_HARDWARE_LAYERING_PRINCIPLE_BLOCK -->
## Runtime / Hardware Layering Principle

DLM keeps rich proof/passport/trust semantics at the source and compiler layers, while runtime and hardware execution must remain compact and dense.

```text
Meaning-rich above.
Execution-dense below.
```

This principle prevents full proof objects, full history chains, and full passports from being carried into GPU kernels, SIMD lanes, dense buffers, or hot runtime loops.

<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->
## DLM Architectural Laws

The project has an explicit architectural constitution: `docs/DLM_ARCHITECTURAL_LAWS.md`.

The core formula is:

```text
Meaning-rich above.
Execution-dense below.
Audit-complete backward.
```

<!-- V0_57_FUNCTION_LAMBDA_APPLICATION_BLOCK -->
## v0.57.0 — Function Type / Lambda / Application Foundation

DLM now has an ordinary function foundation for Stage 2 — ordinary mathematics of the language.

Added:

```text
FunctionType
LambdaTerm
ApplicationTerm
ApplicationStatus
```

Main law:

```text
Function application is not theorem proving.
LambdaTerm is not StaticProof.
ApplicationTerm is not TruthClaim.
```

Readiness delta:

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         18–24% -> 24–32%
Architectural readiness: 40–50% -> 44–56%
Fundamental readiness:   25–35% -> 28–40%
```


<!-- V0_58_FUNCTION_CONTRACTS_BLOCK -->
## v0.58.0 — Function Contract / Purity / Totality Boundary

Adds `docs/FUNCTION_CONTRACTS.md` and the first contract layer above ordinary functions. Function contracts record purity, explicit effect boundaries, totality status, static evidence, open obligations and honest downgrade status.

Main law:

```text
FunctionContract is not theorem/proof/truth.
It is an audit/control object for future optimization, scheduling and assurance modes.
```


## v0.59.0 — Product / Sum / Record Type Foundation

Added `docs/STRUCTURAL_TYPES.md` and the core structural type layer: ProductType/ProductTerm, SumType/SumInjection and RecordType/RecordTerm/RecordProjection. This layer preserves trust taint, rejects proof/truth/runtime objects as ordinary structural values, and prepares future layout/ABI-aware records.


## v0.60.0 — Structural Elimination / Pattern Boundary

Adds explicit product elimination, sum case elimination and record pattern reports. Structural elimination is value-level and does not become proof, theorem or truth. Taint is preserved; proof/truth/theorem/runtime smuggling is rejected. See `docs/STRUCTURAL_ELIMINATION.md`.


## v0.61.0 — Option / Result / Partiality Type Foundation

Adds explicit `Option<T>`, `Result<T,E>` and `PartialityReport` objects for ordinary mathematical partiality. Partial functions now have a typed target representation instead of hidden null/exception semantics. Readiness delta: Local 42–50% -> 48–56%, Architectural 60–72% -> 64–76%, Fundamental 42–54% -> 46–58%.

### v0.62 — List / Sequence Type Foundation

Adds explicit finite `List<T>` and `Sequence<T>` semantic reports with typed elements, explicit length, Option-style indexing boundary, taint preservation, and proof/truth/runtime rejection.

### v0.63 — Fold / Map / Traversal Boundary

`v0.63` adds explicit finite traversal reports for `map` and `fold` over `List`/`Sequence` values. Traversals are bounded by collection length and explicit fuel, consume `FunctionContract` evidence, preserve taint, and never become proofs, theorems, truth claims, runtime witnesses or hidden normalization loops.

### v0.64 — Recursion / Well-Founded Fuel Boundary

Adds explicit recursion schemes, recursive-call reports, well-founded measure classes and fuel boundaries. Recursion is not proof, theorem, truth, runtime witness, or hidden unbounded normalization.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.


## v0.66.0 — Standard Algebraic Prelude Foundation

The project now has a checked algebraic prelude boundary for Nat/Bool/Option/Result/List/Sequence operations. Standard prelude operations are explicit contracts with signatures, budgets, taint propagation and stable fingerprints.


## v0.67.0 — Prelude Evaluation / Small-Step Algebraic Semantics

DLM now has a small-step evaluator for verified standard prelude contracts. Primitive Nat/Bool/length/index operations reduce deterministically; Option/Result/List/Sequence map/fold preserve algebraic shape and use bounded symbolic application instead of executing arbitrary hidden function bodies. See `docs/PRELUDE_EVALUATION.md`.
