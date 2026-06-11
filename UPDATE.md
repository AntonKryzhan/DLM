# UPDATE.md

## v0.36.0 — Property-Based Invariant Tests

Дата: 2026-06-11

### Цель патча

`v0.36.0` добавляет первый property-style слой проверки инвариантов DLM без изменения публичного синтаксиса `.dlm` и без изменения runtime/checker semantics.

После `v0.35.0` checker уже имеет pass pipeline. Теперь фиксируются свойства, которые должны пережить будущий split на `typeck`, `proofck`, `passport_infer`, `bridgeck` и `audit`.

### Добавлено

```text
crates/dlm_core/tests/property_invariants.rs
docs/PROPERTY_INVARIANTS.md
```

### Проверяемые свойства

```text
Trust join is idempotent / commutative / associative / monotone.
CheckPolicy is prefix-closed over the trust lattice.
BridgeProfile matches the central bridge_law for every bridge kind.
Truth-preserving bridges must also preserve proof evidence.
Axiom-requiring bridges must be Axiom-or-worse tainted.
quote remains syntax-only.
transport / migration / materialize remain value-only and do not preserve proof/truth by default.
soundness remains Axiom-tainted.
unsafe and unknown bridges remain Unsafe-tainted.
Passport derivations do not lower trust.
HistoryChain remains ordered and multiplicity-preserving.
```

### Команда проверки

```powershell
cargo test -p dlm_core --test property_invariants
```

### Архитектурный смысл

`v0.36.0` начинает переводить главный смысл проекта из набора ручных examples в набор явно проверяемых мета-инвариантов. Это нужно до дальнейшего расширения IR pipeline, потому что будущие refactor-патчи должны доказывать, что trust/passport/bridge laws не изменились случайно.

# UPDATE.md

## v0.35.0 — Checker Orchestration / First Pass Split

Дата: 2026-06-11

### Цель патча

`v0.35.0` начинает отделять checker-orchestration от монолитного `checker.rs` без изменения публичного синтаксиса `.dlm` и без переписывания текущей семантики.

После `v0.34.0` в проекте уже есть ID/resolver skeleton. Теперь этот frontend-pass подключён к checker pipeline:

```text
RawAST accepted
  -> name_resolution
  -> legacy_checker
```

### Добавлено

```text
crates/dlm_core/src/passes.rs
PassId
PassStatus
PassReport
PassPipelineReport
FrontendPassOutput
run_frontend_passes(...)
crates/dlm_core/tests/checker_passes.rs
docs/CHECKER_ORCHESTRATION.md
```

### Изменено

`CheckReport` теперь содержит:

```rust
pub passes: PassPipelineReport
```

Это позволяет тестам, CLI, будущему `audit` и будущим IR passes видеть не только итоговые diagnostics, но и то, какие stages прошли, упали или были пропущены.

### Поведение при ошибках

Если `name_resolution` падает, `legacy_checker` помечается как:

```text
Skipped
```

и checker не запускается поверх некорректного symbol graph.

Если `name_resolution` проходит, но semantic checker находит ошибки, `legacy_checker` помечается как:

```text
Failed
```

а старые diagnostics остаются в `CheckReport::diagnostics`.

### Инвариант

```text
failed frontend pass must block dependent semantic passes
```

То есть поздний pass не должен пытаться чинить или игнорировать ошибку раннего pass.

### Regression tests

Добавлен файл:

```text
crates/dlm_core/tests/checker_passes.rs
```

Он проверяет:

```text
frontend pipeline reports raw_ast_accepted and name_resolution;
checker report includes legacy_checker after frontend;
checker stops before legacy_checker when name_resolution fails.
```

### Архитектурный смысл

`v0.35.0` делает первый практический шаг к nanopass-style архитектуре:

```text
RawAST
  -> HIR
  -> ResolvedHIR
  -> TypedIR
  -> ProofIR
  -> PassportIR
  -> CheckedModule
```

Пока старый checker остаётся внутри `legacy_checker`, но теперь он явно оформлен как один stage, который позже можно раскалывать на `typeck`, `proofck`, `passport_infer`, `bridgeck` и `audit`.

## v0.37.0 — Meta-Level Stratification foundation

- Added `meta_level.rs` with `MetaLevelIndex`, `MetaStage`, `MetaAccess`, `MetaLevelContext` and strict observer-level validation.
- Added `MetaLevelError[E0908]` for object/meta-level escape attempts.
- Added `meta_level_passport(...)`, `object_level_passport(...)` and `meta_quote_passport(...)`.
- `meta_quote_passport(...)` produces `Term<T>` only; it does not create `TruthClaim`, `Provable` or `StaticProof`.
- Meta-quote preserves existing trust taint instead of cleaning it.
- Added regression tests in `crates/dlm_core/tests/meta_levels.rs`.

## v0.38.0 — Statement / Theorem foundation

- Added `statement.rs` with `StatementDecl`, `TheoremDecl`, `GoalDecl`, `HypothesisDecl` and declaration-kind helpers.
- Added `TypeKind::Statement`, `TypeKind::Theorem`, `TypeKind::Goal` and `TypeKind::Hypothesis`.
- Added `StatementTheoremError[E0909]` for invalid theorem-layer construction attempts.
- Added passport constructors for statements, goals, hypotheses, checked theorems and axiom-tainted theorems.
- `theorem_from_static_proof(...)` accepts only `StaticProof` evidence; it rejects raw `ProofTerm` and `RuntimeWitness` values.
- `axiom_theorem(...)` makes theorem assumptions visible as `trust=Axiom` and records `theorem:axiom:*` history.
- Added regression tests in `crates/dlm_core/tests/statements_theorems.rs`.

## v0.39.0 — Proof Context Foundation

Added the first internal proof-context layer:

- `ProofContext`;
- `HypothesisSet`;
- `HypothesisId`;
- `TacticStep`;
- `ProofObligation`;
- `ProofClosure`;
- `open_proof_context(...)`;
- `assume_hypothesis(...)`;
- `close_proof_with_static_proof(...)`;
- `close_proof_by_axiom(...)`.

No `.dlm` syntax was changed.

Main invariant:

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```

All three propositions must match exactly.


## v0.40.0 — Tactic Script Foundation

Дата: 2026-06-11

### Цель патча

`v0.40.0` добавляет первый внутренний слой tactic-script поверх `ProofContext`.

Публичный `.dlm` синтаксис не меняется. Это foundation-слой для будущего proof/tactic checker split.

### Добавлено

```text
crates/dlm_core/src/tactic.rs
crates/dlm_core/tests/tactic_script.rs
docs/TACTIC_SCRIPT.md
```

### Поддерживаемые команды

```text
Assume<P>
ExactStaticProof<TheoremName, Statement<P>, StaticProof<P>>
AdmitAxiom<TheoremName, Statement<P>, Reason>
```

### Главный инвариант

```text
closing tactic must be final
```

Скрипт может оставлять goal открытым с obligation, либо закрыть его через `StaticProof`, либо явно закрыть через axiom admission. После закрывающей команды дальнейшие tactic-команды запрещены.

### Команда проверки

```powershell
cargo test -p dlm_core --test tactic_script
```

## v0.41.0 — Proof Certificate Foundation

Дата: 2026-06-11

### Цель патча

`v0.41.0` добавляет внутренний слой proof certificates поверх `ProofClosure` и `TacticScriptReport`.

Публичный `.dlm` синтаксис не меняется. Это foundation-слой для будущего ProofIR / certificate serialization / audit report.

### Добавлено

```text
crates/dlm_core/src/certificate.rs
crates/dlm_core/tests/proof_certificate.rs
docs/PROOF_CERTIFICATE.md
```

### Главный инвариант

```text
closed ProofClosure<Theorem<name:P>> => ProofCertificate<name:P>
```

Открытые proof obligations не могут производить certificate.

### Проверка certificate

```text
certificate.theory == theorem.theory
certificate.theorem_name == theorem.name
certificate.proposition == theorem.proposition
certificate.trust == theorem.trust
certificate.provenance == theorem.provenance
certificate.fingerprint == fingerprint(certificate contents)
```

### Команда проверки

```powershell
cargo test -p dlm_core --test proof_certificate
```


## v0.42.0 — Proof Certificate Audit / Export Foundation

Added deterministic export and audit reports for proof certificates.

- New module: `certificate_audit.rs`.
- New tests: `certificate_audit.rs`.
- New docs: `docs/PROOF_CERTIFICATE_AUDIT.md`.
- New diagnostic kind: `ProofCertificateAuditError[E0913]`.

No `.dlm` syntax or runtime behavior changes.


## v0.43.0 — Equality Proof / Rewrite Foundation

Added a typed equality/rewrite foundation.

Changed/added files:

- `crates/dlm_core/src/equality.rs`
- `crates/dlm_core/tests/equality_rewrite.rs`
- `docs/EQUALITY_REWRITE.md`

New core artifacts:

- `EqProof { lhs, rhs }`
- `RewriteRule { name, lhs, rhs }`
- `RewriteStep`
- `RewriteTrace`
- `RewriteCertificate { from, to }`

New diagnostic kind:

- `EqualityRewriteError[E0914]`

Primary invariants:

- `Bool` is not rewrite evidence.
- `RuntimeWitness` is not static equality evidence.
- raw `ProofTerm` must be kernel-checked first.
- `EqProof` must be converted into a `RewriteRule` before application.
- rewrite certificates preserve ordered rewrite traces and trust taint.


## v0.44.0 — Rewrite Normalization / Audit Foundation

Added bounded rewrite normalization on top of v0.43 equality rewrite certificates.

Changed/added files:

- `crates/dlm_core/src/rewrite_normalization.rs`
- `crates/dlm_core/tests/rewrite_normalization.rs`
- `docs/REWRITE_NORMALIZATION.md`

New diagnostic kind:

- `RewriteNormalizationError[E0915]`

Protected laws:

- normalization is ordered and bounded;
- cyclic rewrite systems fail by `max_steps`;
- only `RewriteRule` passports can participate;
- report/certificate endpoints must match;
- axiom taint is preserved through normalization.

No `.dlm` syntax, checker behavior or runtime behavior changed.


## v0.45.0 — Nat Induction MVP

Added the first core-level Nat induction proof foundation.

Changed/added files:

- `crates/dlm_core/src/induction.rs`
- `crates/dlm_core/tests/nat_induction.rs`
- `docs/NAT_INDUCTION.md`

New diagnostic kind:

- `InductionError[E0916]`

Protected laws:

- Nat induction requires an explicit `InductionScheme<Nat,P>`.
- Base and step cases require exact `StaticProof` evidence.
- `RuntimeWitness` and raw `ProofTerm` cannot close induction cases.
- `InductionProof` is not silently a theorem; theorem construction requires a matching `Statement`.
- Axiom taint from base or step cases is preserved.

No `.dlm` syntax, checker behavior or runtime behavior changed.


## v0.46.0 — Module / Import System Foundation

- Added `module_system.rs`.
- Added module manifests, import graph validation, public/private export policy, and export passports.
- Added `ModuleImportError[E0917]`.
- Added tests in `module_imports.rs`.
- Added `docs/MODULE_IMPORTS.md`.


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

- Added `axiom_registry.rs`.
- Added `AxiomDecl`, `AxiomRegistry`, `DependencyEntry`, `MetatheoryDependencyAuditReport`.
- Added `AxiomRegistry` and `MetatheoryDependencyAudit` passport kinds.
- Added `MetatheoryDependencyError[E0919]`.
- Added tests in `metatheory_dependencies.rs`.
- Added `docs/METATHEORY_DEPENDENCIES.md`.

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

- Added `crates/dlm_core/src/conservative_extension.rs`.
- Added `crates/dlm_core/tests/conservative_extension.rs`.
- Added `docs/CONSERVATIVE_EXTENSION.md`.
- Added `TypeKind::ConservativeExtensionAudit`.
- Added `DiagnosticKind::ConservativeExtensionError` rendered as `E0921 ConservativeExtensionError`.
- Added preserved-theorem witnesses and conservative-extension audit reports.
- Added stable text export for conservative-extension reports.

This keeps development inside track 1: metamathematical foundation.

## v0.51.0 — Theorem Dependency Graph / Global Metatheory Inventory

This release adds a global inventory layer for theorem foundations. It can assemble theorem nodes, dependency audits, metatheory closure reports and conservative-extension evidence into a single ordered, fingerprinted inventory report.

The release remains inside the first strategic development phase: metamathematical foundation.

## v0.52.0

Added Soundness Boundary Ledger / Bridge Assumption Inventory foundation.

## Documentation update — High-Performance Compilation Track

Added a strategic roadmap document for future native high-performance compilation:

- `docs/HIGH_PERFORMANCE_COMPILATION.md`

This does not alter the current implementation order. It records the future path for `DLM-Fast`, proof/passport erasure, optimization contracts, LLVM/MLIR/Cranelift backends, SIMD/PGO/LTO modes, and benchmark gates against C++/Rust/C/Zig/Fortran.


## v0.53.0

Added the Trusted Base Closure foundation gate. This closes the current metatheory-foundation audit chain by combining registry, dependency, closure, inventory, and soundness-boundary evidence into one final report.

## v0.54.0

Added Metatheory Foundation Exit / Completion Checklist. This is the formal gate for closing phase 1 before starting ordinary mathematics of the language.


## Documentation update — Stage Readiness Control Model

Added a formal project-control model for stage maturity:

- local readiness;
- architectural readiness;
- fundamental readiness.

This model is now part of the roadmap and should be updated after major patches. It makes explicit that moving forward means passing an MVP gate, not reaching absolute 100% finality.

New document:

- `docs/STAGE_READINESS_MODEL.md`


## v0.55.0 — Logical Connectives / Quantifier Foundation

- Added `logic.rs`.
- Added `LogicalFormula` and `QuantifiedFormula` passport types.
- Added `LogicalConnective::{And, Or, Implies, Iff, Not}`.
- Added `QuantifierKind::{Forall, Exists}` and explicit `BoundVariable`.
- Added formula/quantifier passports and stable exports.
- Added `LogicFormulaError[E0926]`.
- Added regression tests for arity, binder validation, taint preservation and proof/truth/runtime separation.

Readiness delta:

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         5–10%  -> 12–18%
Architectural readiness: 30–40% -> 35–45%
Fundamental readiness:   20–30% -> 22–32%
```


## v0.56.0 — Substitution / Alpha-Equivalence Foundation

- Added `substitution.rs`.
- Added `VariableScopeReport`, `AlphaEquivalenceReport`, `SubstitutionReport`.
- Added `SubstitutionError[E0927]`.
- Added `TypeKind::VariableScopeReport`, `TypeKind::AlphaEquivalenceReport`, `TypeKind::SubstitutionReport`.
- Added tests for free/bound variables, alpha-equivalence, capture-avoiding substitution, binder shadowing, taint preservation and stable exports.

Readiness delta:

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         12–18% -> 18–24%
Architectural readiness: 35–45% -> 40–50%
Fundamental readiness:   22–32% -> 25–35%
```

<!-- RUNTIME_HARDWARE_LAYERING_PRINCIPLE_BLOCK -->
## Docs update — Runtime / Hardware Layering Principle

Added `docs/RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md`, which fixes a global rule for future runtime, GPU and compiler work: rich semantic meaning must stay high-level, while hardware execution must stay dense and compact.

<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->
## Docs update — DLM Architectural Laws

Added `docs/DLM_ARCHITECTURAL_LAWS.md`, a detailed architectural law document that defines 25 global laws for DLM development.

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
