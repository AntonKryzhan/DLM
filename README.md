# DLM — Deductive Logic Machine

**English** | [Русский](README.ru.md)

**DLM is an implemented, research-stage programming language and formal-computation
system.** Its Rust workspace contains a parser, semantic and contract checking,
an interpreter, typed intermediate representations, a native C backend for the
supported executable subset, proof-related infrastructure, and substantial
standard libraries. The goal is a **general-purpose language** in which formal
reasoning is available when needed—not a requirement to prove every program.

The audited source snapshot is **v0.145.0** (`Cargo.toml`). The word *research*
describes open assurance and compatibility work; it does not mean that this
repository is a minimal demonstration or merely a language proposal.

### What is implemented in the source tree

- **Compiler and language:** source parsing, modules, type and effect checking,
  generics and type classes, ownership/resource checks, checked lowering,
  interpretation and C generation; a separate `native { ... }` application
  domain supports machine data, FFI, atomics, SIMD and several artifact types
  within documented limits. See [compiler](docs/COMPILER.md) and
  [native applications](docs/NATIVE_APPLICATIONS.md).
- **Logic and audit:** theory boundaries, proof passports, provenance and
  trust tracking, a proof kernel and bounded independent certificate/replay
  checks. These do **not** constitute a proof of the entire compiler, runtime
  or arbitrary user program. See [law status](docs/LAW_STATUS.md).
- **Computational libraries:** exact and bounded arithmetic, rational and
  complex numbers, matrices, graphs, algebra, topology, numerical methods,
  files, processes, vector UI and graphics. Implemented domains have their
  own limits and validation conditions; see
  [mathematical progress](docs/MATHEMATICS_PROGRESS.md).
- **Networking (DLN):** typed socket and codec layers, asynchronous operations,
  cryptographic primitives, QUIC/transport adapters, NetworkSpec and later
  identity, discovery and adaptive-network modules. Native dependencies,
  platform validation and protocol-proof obligations remain explicit; see
  [DLN status](docs/DLN_STATUS.md) and the respective `docs/DLN_*.md` files.

**Source inventory for this v0.145.0 snapshot:** 4 Rust workspace crates
(`dlm_core`, `dlm_cli`, `dlm_network_proof`, `dlm_transport_proof`), 128 Rust
source files under `dlm_core/src`, 74 `lib/std/*.dlm` modules, 298 `.dlm`
examples and 164 Rust integration-test files across the workspace. These are
counts of implementation and test assets, **not** proof of correctness or
production adoption.

### Maturity: four separate questions

| Dimension | Evidence-based assessment |
| --- | --- |
| **Implementation depth** | Substantial working compiler, runtime, logic, mathematics, native and network code; far beyond a toy prototype. |
| **API and semantic stability** | Supported semantics and compatibility boundaries are tested and documented, but the pre-1.0 APIs and some language features can still change. |
| **Users and ecosystem** | A sizeable in-repository standard library, examples and tests exist; independent third-party usage, tooling coverage and ecosystem breadth are not established by source size. |
| **Production readiness** | Real executable capabilities exist, but full cross-platform, security, performance and v1.0 acceptance gates are not yet demonstrated. Use documented supported subsets and validate for each deployment. |

The source tree is the evidence for what exists; passing tests are evidence for
their tested scenarios, not universal guarantees. Some roadmap/status documents
contain historical version snapshots. For current release numbering use
`Cargo.toml`; for open contracts and acceptance work consult
[ROADMAP.md](ROADMAP.md), [docs/LAW_STATUS.md](docs/LAW_STATUS.md) and
[docs/MATHEMATICS_PROGRESS.md](docs/MATHEMATICS_PROGRESS.md).

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

**DLM** is a programming-language implementation and formal-reasoning system
built around one central idea: programs should execute, while selected values,
claims and transformations can also carry explicit proof status, trust level,
provenance, validation history and logical boundaries.

DLM already has a checker, interpreter, native compilation path and application
libraries. Its research focus concerns how those pieces interact with proof
objects and mathematical meaning; it is not a substitute for ordinary language
capabilities or an assertion that all computations are formally proved.

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

DLM has a substantial implemented compiler and library surface, but it does
not claim the proof-system coverage, ecosystem or production assurance of these
established tools. Its proof-related guarantees are scoped to documented kernels
and contracts.

---

## Advantages

DLM implements or actively develops:

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

The substantial implementation should not be confused with a stable v1.0
platform. Current limitations include:

* evolving pre-1.0 syntax, APIs, some semantics and compatibility contracts;
* less mature IDE, package, integration and distribution tooling than established languages;
* incomplete independent adoption and production-operations evidence;
* platform-specific native and networking dependencies and unfinished acceptance matrices;
* bounded proof-checking and numerical guarantees, not a whole-program or
  whole-compiler correctness theorem;
* remaining work on general task scheduling, full language/backend coverage
  and the v1.0 release gates.

The standard library is **substantial but uneven**: many specialized modules
are implemented, while some common general-purpose facilities and portability
guarantees remain open. DLM can be used and tested within its documented
capability boundaries; production suitability must be established per use case.

---

## What DLM can be useful for

Implemented and research-stage areas of use:

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
* checked native utilities and file-processing applications within the supported subset;
* network/transport implementation and protocol research with explicit assurance boundaries.

---

## Repository structure

```text
crates/dlm_core/              Parser, checker, interpreter, proof and native backends
crates/dlm_cli/               The `dlm` command-line interface
crates/dlm_network_proof/     Independent bounded network-certificate checks
crates/dlm_transport_proof/   Independent bounded transport-certificate checks
lib/std/                     DLM standard-library modules
examples/{valid,invalid,math,proofs,native}/  Source examples and fixtures
crates/*/tests/              Rust integration and regression tests
docs/                        Design, contracts and acceptance evidence
Cargo.toml                   Rust workspace, version and license declaration
```

---

## Running checks

Examples using the checked-in workspace:

```bash
cargo run -p dlm_cli -- check examples/valid/provability_truth_boundary.dlm
cargo run -p dlm_cli -- run examples/valid/simple_nat.dlm
cargo test --workspace --locked
```

Native executable builds use `dlm build` with a compatible 64-bit GCC/Clang
toolchain and only cover supported lowering paths; consult
[docs/COMPILER.md](docs/COMPILER.md) for platform setup and failure modes.
The test command runs the suite—it is **not** a claim that every target or CI
environment has already passed.

---

## Development status

DLM v0.145.0 is an active, sizeable research-stage implementation, not an
unimplemented proposal. The compiler, interpreter, native subsystem, logic
infrastructure, mathematical libraries and DLN modules all contain executable
code and dedicated tests. Their validation scopes differ, and work remains on
API stability, platform parity, language completeness, protocol assurance,
ecosystem tooling and v1.0 acceptance. Release-specific guarantees must be
checked against the actual code and recorded test evidence.

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

Historical interpretation of this stage model:

```text
Metamathematical foundation: strong MVP gate, not absolute final theory.
Ordinary mathematics: next active construction layer.
Future proof kernel, stdlib, runtime and compiler tracks will feed pressure back into the foundation.
```

See `docs/STAGE_READINESS_MODEL.md`.

## License

confirm the repository's
distribution terms before reuse.

