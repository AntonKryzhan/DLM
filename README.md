[Русский](docs/LANGUAGE_GUIDE_RU.md) | [English](docs/LANGUAGE_GUIDE_EN.md)

> **Documentation / Документация (v0.131.0):** [Руководство на русском](docs/LANGUAGE_GUIDE_RU.md) · [English language guide](docs/LANGUAGE_GUIDE_EN.md). Both include quick start, verified syntax examples, compilation, types, proofs, effects, ownership, graphics, limitations, and links to detailed technical references. The existing README below preserves historical background and version notes.

---

# DLM — Deductive Language Machine

> Current source version: **0.131.0**. DLM is an experimental language with a
> native C backend for its supported executable subset, formal reasoning, and
> bounded exact computational-mathematics libraries. The long-term goal is a
> **general-purpose programming language**, not a mandatory theorem prover for
> every program. The current engineering plan and v1.0 acceptance gates are
> in [ROADMAP.md](ROADMAP.md); implemented and open guarantees are tracked in
> [docs/LAW_STATUS.md](docs/LAW_STATUS.md). Mathematical work is separately
> tracked in [docs/MATHEMATICS_PROGRESS.md](docs/MATHEMATICS_PROGRESS.md).

v0.130 adds explicitly bounded `FnEffect` callbacks, generic callable constraints and
builtin associated type projections, ordered effectful traversals, streaming file
callbacks and file-reading HTTP handlers. See [Callback effects](docs/EFFECT_FUNCTIONS.md).

v0.99 preserves source-file identity through parsing, checked facts and native
lowering, and reports physical file/line/byte-column locations for parser and
expression-checking errors. See [Source identity and diagnostics](docs/SOURCE_IDENTITY.md).

v0.100 binds checked function contracts and record schemas by declaration ID,
passes frontend-resolved nominal types to lowering and rejects missing or
inconsistent checked declarations. See [Checked declaration identity](docs/DECLARATION_IDENTITY.md).

v0.100.1 combines the complete source tree with the additional exact-complex
text API while preserving existing ComplexQ interfaces. Build instructions and
integration scope: [Complete cumulative distribution](docs/CUMULATIVE_BUILD.md).

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

## v0.68.0 — Prelude Lowering / Verified Erasure Boundary

`PreludeLoweringReport` connects a verified prelude contract and its evaluation report to a lowering target and erasure mode. Lowering is an audit object: it may erase proof/passport metadata into a compact descriptor, but never erases trust, provenance or obligations from the audit layer. See `docs/PRELUDE_LOWERING.md`.

## v0.69.0 — Backend Capability / Lowering Target Contracts

`BackendCapabilityContract` and `BackendLoweringReport` require a backend to declare operational capabilities (`deterministic`, `pure`, `no_alloc`, `no_alias`, `vectorizable`, `gpu_resident`, ...) before it may consume a lowering report. See `docs/BACKEND_CAPABILITY.md`.

## v0.70.0 — Backend Layout / ABI Descriptor Boundary

`BackendLayoutDescriptor` and `BackendLayoutReport` map lowering targets to explicit ABI containers and reject runtime layouts that carry full semantic passports per value or per element. See `docs/BACKEND_LAYOUT.md`.

## v0.71.0 — Value Representation / Dense Runtime Descriptor

DLM now has a dense runtime descriptor boundary after backend layout validation. `DenseRuntimeDescriptor` and `DenseRuntimeReport` represent compact scalar/vector/GPU/remote payload shapes while keeping audit/proof/passport data out of hot runtime data paths.

Key rule:

```text
meaning-rich above, execution-dense below
```

## v0.72.0 — Example Corpus Integrity / GPU Checkpoint Restore Completion

Whole-corpus checking found features that were documented but never enforced. This release:

- implements the v0.21 GPU memory checkpoint/restore layer in checker and runtime (`GpuMemoryCheckpoint`, `can_restore_gpu_memory_checkpoint`, `checkpoint_gpu_memory` / `restore_gpu_memory` and aliases);
- makes three negative GPU examples fail for their documented reason instead of "unknown builtin";
- repairs `examples/valid/infinity_modes.dlm` (dangling `Core -> Meta` bridge);
- makes `dlm --version` report the workspace version;
- repairs `docs/MANIFEST.json`;
- adds `tests/example_corpus.rs`, which checks every example in its intended mode and rejects negative examples that fail only because a builtin is missing.

```text
A negative example must fail for the documented reason, not because a feature is missing.
```

See `docs/EXAMPLE_CORPUS_INTEGRITY.md`.

## v0.73.0 — Ordinary Mathematics Surface Wiring

The Stage 2 layers (functions, contracts, Option/Result, List/Sequence, products, map/fold) are now usable from `.dlm`:

```dlm
theory Collections {
    fn double(n: Nat) -> Nat = n + n
    fn add(acc: Nat, x: Nat) -> Nat = acc + x

    let xs = list(1, 2, 3, 4)
    let total = fold(map(xs, double), 0, add)
    let maybe = index(xs, 9)              // Option<Nat>: none, not an exception

    print_decimal(total)                  // 20
    print_decimal(unwrap_or(maybe, 0))    // 0
}
```

- `fn` declarations are parsed, type-checked and executed; before this release the parser silently dropped them.
- Every function gets a `FunctionContract` passport (purity, totality, effects, taint); `map`/`fold` accept only verified contracts.
- Recursion is rejected (`E0935`) until recursion schemes are exposed in syntax.
- Core fix: Stage 2 item types are canonical types (`Option<Nat>`, `List<Nat>`), not value descriptors, so `list(some(1), none(Nat))` is valid.

See `docs/ORDINARY_SURFACE.md`.

## v0.74.0 — Well-Founded Recursion, Records and Sums

```dlm
theory Geometry {
    record Point(x: Nat, y: Nat)

    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn total(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + total(tail(xs)))
    fn from_nat(n: Nat) -> Nat = n
    fn from_flag(b: Bool) -> Nat = if_then_else(b, 1, 0)

    print_decimal(fact(20))
    print_decimal(field(Point(3, 4), y))
    print_decimal(case(inr(true, Nat), from_nat, from_flag))
}
```

- Recursion needs a declared measure (`decreasing n` / `decreasing xs`). The checker proves decrease syntactically under a guard, the core `RecursionScheme` report must be `verified_well_founded` (or visibly downgraded), and the runtime enforces fuel and re-checks decrease.
- `record Name(field: Type, ...)`, positional construction and `field(r, name)` projection go through the core record reports.
- `Sum<A, B>` with `inl`, `inr`, `is_left` and `case` goes through the core sum reports.
- The runtime executes on a dedicated large stack and fails gracefully instead of aborting when the stack budget runs out.

See `docs/ORDINARY_SURFACE.md` (v0.74 section).

## v0.75.0 — Lambdas, Function Values, Higher-Order Functions, Lexicographic Measures

```dlm
theory HigherOrder {
    fn twice(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(f(x))
    fn adder(k: Nat) -> Fn<Nat, Nat> = fun(n: Nat, n + k)
    fn ack(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(m, 0), n + 1, if_then_else(nat_eq(n, 0), ack(pred(m), 1), ack(pred(m), ack(m, pred(n)))))

    let inc = fun(n: Nat, n + 1)
    print_decimal(twice(inc, 0))                                   // 2
    print_decimal(fold(list(1, 2, 3), 0, fun(a: Nat, x: Nat, a + x)))
    print_decimal(ack(2, 3))                                       // 9
}
```

- `Fn<A, R>` function types, `fun(x: T, body)` lambdas through the core `LambdaTerm`, closures with lexical capture, functions as values in lists, records and results.
- Function values are pure and total by construction; effectful functions can be called but not passed around. Taint stays visible.
- `map`, `fold` and `case` accept any function value, not only declared names.
- Lexicographic measures `decreasing (m, n)`, published as `RecursionScheme<...:lexicographic:...>`.

See `docs/ORDINARY_SURFACE.md` (v0.75 section).

## v0.76.0 — Statements, Theorems and Proofs

```dlm
theory Arithmetic {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn double(n: Nat) -> Nat = n + n

    theorem sum_to_4: nat_eq(sum_to(4), 10)
    theorem gauss_below_200(n: Nat) below 200: nat_eq(mul(2, sum_to(n)), mul(n, n + 1))
    axiom add_comm(n: Nat, m: Nat): nat_eq(n + m, m + n)
    conjecture double_monus(n: Nat) tested 1000: nat_eq(monus(double(n), n), n)
}
```

- `theorem` proves a closed proposition by computation or a bounded one (`below N`) by exhaustion, through the core statement → goal → proof context → static proof → theorem → certificate pipeline.
- `axiom` admits a statement as a visibly Axiom-tainted theorem.
- `conjecture ... tested N` records test evidence for an unbounded claim and stays an open `Goal`, never a theorem.
- False statements are refuted with a counterexample. Statements must be pure, total and independent of theory values.
- `dlm certify file.dlm` prints proof certificates with the evidence method; `dlm explain` counts proved, admitted and open statements.

See `docs/PROOF_SURFACE.md`.

## v0.77.0 — Symbolic Proofs of Unbounded Statements

```dlm
theory Symbolic {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    fn twice_add(a: Nat) -> Nat = add(a, a)

    theorem gauss(n: Nat): nat_eq(mul(2, sum_to(n)), mul(n, n + 1)) by induction n
    theorem add_spec(a: Nat, b: Nat): nat_eq(add(a, b), a + b) by induction a
    theorem twice_add_spec(a: Nat): nat_eq(twice_add(a), mul(2, a)) by normalize using add_spec
}
```

- `by normalize` and `by induction n` prove statements for **all** natural numbers through a new symbolic kernel: polynomial normal forms, definition unfolding, guarded `pred`/`monus`, beta-reduction, induction hypothesis as rewrite or linear combination.
- `using lemma, ...` reuses earlier unconditional theorems (symbolic, closed, axioms) as rewrite rules. Bounded theorems and conjectures are refused as lemmas. Axiom lemmas taint the result.
- Induction goes through the core `InductionScheme` → base/step → `InductionProof` objects.
- The kernel is fuzz-tested: thousands of random statements; every proved one is re-checked by execution, and an injected unsound rule is caught.

See `docs/PROOF_SURFACE.md` (v0.77 section).

## v0.78.0 — Inequalities, List Induction, Runtime Cross-Check

```dlm
theory Lists {
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))

    theorem fact_positive(n: Nat): fact(n) > 0 by induction n
    theorem square_ge(n: Nat): not(n > mul(n, n)) by induction n
    theorem len_is_length(xs: List<Nat>): nat_eq(len(xs), length(xs)) by induction xs
    theorem map_length(xs: List<Nat>): nat_eq(length(inc_all(xs)), length(xs)) by induction xs
}
```

- Goals and induction hypotheses may be inequalities (`a > b`, `not(a > b)`); steps are closed by nonnegative combinations of the hypothesis.
- Theorems over `List<Nat>` with `by induction xs`, through new core list induction objects (`InductionScheme<List<Nat>,P>`).
- Every symbolic theorem is re-checked by the runtime on a finite grid; a disagreement is a hard error (this also catches false axioms used as lemmas).
- New fuzzers for inequalities and lists; the combination rule is checked semantically.

See `docs/PROOF_SURFACE.md` (v0.78 section).

## v0.79.0 — Step Induction, Generalized Hypotheses, Bool Cases

```dlm
theory Advanced {
    fn half(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 0, half(monus(n, 2)) + 1))
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    fn add_acc(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add_acc(pred(a), b + 1))
    fn pick(b: Bool, n: Nat) -> Nat = if_then_else(b, n + 1, n)

    theorem half_bound(n: Nat): not(mul(2, half(n)) > n) by induction n step 2
    theorem even_or_next(n: Nat): or(even(n), even(n + 1)) by induction n step 2
    theorem add_acc_spec(a: Nat, b: Nat): nat_eq(add_acc(a, b), a + b) by induction a generalizing b
    theorem pick_ge(b: Bool, n: Nat): not(n > pick(b, n)) by normalize
}
```

- `by induction n step s` proves the bases `P(0) .. P(s-1)` and the step `n = k + s` from `P(k) .. P(k+s-1)`. This works through new core step-induction objects.
- `generalizing x, ...` makes the hypothesis hold for all values of the listed variables, so accumulator-style functions become provable.
- Bool variables are split by cases. A Bool hypothesis such as `or(even(k), even(k+1))` is used in the step.
- A lemma rewrite cycle no longer crashes the process with a stack overflow. Kernel depth is limited, and proofs run on a large-stack thread.

See `docs/PROOF_SURFACE.md` (v0.79 section).

## v0.80.0 — Modules, Theory Extension, Standard Library

```text
module examples.std_library_usage
import std.list

theory App extends StdList {
    fn total_plus_count(xs: List<Nat>) -> Nat = sum_list(inc_all(xs))
    theorem total_plus_count_spec(xs: List<Nat>): nat_eq(total_plus_count(xs), sum_list(xs) + length(xs)) by normalize using inc_all_sum
}
```

- `import` loads modules from files (`--lib <dir>`, `DLM_PATH`), with detection of cycles, missing modules and module-name mismatches.
- `theory Name extends Base` inherits functions, records, theorems and lemmas; passports keep their trust, so an imported axiom still taints.
- `private` declarations are hidden from other modules.
- Standard library in `lib/std`: `std.nat` and `std.list`, with 20 symbolically proved theorems.
- Lemmas with nested patterns (`sum_list(inc_all(xs))`) can now be used.

See `docs/MODULES.md`.

## v0.81.0 — `dlm audit`, Projects, Mutual Recursion

- `dlm audit file.dlm` shows the trusted base: the proof method of each theorem, transitive lemmas and axioms (including those from imported modules), the runtime cross-check, the status `closed` / `axiom-dependent` / `unsafe`, and a fingerprint.
- `dlm project [dir]` checks every root listed in `dlm.project`. The repository manifest covers `lib/std` and all valid examples.
- `mutual { ... }` declares mutually recursive functions with a shared decreasing measure. They are supported by the checker, the symbolic kernel and the runtime.

```dlm
theory Parity {
    mutual {
        fn even_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, odd_n(pred(n)))
        fn odd_n(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), false, even_n(pred(n)))
    }
    theorem parity_split(n: Nat): or(even_n(n), odd_n(n)) by induction n step 2
}
```

See `docs/AUDIT_AND_PROJECTS.md`.

## v0.82.0 — Multi-line Declarations, `let`, `match`

```dlm
theory Patterns {
    fn sum_list(xs: List<Nat>) -> Nat decreasing xs =
        match xs {
            [] => 0,
            cons(h, t) => h + sum_list(t)
        }

    fn clamp(n: Nat) -> Nat = match n { 0 => 0, 1 => 1, _ => 2 }

    fn quad(n: Nat) -> Nat =
        let d = n + n in
        d + d

    theorem quad_spec(n: Nat): nat_eq(quad(n), mul(4, n)) by normalize
    theorem clamp_le(n: Nat): not(clamp(n) > 2) by normalize
}
```

- Declarations can span several lines: a line continues while brackets are open or after `=`, `,`, `=>`, `:`. A proof clause `by ...` can stand on its own line.
- `let x = v in body` evaluates `v` once. Grouping parentheses `( ... )` are supported.
- `match` with patterns `0`/`succ(k)`, `[]`/`cons(h, t)`, `none`/`some(x)`, `true`/`false`, `_`/`x`. The match must be exhaustive and every arm reachable, and recursion through `succ`/`cons` passes the termination check.
- `by normalize` splits Nat variables into cases when needed (`clamp_le`).

See `docs/PATTERNS_AND_LET.md`.

## v0.83.0 — Text, Line I/O, Arguments, `fn main`

```dlm
theory WordCount {
    fn report(label: Text, n: Nat) -> Text = concat(label, concat(": ", nat_to_text(n)))
    fn main() -> Nat =
        let input = read_all() in
        let _ = print_line(report("lines", length(lines(input)))) in
        let _ = print_line(report("words", length(words(input)))) in
        0
}
```

- Text literals `"..."` with escapes `\n \t \" \\`.
- Text operations: `concat`, `text_length`, `text_eq`, `nat_to_text`, `text_to_nat`, `substring`, `lines`, `words`, `join`.
- Input and output: `read_line`, `read_all`, `args`, `print_line`.
- Entry point `fn main()`: a Nat result becomes the exit code. `dlm run file.dlm --stdin-file in.txt -- arg1 arg2`.
- Symbolic theorems can quantify over `Text`.

See `docs/TEXT_AND_IO.md`.

## v0.84.0 — Compiler: IR and C Backend

```text
dlm build examples/valid/word_count.dlm -o wc
printf 'one two\nthree' | ./wc Anton
```

- `dlm build` turns a checked program into a native executable: check, then IR, then C, then `cc -O2`. Theorems, measures and fuel are erased because they were already proved by the checker. About 100 times faster than `dlm run` on `fib(24)`.
- Supported: Nat, Bool, Text, Option, Result, List, Sequence, Product, Sum, records, functions, lambdas, `let`, `match`, mutual recursion, modules, I/O, `main`. Anything else is reported as `E0950` and is not miscompiled.
- `--emit-ir` prints the intermediate representation; `--c-only` writes only the C file.
- The compiler is checked against the interpreter on examples and generated programs: same output, same exit code.

See `docs/COMPILER.md`.

## v0.85.0 — Int: Exact Signed Integers

```text
fn diff(a: Nat, b: Nat) -> Int = int(a) - int(b)
theorem square_expand(a: Int, b: Int): int_eq(sq(a - b), sq(a) - mul(int(2), mul(a, b)) + sq(b)) by normalize
```

- New type `Int` (exact, i128 at run time; overflow is an error). Binary `-` and unary `-` exist only on Int; Nat keeps `monus`. `-7` is an Int constant, `int(n)` converts a Nat. Int and Nat are never mixed implicitly.
- Builtins `int`, `abs`, `int_eq`, `div`, `mod` (total and Euclidean: `div(a, 0) = 0`, `mod(a, 0) = a`; also for Nat), `int_to_text`, `text_to_int`.
- The symbolic kernel proves Int ring identities and decides Int comparisons only on constant differences (no sign assumptions on Int). Int theorems are cross-checked on `-5..5` and fuzzed against the runtime.
- `dlm build` compiles Int with the same semantics and the same error messages as `dlm run`.

See `docs/INTEGERS.md`.


## v0.86.0 — Compiled Runtime: Machine Values, Reclamation and Range Checks

- Pure numeric functions use native scalar parameters, locals and results; the uniform ABI uses tagged small Nat/Int values. Full u128/i128 ranges and overflow errors are preserved.
- Call regions reclaim temporary objects, retain escaping graphs and closure captures, and release old `fold` accumulators. Direct self tail calls reuse their frame and region.
- `dlm build --check-overflow` reports machine-range obligations. `--require-no-overflow` refuses a build with unresolved obligations, independently of mathematical identities proved by the kernel.
- Native Windows x64 uses MinGW-w64, Win32 threads, UTF-8 arguments and byte-exact I/O. Run `./tools/build_windows.ps1`; MSVC `cl.exe` is not a supported C backend.

Details, examples and limitations: [docs/COMPILER.md](docs/COMPILER.md).

## v0.86.1 — Survivor Groups

Compiled programs no longer trace a returned graph again at every call level: survivors of a collection form a group that is kept whole while all its roots are reachable. A closure chain of 40 000 levels now runs in 0.45 s instead of 90.7 s, with the same memory. See `docs/COMPILER.md`.

## v0.87.0 — Semantics and Typed Core (stages A and B)

```text
dlm contracts examples/valid/integers.dlm
dlm build --emit-ir examples/valid/ordinary_higher_order.dlm -o hof
```

- Normative semantics of the executable language (`docs/SEMANTICS.md`): evaluation order, laziness, effects, arithmetic (`Defined_128`), failure categories, resource policies, observable behaviour. The interpreter and the compiler are compared on it, including runtime errors.
- Typed Core: every expression has an id and an exact source span; the checker hands the type of every expression and the contract of every function to a typed IR (`DLM-IR v2`), which is verified after lowering. The scalar backend and the overflow analysis use these types instead of reconstructing them.
- `dlm contracts`: effects, termination, machine arithmetic, stack and mathematical specification per function, as independent properties.
- Compiled programs: self tail calls are loops on every path; stack exhaustion is a defined runtime error. `dlm run` prints the output produced before a runtime error.
- The architectural laws and the development program are in `docs/DLM_ARCHITECTURAL_LAWS.md` and `docs/DEVELOPMENT_PROGRAM.md`; their implementation state is in `docs/LAW_STATUS.md`.

## v0.88.0 — Record Schemas and Independent Equational Replay

```text
dlm build --emit-ir examples/valid/record_schemas.dlm -o records
dlm proof-export --trusted-only examples/valid/equational_replay.dlm Equations.identity > identity.kproof
dlm proof-check identity.kproof
```

- Checked record schemas and compilation-local nominal IDs reach the backend, including nested and inherited
  records. IR verification checks constructor fields, projections and nominal identity (`DLM-IR v3`).
- Stage C begins with structured term/proof DAGs for mathematical Nat equalities, replayed independently of
  symbolic search and source execution. Certificates with evidence use v2; other certificates explicitly retain
  `legacy-trusted` strength. Proof artifacts can be checked after deleting the source.
- Rules, budgets, external axiom contexts, API changes and remaining obligations are documented in
  `docs/RECORD_SCHEMAS.md`, `docs/EQUATIONAL_REPLAY.md` and `docs/LAW_STATUS.md`. Full logic/induction replay and
  machine-code certification remain open.

## v0.89.0 — Finite F2 mathematics

```text
dlm homology examples/math/circle.simp
dlm homology examples/math/torus.simp
dlm run --lib lib examples/valid/f2_arithmetic.dlm
```

Exact bounded matrix/chain-complex/category Rust APIs and scalar DLM library
`std.f2`. Homology output includes cycle representatives and explicitly reports
runtime-checked assurance. See [finite mathematics](docs/FINITE_MATHEMATICS.md)
for supported operations, limits and remaining theory/proof obligations.

## v0.90.0 — Source mathematics and independent F2 witnesses

Matrices, kernels, images and finite homology are now callable from ordinary
DLM using `std.matrix_f2` / `std.homology_f2`. Source operations validate shapes
and return Result errors; the existing interpreter and C backend execute them.
Result patterns (`ok` / `err`) and IR dominance checks preserve guarded payload
extraction. Independent host-side witnesses can be exported and checked without
Gaussian elimination or the original source:

```text
dlm run --trusted-only --lib lib examples/valid/matrix_homology_f2.dlm
dlm homology-export examples/math/torus.simp torus.f2proof
dlm homology-check torus.f2proof
```

These are bounded F2 computations and runtime witnesses, not new StaticProofs
or completion of HoTT/derived mathematics. See
[APIs, assurance and limits](docs/MATH_SOURCE_AND_WITNESSES.md).

## v0.91.0 — Finite algebra

Finite groups, rings, fields and left modules now have exhaustive law-checking
APIs in Rust and ordinary source libraries. Groups support normal quotients,
kernels/images and the checked map G/ker(f) -> im(f). Source `std.prime` adds
bounded exact Fp scalar arithmetic.

```text
dlm run --trusted-only --lib lib examples/valid/finite_algebra_demo.dlm
```

[Algebra APIs and assurance](docs/FINITE_ALGEBRA.md) ·
[Mathematics progress and remaining work](docs/MATHEMATICS_PROGRESS.md).
Source orders are <=8, host orders <=64; prime-field scalars support p<=251.
These are runtime-checked finite structures; general algebraic proofs, non-F2
matrix/homology coefficients and higher mathematical theories remain open.

### v0.92.0 — Bounded exact rationals and prime-field homology

Ordinary `std.rational`, `std.matrix_fp`, `std.homology_fp` libraries, exact host
APIs and oriented boundaries over primes <=251 are described in
[NUMERIC_FIELDS.md](docs/NUMERIC_FIELDS.md). For example:

```sh
dlm homology --prime 3 examples/math/projective_plane.simp
dlm run --lib lib examples/valid/rational_prime_demo.dlm
```

Numeric profiles, runtime assurance and remaining work are explicit; the old F2
commands/witness formats are preserved. See `docs/MATHEMATICS_PROGRESS.md`.

### v0.93.0 — Exact precision budgets and rational matrices

[EXACT_RATIONAL_MATRICES.md](docs/EXACT_RATIONAL_MATRICES.md) describes exact
limb arithmetic (configurable up to 16384 bits), Q matrix rank/kernel,
determinants, inverses and linear systems, plus ordinary bounded `std.matrix_q`.
Source Nat/Int stay u128/i128. Example:

```sh
dlm matrix-q --inverse --solve examples/math/rational_rhs.qmat examples/math/rational_system.qmat
dlm run --lib lib examples/valid/rational_matrix_demo.dlm
```

Deferred positive geometries and symbolic huge-number work are tracked in
[the future application plan](docs/FUTURE_MATH_APPLICATIONS.md).

### v0.94.0 — Exact arithmetic in DLM programs

Ordinary `std.exact` provides canonical integers/rationals with 1..1024-bit
source precision, explicit immutable work reservations, Result errors and the
existing native C path. Nat/Int and previous rational matrix profiles are
unchanged. See [SOURCE_EXACT_ARITHMETIC.md](docs/SOURCE_EXACT_ARITHMETIC.md).

```sh
dlm run --trusted-only --lib lib examples/valid/exact_arithmetic.dlm
dlm build --trusted-only --lib lib -o exact_demo examples/valid/exact_arithmetic.dlm
```

General source BigRational matrices, dense layouts and independent exact/Q
certificates remain open. The deferred applications stay in their future plan.

### v0.95.0 — BigRational matrices and independent Q witnesses

Ordinary `std.matrix_bigq` adds <=4-axis matrices over 1..1024-bit source
BigRational coefficients with explicit budget threading, RREF/rank/kernel,
determinant/inverse and multiple-RHS systems. See
[SOURCE_BIGRATIONAL_MATRICES.md](docs/SOURCE_BIGRATIONAL_MATRICES.md).

```sh
dlm run --trusted-only --lib lib examples/valid/bigrational_matrix_demo.dlm
dlm matrix-q-export examples/math/rational_rank.qmat result.qproof
dlm matrix-q-check result.qproof examples/math/rational_rank.qmat
```

[Q_MATRIX_WITNESSES.md](docs/Q_MATRIX_WITNESSES.md) defines separate standalone
rank/kernel witnesses and exact inverse/solution residual APIs. Checking uses
matrix identities, with no elimination search or new StaticProof authority.
Existing numeric profiles, compiler semantics and proof formats are preserved.

### v0.96.0 — Exact scalar powers, lcm and integer square roots

The active mathematical section remains numeric foundations. Host exact types
and ordinary `std.exact` now expose integer/rational powers, nonnegative lcm
and integer floor square roots with exact remainders. Domains, budgets and
independent Python/source/C fixtures are in
[EXACT_SCALAR_OPERATIONS.md](docs/EXACT_SCALAR_OPERATIONS.md).

```sh
dlm run --trusted-only --lib lib examples/valid/exact_scalar_operations.dlm
dlm build --trusted-only --lib lib -o scalar_demo examples/valid/exact_scalar_operations.dlm
```

Numeric criterion N05 is closed in the existing host/source precision profiles;
4/16 criteria are closed, 12 remain open. The 16384-bit source extension, dense
layouts and real/interval/complex families remain part of this same section.
The next mathematical section waits for its complete acceptance inventory.

### v0.97.0 — Source/native exact precision through 16384 bits

Ordinary `std.exact` now matches the host precision ceiling. Word/block
algorithms cover division, gcd, input/output and integer roots; the <=1024-bit
profile keeps its prior word/work limits. See
[SOURCE_EXACT_PRECISION.md](docs/SOURCE_EXACT_PRECISION.md) for domains,
intermediate bounds, resource semantics and computational evidence.

```sh
dlm run --trusted-only --lib lib examples/valid/wide_exact_arithmetic.dlm
dlm build --trusted-only --lib lib -o wide_demo examples/valid/wide_exact_arithmetic.dlm
```

N04 closes: 5/16 numeric criteria are closed, 11 remain open. Dense layouts,
real/interval/complex families, scalar witnesses and full platform acceptance
remain required. The next mathematical section waits for full numeric closure.

### v0.98.0 — Exact complex arithmetic over Q

Host `math::complex_q::ComplexQ` and ordinary `std.complex_q` provide rational
real/imaginary components, constructors, equality, negation, add/sub/mul/div,
reciprocal, conjugation and squared modulus. Every compound operation threads
the same budget. Precision applies to each rational intermediate; zero division
and malformed raw descriptions are rejected. See
[EXACT_COMPLEX_Q.md](docs/EXACT_COMPLEX_Q.md) for the bounded Q(i) contract.

```sh
dlm run --trusted-only --lib lib examples/valid/exact_complex_q.dlm
dlm build --trusted-only --lib lib -o complex_demo examples/valid/exact_complex_q.dlm
```

N06 closes: 6/16 numeric criteria are closed, 10 remain open (62.5% of checklist
items, not effort). The active section remains numeric foundations; acceptance
is recorded in [MATHEMATICS_PROGRESS.md](docs/MATHEMATICS_PROGRESS.md).
Computable complex numbers are a separate numeric criterion (N09). Intervals
(N07) come next; the following mathematical section continues to wait.

### v0.101.0 — C-min accepted; rational intervals

The bounded proof-kernel criterion C-min is accepted: each of its six
conditions has a dedicated test, kernel proof artifacts must be canonical text,
and `dlm proof-check` exits 1 for an invalid proof, 2 for unreadable input and
3 when the check runs out of budget (never a refutation). See
[EQUATIONAL_REPLAY.md](docs/EQUATIONAL_REPLAY.md).

Host `math::interval_q` and ordinary `std.interval_q` add closed rational
intervals: inclusion-correct and tightest arithmetic, hull/intersection,
comparison that stays undecided on overlap, explicit refusal of divisors that
contain zero, and square roots with guaranteed bounds within `2^-k`
([EXACT_INTERVALS.md](docs/EXACT_INTERVALS.md)). Numeric criteria N01–N07 are
closed (7/16).

```sh
dlm run --trusted-only --lib lib examples/valid/interval_q_demo.dlm
dlm proof-check examples/proofs/identity.kproof
```

### v0.102.0 — Owned byte buffers (D-min); computable reals

Stage D starts with `ByteBuf`, an owned mutable byte buffer. The checker tracks
borrows (`buf_len`, `buf_get`) and moves (`buf_set`, `buf_push`, `buf_to_list`,
`buf_free`, `ByteBuf` parameters, `let`, results) on every control path and
reports misuse as `E0943 OwnershipError`; owners still held at the end of their
scope, or consumed on only one branch, are released there. Native code changes
buffers in place and releases live buffers when a runtime error ends the
program (`DLM_OWNERSHIP_TRACE=1` prints the counts). See
[OWNERSHIP.md](docs/OWNERSHIP.md).

Host `math::creal` and ordinary `std.creal` add computable reals: programs of up
to 64 nodes (`c 2 ; sqrt 0`) whose `approx(n)` is a rational interval of width at
most `2^-n` guaranteed to contain the number; domain errors are reported only
when the bounds prove them, and comparisons answer "undecided" instead of
guessing ([COMPUTABLE_REALS.md](docs/COMPUTABLE_REALS.md)). Numeric criteria
N01–N08 are closed (8/16).

```sh
dlm run --trusted-only examples/valid/byte_buffer_demo.dlm
dlm run --trusted-only --lib lib examples/valid/creal_demo.dlm
```

### v0.103.0 — Borrowed buffers; computable complex numbers

`fn f(b: &ByteBuf)` borrows a buffer for a call; `buf_copy`, `buf_slice`, `buf_append`, `buf_eq`,
`buf_from_text`, `buf_to_text` ([OWNERSHIP.md](docs/OWNERSHIP.md)). `std.ccomplex`: complex numbers with
computable real parts ([COMPUTABLE_COMPLEX.md](docs/COMPUTABLE_COMPLEX.md)); N01–N09 closed.

### v0.131.0 — Hardware-accelerated (GPU) presentation

`win_gpu`, `win_present_scene`, `win_read`: vector scenes drawn by OpenGL (GLX/WGL loaded at run time, MSAA, stencil-then-cover fills, gradients, clips, images), software fallback ([GUI.md](docs/GUI.md)).

### v0.129.0 — TrueType text of every script, lists and scrolling

`file_font_load`/`file_font_find`, `vx_text_fonts` (font fallback, fit width), simplified bidi and Arabic joining, `\u{..}` escapes; `Scroll`/`ListView`/`Table`, `Select`/`Wheel` events; XIM/Unicode window input, wheel ([GUI.md](docs/GUI.md)).

### v0.128.0 — Vector UI and native windows

`vx_*` vector scenes (anti-aliased, HiDPI, SVG, incremental), Cyrillic font, `std.vector` (vector widgets, states, `check_ui`), `win_*` + `run_window` (X11, Win32) ([GUI.md](docs/GUI.md)).

### v0.127.0 — Own GUI, stage G1

Raster images `gx_*`, PNG, `std.ui` widgets/layout/rendering/events, browser backend; recursion into record lists via `map`/`fold` ([GUI.md](docs/GUI.md)).

### v0.126.0 — Recursive data, processes, TCP

Recursive records, recursive and generic `enum` with structural measures; `proc_run/spawn/poll/wait/kill/env`; `net_*` TCP; `std.process`, `std.net` ([OS.md](docs/OS.md)).

### v0.125.0 — Loops, enums, file system

`fn f(...) -> T loop` (tail loops in constant stack), `enum` with exhaustive matches, `dlm run --interactive`; `file_seek/tell/size/truncate/set_buffered`, directories, rename/copy/atomic write; `std.files` ([APPS.md](docs/APPS.md), [FILES.md](docs/FILES.md)).

### v0.124.0 — BigNat primes, BPSW, BVH

`bn_powmod`, `bn_modinv`, `bn_is_prime`, `bn_next_prime`, `bn_random`, hex; 256-bit `nt_mod_mul`, BPSW `nt_is_prime`; `bvh_raycast`, `bvh_winding`, `bvh_pairs`; `std.bigprime` ([BIGNUM.md](docs/BIGNUM.md)).

### v0.123.0 — Sparse solvers, robust geometry, signals, statistics

Krylov solvers, exact predicates, Delaunay, k-d trees, STFT/wavelets/IIR, SHA-256; `std.signal`, `std.statistics`, `std.spatial` ([KERNELS.md](docs/KERNELS.md)).

### v0.122.0 — Ordered maps

`NatMap`, `TextMap`, `km_*`, `tm_*`, `std.maps` ([MAPS.md](docs/MAPS.md)).

### v0.121.0 — SAT certificates, model counting, integer programming

`sat_proof`, `sat_check`, `sat_count`, `ilp_*` ([PROOFS_ILP.md](docs/PROOFS_ILP.md)).

### v0.120.0 — Gröbner bases, FinSet

`mv_*`, `gb_*`, `std.ideals`, `std.categories` ([GROEBNER.md](docs/GROEBNER.md)).

### v0.119.0 — SAT, integral homology, persistence

`sat_*`, `tp_*`, `std.logic`, `std.topology` ([LOGIC_TOPOLOGY.md](docs/LOGIC_TOPOLOGY.md)).

### v0.118.0 — Number theory, finite fields, permutation groups

`nt_*`, `pf_*`, `pm_*`, `std.number_theory` ([ALGEBRA.md](docs/ALGEBRA.md)).

### v0.117.0 — Analysis and optimization

FFT, LP simplex, `std.analysis`, `std.optimize` ([ANALYSIS.md](docs/ANALYSIS.md)).

### v0.116.0 — Numerical linear algebra

`la_*` (LU, Cholesky, QR, eigen, SVD, CG), `std.linalg` ([LINALG.md](docs/LINALG.md)).

### v0.115.0 — Graphs, geometry, probability

`NatVec`, native `gr_*`, `std.graphs`, `std.geometry`, `std.probability` ([GRAPHS.md](docs/GRAPHS.md)).

### v0.114.0 — Machine floats

`Float`, `FloatVec`, float literals, `std.numerics` ([FLOATS.md](docs/FLOATS.md)).

### v0.113.0 — Superclasses and default methods

`class C<T>: Super { ... fn m(...) -> R = default }` ([GENERICS.md](docs/GENERICS.md)).

### v0.112.0 — Type classes

`class`/`instance` with generic instances and class bounds, `std.classes` ([GENERICS.md](docs/GENERICS.md)).

### v0.111.0 — Type-argument inference and bounds

Omitted type arguments inferred locally; bounds `Eq`/`Ord`; `equal`, `less` ([GENERICS.md](docs/GENERICS.md)).

### v0.110.0 — Generics

Generic functions and records with explicit type arguments, `std.collections` ([GENERICS.md](docs/GENERICS.md)).

### v0.109.0 — File handles

Linear `File` resource with sticky errors and explicit close ([FILES.md](docs/FILES.md)).

### v0.108.0 — Numeric acceptance

N16: evidence map and cross-family acceptance ([NUMERIC_ACCEPTANCE.md](docs/NUMERIC_ACCEPTANCE.md)). Numeric section N01–N16 closed.

### v0.107.0 — Dense numbers

`BigNat` dense naturals, native kernel under `std.exact`, `std.dense`, cost profile ([DENSE_NUMBERS.md](docs/DENSE_NUMBERS.md)).
N01–N15 closed.

### v0.106.0 — Numeric contracts; scalar witnesses

Error categories and budget contracts ([NUMERIC_CONTRACTS.md](docs/NUMERIC_CONTRACTS.md), `std.numeric`); checkable
scalar witnesses ([SCALAR_WITNESSES.md](docs/SCALAR_WITNESSES.md), `std.scalar_witness`). N01–N13, N15 closed.

### v0.105.0 — Numeric conversions; portable format

Explicit rounding conversions and `dlm1:` portable numeric texts ([NUMERIC_CONVERSIONS.md](docs/NUMERIC_CONVERSIONS.md),
`std.convert`). N01–N12 closed.

### v0.104.0 — Elementary functions; TextBuf

Computable-real nodes `exp`, `log`, `sin`, `cos`, `pi` ([ELEMENTARY_FUNCTIONS.md](docs/ELEMENTARY_FUNCTIONS.md));
owned text builder `TextBuf` (`tbuf_new/push/len/finish`). N01–N10 closed.
