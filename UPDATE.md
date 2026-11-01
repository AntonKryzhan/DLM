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


## v0.60.0 — Structural Elimination / Pattern Boundary

Adds explicit product elimination, sum case elimination and record pattern reports. Structural elimination is value-level and does not become proof, theorem or truth. Taint is preserved; proof/truth/theorem/runtime smuggling is rejected. See `docs/STRUCTURAL_ELIMINATION.md`.


## v0.61.0 — Option / Result / Partiality Type Foundation

Adds explicit `Option<T>`, `Result<T,E>` and `PartialityReport` objects for ordinary mathematical partiality. Partial functions now have a typed target representation instead of hidden null/exception semantics. Readiness delta: Local 42–50% -> 48–56%, Architectural 60–72% -> 64–76%, Fundamental 42–54% -> 46–58%.

## v0.62.0

Added finite List/Sequence foundation with explicit item typing, explicit finite length, in-bounds/out-of-bounds index reports, stable exports and taint preservation.

## v0.63.0

- Added `traversal` module.
- Added `MapTraversalReport`, `FoldTraversalReport`, `TraversalStatus`.
- Added `TypeKind::MapTraversal`, `TypeKind::FoldTraversal`, `TypeKind::TraversalReport`.
- Added `DiagnosticKind::TraversalError` / `E0934`.
- Added tests for bounded map/fold traversal, fuel rejection, contract mismatch, taint preservation and stable exports.

## v0.64.0

- Added recursion schemes and recursive-call audit reports.
- Added well-founded/fuel recursion statuses.
- Added `E0935 RecursionBoundaryError`.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.

## v0.66.0 — Standard Algebraic Prelude Foundation

Adds checked standard prelude contracts for Nat/Bool/Option/Result/List/Sequence operations. Collection map/fold contracts require explicit verified unified termination budgets. Prelude contracts preserve Axiom/Oracle/Unsafe taint and remain audit objects, not proof/theorem/truth claims.


## v0.67.0

Added `PreludeEvaluationReport` and small-step evaluation for verified standard prelude operations. Evaluation is deterministic and fuel-bounded; map/fold use symbolic application and do not run arbitrary user code.

## v0.71.0 — Value Representation / Dense Runtime Descriptor

Adds `DenseRuntimeDescriptor` and `DenseRuntimeReport`, the first dense runtime representation layer after backend layout validation. The layer records scalar, tagged, dense-vector, slice-view, GPU-region, remote-region and audit-only runtime forms without turning descriptors into proofs, theorems, truth claims or runtime witnesses.

Main law:

```text
Passport/audit/proof metadata stays above hot runtime payloads.
Dense runtime data is region/value/block-oriented, not passport-per-element.
```

## v0.72.0 — Example Corpus Integrity / GPU Checkpoint Restore Completion

Дата: 2026-10-26

### Найдено

```text
examples/valid/gpu_checkpoint_restore_memory.dlm — check падал: checkpoint_gpu_memory/restore_gpu_memory не реализованы
examples/valid/infinity_modes.dlm — check падал: мост Core_quote ссылался на отсутствующую теорию Meta
3 negative-примера GPU checkpoint падали по неверной причине (unknown builtin), правила v0.21 не проверялись
dlm --version печатал 0.33.0
docs/MANIFEST.json — 4 записи с префиксом docs/, 3 документа отсутствовали
```

### Изменено

```text
crates/dlm_core/src/passport.rs   TypeKind::GpuMemoryCheckpoint, Capability::CanRestoreGpuMemoryCheckpoint,
                                  Passport::gpu_memory_checkpoint, Passport::restored_gpu_memory_region
crates/dlm_core/src/checker.rs    checkpoint_gpu_memory / restore_gpu_memory (+ алиасы)
crates/dlm_core/src/runtime.rs    RuntimeValue::GpuMemoryCheckpoint и исполнение тех же операций
crates/dlm_core/src/bridge_assumption.rs  объединены идентичные ветви UnsafeBridge (поведение не изменено)
crates/dlm_cli/src/main.rs        --version из CARGO_PKG_VERSION
examples/valid/infinity_modes.dlm добавлена теория Meta для моста Core_quote
docs/MANIFEST.json                исправлены пути, добавлены недостающие документы
```

### Добавлено

```text
crates/dlm_core/tests/gpu_checkpoint_restore.rs
crates/dlm_core/tests/example_corpus.rs
crates/dlm_cli/tests/cli_version.rs
docs/EXAMPLE_CORPUS_INTEGRITY.md
```

### Команды проверки

```powershell
cargo test --workspace
cargo test -p dlm_core --test example_corpus
cargo test -p dlm_core --test gpu_checkpoint_restore
```

## v0.73.0 — Ordinary Mathematics Surface Wiring

Дата: 2026-10-26

### Цель

Слои Stage 2 (v0.55–v0.67) существовали только как Rust-API. Теперь они доступны из `.dlm`. Раньше парсер молча отбрасывал строки `fn`.

### Добавлено

```text
crates/dlm_core/src/ordinary.rs                    surface-типы: разбор и канонизация
crates/dlm_core/src/checker/ordinary_surface.rs    fn, контракты, Option/Result, List/Sequence, Product, map/fold
crates/dlm_core/src/runtime/ordinary_surface.rs    исполнение тех же операций
crates/dlm_core/tests/ordinary_surface.rs          13 тестов
examples/valid/ordinary_{functions,partiality,collections,products}.dlm
examples/invalid/ (11 примеров на отдельные правила, включая *_rejected_by_trusted_only)
docs/ORDINARY_SURFACE.md
```

### Изменено

```text
passport.rs     TypeKind::TypedValue, ordinary_type_name()
partiality/sequence/structural/structural_elimination/traversal/function
                whitelists используют канонический тип вместо дескриптора значения
traversal.rs    map_collection_passport / fold_collection_passport
ast.rs          TheoryItem::Fn, FnDecl, FnParam
parser.rs       разбор fn, литералы true/false
resolve.rs      имена fn в общем пространстве имён теории
checker.rs      подключение ordinary_surface, локальные фреймы параметров
runtime.rs      новые RuntimeValue, функции, локальные фреймы
```

### Ломающее изменение (Rust API)

Тип элемента коллекции Option/Result теперь канонический: `Option<Nat>`, а не `OptionValue<some:Nat>`. Тест `sequence_types` обновлён: он закреплял старое поведение. Добавлен регрессионный тест на смешивание `some`/`none` и списков разной длины.

### Команды проверки

```powershell
cargo test --workspace
cargo test -p dlm_core --test ordinary_surface
cargo run -p dlm_cli -- run examples/valid/ordinary_collections.dlm
```

## v0.74.0 — Well-Founded Recursion, Records and Sums

Дата: 2026-10-26

### Добавлено

```text
fn ... decreasing <param> [fuel N] = ...   рекурсия с мерой (nat_decreasing / structural_subterm)
record Name(field: Type, ...)              записи; Name(...) — конструктор, field(r, f) — проекция
Sum<A, B>: inl, inr, is_left, case         суммы
tail, is_empty, cons                       операции над коллекциями для структурной рекурсии
crates/dlm_core/tests/ordinary_recursion_records.rs   11 тестов
examples/valid/ordinary_recursion.dlm, ordinary_records_sums.dlm
examples/invalid/ (7 примеров, включая runtime-исчерпание топлива)
```

### Изменено

```text
ast.rs        FnDecl.measure (FnMeasure), TheoryItem::Record (RecordDecl)
parser.rs     decreasing/fuel, record
ordinary.rs   Sum, Record в surface-типах
passport.rs   ordinary_type_name: RecordTerm -> Record<Name>
resolve.rs    имена record в пространстве имён теории
checker       проверка well-foundedness, RecursionScheme, записи, суммы, проверка ссылок на записи
runtime.rs    отдельный поток со стеком 1 GiB, бюджет стека, топливо, проверка убывания меры,
              диспетчеризация ordinary/fn до legacy-таблицы, вынос веток eval_expr в малые функции
```

### Проверки

```powershell
cargo test --workspace
cargo test -p dlm_core --test ordinary_recursion_records
cargo run -p dlm_cli -- run examples/valid/ordinary_recursion.dlm
```

## v0.75.0 — Lambdas, Function Values, Higher-Order Functions, Lexicographic Measures

Дата: 2026-10-27

### Добавлено

```text
Fn<A, ..., R>                 тип функций (канонический Fn<A,...,R>)
fun(x: T, ..., body)          лямбды через function::lambda_term, захват окружения по значению
функции как значения          имя fn как значение, функции в списках/записях/результатах, вызов f(args)
map/fold/case                 принимают любое функциональное значение
decreasing (m, n)             лексикографическая мера, RecursionScheme<...:lexicographic:...>
crates/dlm_core/tests/ordinary_higher_order.rs                     10 тестов
examples/valid/ordinary_higher_order.dlm, ordinary_lexicographic.dlm
examples/invalid/ (8 примеров)
```

### Правила

```text
функциональные значения — чистые и тотальные (иначе E0929); эффектные fn можно вызывать, но не передавать
taint функционального значения виден: результаты помечены, map/fold отклоняют, --trusted-only отклоняет
рекурсивная fn не может ссылаться на себя как на значение и вызываться внутри лямбды (E0935)
функциональные привязки не могут называться как встроенные функции (E0002)
```

### Изменено

```text
ast.rs        ExprKind::Lambda; FnMeasure.params (было param); Expr: PartialEq/Eq
parser.rs     fun(...), decreasing (m, n)
ordinary.rs   OrdinaryType::Fn, fn_signature, fn_type_text
checker       FnArg, infer_lambda, вызов значений, lookup_function_value, effects_of, лексикографическая проверка
runtime       RuntimeValue::Closure, apply_callable, вызов значений, лексикографическая проверка убывания
```

### Ломающие изменения (Rust API)

`FnMeasure.param: String` заменено на `FnMeasure.params: Vec<String>`. Имя `fn`, использованное как значение, теперь даёт паспорт `Value<Fn<...>>` вместо `FunctionType<...>`.

### Проверки

```powershell
cargo test --workspace
cargo test -p dlm_core --test ordinary_higher_order
cargo run -p dlm_cli -- run examples/valid/ordinary_higher_order.dlm
```

## v0.76.0 — Statements, Theorems and Proofs

Дата: 2026-10-27

### Добавлено

```text
theorem name: P                           доказательство вычислением
theorem name(x: Nat, b: Bool) below N: P   доказательство перебором ограниченного утверждения
axiom name(x: T): P                        аксиома: Theorem с trust=Axiom, validation=Assumed
conjecture name(x: Nat) tested N: P        свидетельство: открытая цель Goal, не теорема
dlm certify <file>                         сертификаты доказательств и метод доказательства
dlm explain                                счётчики доказанных теорем, аксиом и открытых целей
Runtime::evaluate_statement                вычисление утверждения на стеке runtime
crates/dlm_core/src/checker/statements.rs
crates/dlm_core/tests/ordinary_theorems.rs (10), crates/dlm_cli/tests/cli_certify.rs (2)
examples/valid/ordinary_theorems.dlm, examples/invalid/ (9 примеров)
docs/PROOF_SURFACE.md
```

### Правила

```text
тело утверждения — чистое тотальное Bool-выражение без ссылок на значения теории
переменные theorem/conjecture — Nat или Bool; не более 100000 случаев
ложная теорема или опровергнутая гипотеза — ошибка E0909 с контрпримером
неограниченное универсальное утверждение нельзя доказать проверкой: только axiom или conjecture
```

### Изменено

```text
ast.rs        TheoryItem::Statement, StatementItem, StatementItemKind
parser.rs     theorem/axiom/conjecture (строки axiom раньше молча игнорировались)
ordinary.rs   render_expr перенесён сюда; statement_proposition
checker.rs    CheckReport.certificates
runtime.rs    on_runtime_thread, evaluate_statement, StatementEvaluation; имена утверждений в runtime
soundness.rs  proved_theorems, axiom_theorems, open_goals
dlm_cli       команда certify
```

## v0.77.0 — Symbolic Proofs of Unbounded Statements

Дата: 2026-10-27

### Добавлено

```text
crates/dlm_core/src/symbolic.rs        символическое ядро: многочлены над атомами, раскрытие определений,
                                       безопасные pred/monus, разрешимые nat_eq/>/if, β-редукция, леммы, индукция
theorem ...: P by normalize [using l]  доказательство нормализацией
theorem ...: P by induction n [using l] индукция по Nat через объекты ядра InductionScheme/BaseCase/StepCase/InductionProof
crates/dlm_core/tests/symbolic_proofs.rs (6), symbolic_kernel_soundness.rs (3: фаззинг 5000 утверждений)
examples/valid/symbolic_proofs.dlm, examples/invalid/ (5 примеров)
```

### Правила

```text
неограниченная теорема над Nat требует by normalize / by induction (или below N для ограниченной версии)
леммы: замкнутые теоремы, символические теоремы, аксиомы (аксиома → результат Axiom)
ограниченные теоремы и гипотезы леммами быть не могут
неудача — E0909 с остаточной нормальной формой
```

### Проверка корректности ядра

Фаззинг: 3000 случайных утверждений через нормализацию и 2000 через индукцию. Около половины доказываются, и каждое доказанное перепроверяется исполнением на сетке 8×8. Внесённая намеренно некорректная мутация правила `pred` этим тестом ловится.

## v0.78.0 — Inequalities, List Induction, Runtime Cross-Check

Дата: 2026-10-27

### Добавлено

```text
symbolic.rs        Fact (= 0 / >= 0), неотрицательные комбинации с гипотезой; списки и Option в ядре;
                   переменные List<Nat>; индукция по списку: [] / cons(h, t)
induction.rs       list_induction_scheme, list_induction_base_case, list_induction_step_case, list_induction_proof;
                   TypeKind::ListInductionScheme
checker            перекрёстная проверка runtime для каждой символической теоремы (Nat 0..6, 40 списков);
                   расхождение — E0909 "kernel/runtime disagreement"
runtime            домен List<Nat> в evaluate_statement (списки длины <= 3)
dlm certify        строки crosscheck:
tests              symbolic_lists_inequalities.rs (8), symbolic_kernel_soundness.rs (+3), unit-тест правила комбинации
examples           valid/symbolic_lists_inequalities.dlm; invalid/ (3 примера)
```

### Правила

```text
теорема над List<Nat> требует by normalize / by induction (перебором списки не доказываются)
List<T> с T != Nat и Bool-переменные в символических доказательствах пока не поддерживаются
перекрёстная проверка не является доказательством; большие сетки (> 20000) пропускаются с записью в истории
```

### Проверка корректности

Фаззинг неравенств (3000) и списков (2000), каждое доказанное утверждение перепроверяется исполнением. Мутации «отрицательный множитель неравенства» и «length без известного префикса» ловятся тестами.

## v0.79.0 — Step Induction, Generalized Hypotheses, Bool Cases

Дата: 2026-10-27

### Добавлено

```text
by induction n step s        базы P(0)..P(s-1), шаг n = k+s из P(k)..P(k+s-1); s = 1..4, только Nat
generalizing x, ...          гипотеза индукции для всех значений x (правило переписывания с переменными-образцами)
Bool-переменные              разбор случаев (до 6 переменных); Bool-гипотеза как истина в цели шага
induction.rs                 nat_step_induction_scheme / _base_case / _step_case / _proof; TypeKind::NatStepInductionScheme
symbolic.rs                  Fact::Holds, prove_obligation_with (несколько гипотез), prove_by_induction_with,
                             ограничение глубины MAX_SYMBOLIC_DEPTH = 384, поток ядра 256 MiB, защита от циклов переписывания
ast.rs                       ProofHint.induction: InductionOptions { step, generalizing }
tests                        symbolic_advanced_induction.rs (9), symbolic_kernel_soundness.rs (+2)
examples                     valid/symbolic_advanced_induction.dlm; invalid/ (3 новых)
```

### Исправлено

```text
лемма вида h(n) = g(n), где g раскрывается обратно в h(n), вызывала переполнение стека и аварийное завершение;
теперь повторное переписывание тем же правилом не применяется, глубина ограничена
```

### Изменено

```text
examples/invalid/symbolic_proof_requires_nat_variables.dlm: Bool-переменные теперь допустимы, пример переведён на List<Bool>
сообщение: "symbolic proofs support Nat, Bool and List<Nat> variables"
```

## v0.80.0 — Modules, Theory Extension, Standard Library

Дата: 2026-10-27

### Добавлено

```text
import a.b.c                     загрузка модулей из файлов: D/a/b/c.dlm, D/c.dlm, R/a/b/c.dlm, --lib, DLM_PATH
theory Name extends Base         наследование fn, record, теорем/аксиом/гипотез и лемм (не let-значений)
private fn/record/theorem/...    скрыто от теорий других модулей (и по цепочке наследования)
program.rs                       Program, load_program, load_program_with_paths, link_program; циклы, отсутствующие
                                 модули, несовпадение имени модуля, одна теория в двух модулях — E0917
Checker::check_program           сначала импортированные модули, отчёт — по корневому; ошибки импорта с префиксом
Runtime::run_program             импортированные модули дают только объявления
dlm --lib <dir>                  пути поиска модулей
lib/std/nat.dlm, lib/std/list.dlm стандартная библиотека: 20 доказанных теорем
symbolic.rs                      леммы с вложенными образцами (sum_list(inc_all(xs))) — синтаксическое сопоставление
tests                            modules.rs (7), program.rs (2), cli_modules.rs (2), example_corpus (+1: библиотека),
                                 symbolic_kernel_soundness (+1: фаззинг вложенных лемм)
examples                         valid/std_library_usage.dlm; invalid/ (7)
docs/MODULES.md
```

### Изменено

```text
check_module для модуля с import теперь выдаёт E0917 (импорты раньше молча игнорировались)
имя теории с пробелами — ошибка разбора
```

## v0.81.0 — `dlm audit`, Projects, Mutual Recursion

Дата: 2026-10-27

### Добавлено

```text
audit.rs                 ProgramAudit из паспортов: метод доказательства, транзитивные леммы и аксиомы (через модули),
                         перекрёстная проверка, компоненты доверенной базы, статус closed/axiom-dependent/unsafe,
                         отпечаток fnv64; render_program_audit ("DLM-AUDIT v1")
project.rs               dlm.project (name, lib, roots, policy), load_project, check_project, ProjectReport
dlm audit / dlm project  новые команды CLI (project: --audit, --trusted-only)
dlm.project              манифест репозитория: lib/std + examples/valid
mutual { fn ... fn ... } взаимная рекурсия с общей убывающей мерой; группа обязана быть чистой и тотальной
FnDecl.mutual            имена членов блока
E0935 "declared later"   вызов функции, объявленной ниже, вне mutual-блока
tests                    audit_project.rs (7), mutual_recursion.rs (5), cli_audit_project.rs (2), project.rs (1)
examples                 valid/mutual_recursion.dlm; invalid/ (3)
docs/AUDIT_AND_PROJECTS.md
```

## v0.82.0 — Multi-line Declarations, `let`, `match`

Дата: 2026-10-27

### Добавлено

```text
логические строки           продолжение при открытых скобках или после = , => + : in by; строка "by ..." — к утверждению выше
ExprKind::Let               let x = v in body: checker / runtime (вычисление один раз) / ядро (подстановка) / проверка рекурсии
скобки группировки          (a + b) + c
match                       0, 1.., succ(k), [], cons(h, t), none, some(x), true, false, _, x — разворачивается парсером
                            в if_then_else + let + __match_head / __match_some; полнота и достижимость ветвей проверяются
псевдонимы меры             let k = pred(n) / tail(xs) и pred/monus/tail от псевдонима принимаются как убывание
разбор случаев в ядре       by normalize делит Nat-переменную на 0..c и n'+c+1 (глубина 3, c <= 16, <= 128 случаев)
зарезервированные имена     идентификаторы, начинающиеся с __
tests                       patterns_let.rs (6), symbolic_kernel_soundness (+1: фаззинг let/match/разбора случаев)
examples                    valid/patterns_and_let.dlm; invalid/ (4)
docs/PATTERNS_AND_LET.md
```

### Изменено

```text
let и match внутри выражения с операторами требуют скобок
```

## v0.83.0 — Text, Line I/O, Arguments, `fn main`

Дата: 2026-10-27

### Добавлено

```text
ExprKind::TextLiteral        "..." с экранированием \n \t \r \0 \\ \"; парсер маскирует литералы (__text_N)
текстовые встроенные         concat, text_length, text_eq, nat_to_text, text_to_nat, substring, lines, words, join
ввод-вывод                   read_line (Option<Text>), read_all, args (List<Text>), print_line; эффекты io@stdin/io@args/io@stdout
fn main()                    точка входа; Nat-результат — код выхода (до 255); RunReport.main, exit_code()
dlm run                      --stdin-file PATH, аргументы после --
ядро                         Text-литералы как различимые значения; свёртка concat/text_length/text_eq/nat_to_text
Text в теоремах              символические теоремы над Text; перекрёстная проверка на 5 образцах
tests                        text_io.rs (5), cli_programs.rs (2)
examples                     valid/text_and_io.dlm, valid/word_count.dlm; invalid/ (3)
docs/TEXT_AND_IO.md
```

## v0.84.0 — Compiler: IR and C Backend

Дата: 2026-10-27

### Добавлено

```text
compile.rs              IR (IrProgram / IrFunction / IrExpr / IrInit), lower_program: стирание утверждений,
                        мер и топлива; разрешение имён (слоты, глобальные, функции, встроенные); замыкания с явным захватом
backend_c.rs            emit_c, build_c_executable (cc -O2 -std=gnu11 -lpthread), порядок вычисления слева направо
backend_c_runtime.c     среда исполнения C: u128 Nat с проверкой переполнения, Bool, Text (UTF-8), List/Sequence,
                        Option/Result, Pair, Sum, записи, замыкания, вывод как display_atom, stdin по требованию, args,
                        стек 1 GiB для доказанно завершающейся рекурсии
dlm build               -o, --emit-ir, --c-only, --trusted-only, --lib
E0950 CompileError      конструкция без реализации в бэкенде (мосты, мета-уровень, GPU/remote ...)
Checker                 проверка идёт в отдельном потоке со стеком 256 MiB (глубоко вложенные выражения не роняют процесс)
tests                   compile_c.rs (5: примеры, ввод-вывод, ошибки и глубокая рекурсия, понижение, сгенерированные программы),
                        cli_programs.rs (+1)
docs/COMPILER.md
```

## v0.85.0 — Int: точные целые со знаком

Дата: 2026-10-27

### Добавлено

```text
Int                     новый тип: точное целое (i128 при исполнении, переполнение — ошибка исполнения)
a - b, -a               вычитание и унарный минус только для Int; у Nat вычитания нет (monus или int(a) - int(b));
                        `-7` — константа Int; `+` и `-` одного приоритета, левоассоциативны
встроенные              int, abs, int_eq, div, mod (тотальные, евклидовы: div(a, 0) = 0, mod(a, 0) = a; есть и для Nat),
                        int_to_text, text_to_int
Checker                 Int и Nat не смешиваются неявно (E0937 с подсказкой int(n) / monus)
Runtime                 RuntimeValue::IntExact, проверка теорем на Int по значениям -5..5
Symbolic kernel         Sym::Int: нормализация многочленов с целыми коэффициентами; int_eq и > решаются только при
                        постоянной разности; abs(x) = abs(-x); div/mod на 0 и 1; strict_opaque — неизвестные термы
                        не считаются Nat, если в теории есть Int
Compiler                DV_INT (__int128) в среде исполнения C, те же сообщения об ошибках, что у dlm run
examples                valid/integers.dlm, invalid/nat_subtraction.dlm, invalid/int_nat_mixing.dlm,
                        invalid/int_positivity_unproved.dlm
tests                   integers.rs (6, включая фаззинг ядра на 3000 утверждений с проверкой мутациями),
                        compile_c.rs (+1: сгенерированные Int-программы и переполнения)
docs/INTEGERS.md
```


## v0.86.0 — Скомпилированные программы: машинные значения, освобождение памяти, проверка диапазонов

Дата: 2026-10-27

### Добавлено

```text
скалярные функции      чисто числовые функции получают параметры, локальные значения и результат nat_t / int_t / int
                        без упаковки; вызовы между ними не выделяют память
малые числа             Nat 0..2^62-1 и Int -2^62..2^62-1 хранятся в машинном слове с меткой, без выделения памяти;
                        большие значения сохраняют полный диапазон u128 / i128 и проверки переполнения
регионы вызовов         каждый вызов освобождает временные объекты; результат поднимается вызывающему;
                        старые аккумуляторы fold освобождаются; прямой хвостовой вызов самой себя становится циклом
--check-overflow        отчёт об обязательствах машинной арифметики (интервальный анализ, независимый от доказательств)
--require-no-overflow   отказ в сборке при открытых обязательствах
Windows x64             MinGW-w64, CreateThread со стеком 1 GiB, аргументы UTF-8, двоичные stdin/stdout,
                        tools/build_windows.ps1, .github/workflows/native.yml
tests                   machine_arithmetic.rs (5), compile_c.rs (+5), cli_programs.rs (+1)
```

## v0.86.1 — Группы выживших объектов

Дата: 2026-10-27

### Исправлено

```text
backend_c_runtime.c     при каждом возврате v0.86.0 заново обходил весь возвращаемый граф, поэтому граф, который
                        поднимается через много вызовов (например, цепочка замыканий), обходился на каждом уровне:
                        O(глубина²), 40 000 уровней — 90,7 с. Теперь выжившие после сборки объекты образуют группу;
                        если все её корни снова достижимы, группа сохраняется целиком без обхода, иначе разбирается
                        поблочно. 40 000 уровней — 0,45 с, память та же (19–25 МБ)
                        владение блоком определяется порядковым номером выделения, отметки — номером сборки
tests                   compile_c.rs (+2: линейная работа сборщика на цепочке 20 000 уровней и точное разбиение
                        групп; 10 сгенерированных программ с общими частями результатов против dlm run)
docs                    COMPILER.md, IMPLEMENTATION_NOTES.md, TEST_MATRIX.md, ROADMAP.md, STAGE_READINESS_MODEL.md,
                        запись v0.86.0 в UPDATE.md переведена на русский
```

## v0.87.0 — Семантика и Typed Core (этапы A и B)

Дата: 2026-10-27

### Добавлено

```text
docs/SEMANTICS.md          нормативная семантика исполняемого языка: порядок вычисления, ленивость, эффекты,
                           арифметика (Defined_128), категории отказов, политики ресурсов, наблюдаемое поведение
docs/TYPED_CORE.md         Typed Core, ID и Span, типизированное IR, верификатор
docs/LAW_STATUS.md         состояние законов 1–40 и этапов A–H по свидетельствам (закон 42), без процентов
docs/DLM_ARCHITECTURAL_LAWS.md, docs/DEVELOPMENT_PROGRAM.md
                           документы автора (законы — новая редакция; программа — без служебных элементов веб-страницы)
ExprId, Span               у каждого выражения уникальный идентификатор и точный физический диапазон
                           (UTF-8 байтовые колонки, многострочные выражения, литералы с экранированием, match)
TypedFacts                 checker записывает тип каждого выражения и контракт каждой функции
                           (типы, эффекты с учётом вызываемых функций, чистота, завершение)
типизированное IR          DLM-IR v2: тип, Span и исходный ExprId у каждого узла; сигнатура, эффекты,
                           завершение и стабильный символ module::Theory.name у функций
verify_ir                  проверка после lowering: типы, определение до использования, вызовы, операторы,
                           результат функции; отказ — внутренняя ошибка компилятора
dlm contracts              контракты функций: эффекты, завершение, машинная арифметика, стек, отказы,
                           математическая спецификация — независимые свойства
Runtime::observe_program   наблюдаемое поведение: вывод до ошибки, сообщение, код выхода
```

### Изменено

```text
скалярный backend          типы берутся из IR (унификация удалена); хвостовые вызовы самой себя — цикл
анализ переполнений        диапазоны параметров и результатов по типам IR, без поиска сигнатур по строкам
стек                       исчерпание стека в скомпилированной программе — ошибка исполнения с кодом 1, а не сбой
dlm run                    печатает вывод, сделанный до ошибки исполнения (как скомпилированная программа)
STAGE_READINESS_MODEL.md   проценты больше не обновляются, состояние — в LAW_STATUS.md
```

### Тесты

```text
typed_core.rs              7 тестов: Span и ID, типы IR равны фактам checker'а, верификатор отвергает
                           8 видов повреждений IR, независимые свойства контрактов, исчерпание стека,
                           совпадение интерпретатора и компилятора с ошибками (12 случаев, 24 программы)
cli_programs.rs            +1: вывод до ошибки в dlm run, dlm contracts
compile_c.rs, cli_programs.rs  формат DLM-IR v2
```

## v0.87.1 — Укрепление верификатора Typed Core (этап B)

Дата: 2026-10-28

Проверка исходников подтвердила основание этапов A/B версии 0.87.0. До перехода к этапу C обнаружен пробел:
верификатор принимал повреждённые операции контейнеров и высших порядков, потерянные эффекты и некоторые
неверные слоты/инициализаторы. Пять первоначальных групп регрессионных тестов воспроизвели эти ошибки.

### Изменено

* Для всех `COMPILED_BUILTINS` проверяются типы операндов/результата и исполняемая арность. `ok`/`err`
  принимают один операнд после стирания типа; `map`/`fold` сохраняют тип коллекции и контракт функции.
* Проверяются пустые конструкторы, типы ветвей `case`, номинальный тип записи, уникальность имён её полей
  и тип операнда проекции. Полная схема записи остаётся открытым обязательством.
* Эффекты builtin и вызываемых функций нельзя потерять в контракте вызывающего. Эффектная функция не
  становится чистым функциональным значением; лямбды сохраняют чистоту. Классификация эффектов едина с checker.
* `let` не перезаписывает активный слот; неверные размеры кадров и переполнение числа захватов отвергаются.
  Инициализаторы проверяются как типизированные вызовы без параметров/захватов; каждый global инициализируется
  ровно один раз.
* В тестах C backend stdin подаётся из временного файла: устранена гонка `BrokenPipe`, когда программа
  завершается до записи входа. Сравнение вывода и exit status сохранено.

### Проверка и граница гарантии

`tests/ir_verify.rs`: шесть групп регрессий, включая таблицу всех 54 builtin с мутациями результата,
ограниченных операндов и арности. Существующие проверки IR, интерпретатора и C backend сохранены.

```text
cargo test --workspace --locked --offline
```

Полный прогон: 556 тестов пройдено, 0 ошибок, 0 ignored. Проверено на Linux x86-64 с Rust 1.99.1 из
приложенного архива и GCC 13.3.0. Windows в этом прогоне не проверялся.
Затронуты законы 6, 11, 23, 25, 30 и 39; формат DLM-IR v2 и публичные структуры IR сохранены.
Структурный verifier не доказывает семантическую эквивалентность или завершение. Open этапа B: схемы и ID
записей, факты управления для match, file ID в Span, дальнейшее разрешение имён. Этап C (независимые proof
terms и checker), владение, FFI и плотные обобщённые коллекции не реализованы этим патчем.
## v0.88.0 — Схемы записей и независимая проверка доказательств равенства

Дата: 2026-10-28

### Этап и результат

Продолжение версии 0.87.1 по `ДОКУМЕНТ.txt` и разделу 42 архитектурных законов. Закрыто обязательство B по
передаче и проверке схем записей; начат ограниченный фрагмент C с самостоятельным повторным проверяющим ядром.
Полный этап C не объявляется завершённым.

### Typed Core и записи

* Checker передаёт `RecordFacts`: проверенные имена/типы полей и исходную строку объявления.
* Lowering назначает локальные `RecordId`; `RecordRef(id)` сохраняется во всех вложенных типах выражений,
  слотов, параметров, результатов и globals. Наследование сохраняет ID, одноимённые разные записи различаются.
* `IrProgram::records` хранит схемы и стабильные исходные символы. Конструкторы и проекции несут ID схемы,
  проекции — индекс поля. Verifier проверяет арность, порядок/имена/типы полей, номинальный тип результата,
  тип/индекс проекции и отсутствие неизвестных/неразрешённых/циклических ссылок.
* Формат листинга изменён на `DLM-IR v3`. Синтаксис DLM и runtime-представление записей сохранены.

### Доказательные объекты и replay

* `proof_kernel.rs`: плоские DAG термов и выводов для математических Nat, проверяемые без символического
  поиска, runtime и исходника. Рефлексивность, симметрия, транзитивность, конгруэнтность, 12 алгебраических
  законов, вычисление одной операции над литералами и явный внешний контекст аксиом.
* Аксиома владеет собственными термами/областью переменных; переупорядочивание таблицы непроверенного
  доказательства не меняет разрешённую предпосылку. Использованные аксиомы остаются в отчёте. Сам артефакт
  не может объявить себе аксиомы: CLI и прикрепление к сертификату проверяют с пустым контекстом.
* Бюджеты размера, глубины, работы и текста; итеративный checker; защищённое от разрастания DAG представление
  утверждения. `UnknownWithinBudget` отличается от `Invalid`. Превышение точной числовой ёмкости checker'а
  также даёт неизвестность, а не отрицание математического утверждения.
* Для поддержанного `by normalize` без `using` отдельный producer выдаёт доказательство, которое заново
  проверяется и связывается с точным утверждением сертификата. Повреждение шага отвергается после rehash.
* Сертификаты с DAG используют v2 и `proof_evidence: kernel-replay`. Остальные сохраняют v1 и явно показывают
  `legacy-trusted`; поле `independently_checked` в аудите отделяет эти гарантии. Новый trusted component —
  `equational-replay-kernel`; прежние зависимости проверки не скрываются.

```text
dlm proof-export --trusted-only examples/valid/equational_replay.dlm Equations.identity > identity.kproof
dlm proof-check identity.kproof
```

Артефакт объявляет отдельно формат `DLM-KERNEL-PROOF v1` и правила `nat-semiring-v1`. `proof-export` выдаёт
один чистый артефакт выбранной теоремы; при отсутствии независимого свидетельства команда отказывает.
`proof-check` работает без исходника и не запускает исходную тактику.

### Совместимость Rust API

Добавлены `RecordId`, `RecordFacts`, `IrRecord`, `TypedFacts::records`, `IrProgram::records`,
`OrdinaryType::RecordRef` и `ProofCertificate::kernel`. `IrOp::Record` дополнен ID, `IrOp::Field` стал
структурированным вариантом с layout/index/name. `CertificateAuditReport` дополнен `independently_checked`,
`TrustedComponent` — вариантом `EquationalReplayKernel`. Потребители структур, exhaustive match и IR v2 должны
обновиться; старые v1-сертификаты сохраняют прежний fingerprint. `IrProgram::source_type` служит только
отображению исходных имён, не backend-проверкам.

### Проверки и границы

Добавлены `record_schemas.rs` (3 группы), `equational_kernel.rs` (6 групп), `cli_proof_replay.rs` (1 сценарий)
и два исполняемых примера, включённых в обязательное сравнение C backend с интерпретатором. CLI-тест удаляет
исходник после экспорта. Готовый проверенный артефакт — `examples/proofs/identity.kproof` — также включён.
CLI проверяет сохранённый файл, затем отклоняет его повреждение. Проверки ядра включают
подмену шагов/целей/связей/переменных, аксиомы, rehash, бюджеты, формат и независимое вычисление алгебраических
законов на конечной сетке. Команда общего прогона: `cargo test --workspace --locked --offline`.

Конфигурация: Rust 1.99.1 из приложенного архива, Linux x86-64, GCC 13.3.0. Windows в этой сессии не проверялся.
Полный прогон workspace: 566 тестов пройдено, 0 failed, 0 ignored. После финальной проверки бюджета перед
хешированием повторно пройдены 17 тестов ядра/сертификатов. Clippy завершился без ошибок; существующие
предупреждения других подсистем сохранены без косметического рефакторинга.
Затронуты законы 1, 3, 11–13, 21–25, 29, 32, 39 и критерии B/C. Доказательства остаются compiler/proof-only;
в горячие runtime-данные не добавлены.

Open: file ID/диагностики и факты управления match в B; определяющие равенства функций, произвольные
полиномиальные свидетельства, применение лемм, подстановка, связки, кванторы и Nat/list-индукция в C.
Их прежняя проверка остаётся явно доверенной. Новое ядро не доказывает отсутствие машинных переполнений,
корректность C backend, владение или собственную soundness. Подробности — `docs/RECORD_SCHEMAS.md`,
`docs/EQUATIONAL_REPLAY.md`, `docs/LAW_STATUS.md`.


## v0.89.0 — Finite computational mathematics

Added exact bounded F2 matrices, kernels/images/ranks/tensor products, finite
chain complexes, homology and cohomology representatives, chain-map induced
ranks, total tensor complexes, simplicial face closure, and `dlm homology`.
Added finite category/functor/natural-transformation validation and ordinary DLM
scalar library `std.f2`. See `docs/FINITE_MATHEMATICS.md` for APIs, limits,
resource model, assurance and the explicit remaining mathematical roadmap.

This is a computational library increment, not completion of stage C, HoTT,
or all mathematical theories. Matrix/topology/category APIs are host-side Rust;
only scalar F2 functions are currently exposed in DLM source. Existing IR and
proof formats are unchanged. Patch files are cumulative over the uploaded source.

Validation (Rust 1.99.1, Linux x86-64): 579 workspace tests passed, 0 failed,
0 ignored; 13 added tests. Exhaustive subcases include 512 binary 3x3 matrices
and 64 graphs on four vertices. `cargo clippy --workspace --all-targets` passes
with existing subsystem warnings; no warnings refer to the added math files.
CLI torus and RP2 fixtures were also executed directly.

## v0.90.0 — Source F2 mathematics and independent witnesses

Added `std.matrix_f2` and `std.homology_f2`: ordinary checked DLM records and
functions for bounded exact matrices, RREF, ranks, kernels/images, tensors,
basis extension, homology/cohomology and finite chain complexes. No opaque
mathematical builtins were added. Source public axes are 32, host axes 256.

Added exhaustive Result patterns to parser, checker, both runtimes and Typed
Core builtin checks. IR verification now requires dominating same-subject
guards for partial List/Option/Result projections, addressing the previous B
match-control obligation. This does not close the remaining file-ID/HIR work.

Added independent F2 computation-witness checking: inverse/row-reduction/rank
identities and complete homology quotient bases; strict bounded `.f2proof`
transport; `homology-export` / `homology-check` with optional external claim
binding. The verifier does not run Gaussian elimination or homology search.
Assurance remains runtime-witness; no Nat-kernel proof rule or StaticProof is
created. All existing Rust APIs and IR grammar remain compatible; old backends
need the two new internal Result projection operations to execute new programs.

See `docs/MATH_SOURCE_AND_WITNESSES.md` for APIs, resource bounds, trusted base,
formats, commands and remaining obligations. Mathematical scope remains finite
F2: integral torsion, generic algebraic structures, derived mathematics and HoTT
are not claimed implemented. The patch is cumulative over the uploaded source.

Validation (Rust 1.99.1, Linux x86-64): 599 workspace tests passed, 0 failed,
0 ignored, including 20 added tests. After the final Result reachability check,
all 11 parser/IR-pattern and corpus tests were repeated successfully. Clippy
passes for the workspace and all targets with existing subsystem warnings; no
warnings refer to the new math, guard, CLI-command or test modules.

Differential source/interpreter/host/C tests include 64 binary 2x3 matrices and
point/circle/disk/sphere/torus/RP2 computations. Independent witness tests include
512 binary 3x3 matrices, 64 four-vertex graphs and all 135 single payload-bit
mutations of the circle fixture. Exported proofs replay after source deletion;
external-boundary mismatches and forged noncycles/boundaries are rejected.
Native Windows/MinGW was not tested in this run.

Packaging: cumulative changed/new files only, with `DLM-main/` paths. Apply over
the uploaded original archive or over 0.89.0. Source/API details and remaining
obligations are in `docs/MATH_SOURCE_AND_WITNESSES.md`.

## v0.91.0 — Finite algebra and explicit progress tracking

Added `math::algebra`: validated finite groups, subgroups, normal quotients,
homomorphisms, kernels/images and the checked first-isomorphism construction;
unital rings, finite fields (including supplied extension-field tables),
left modules and ring/module maps. Group-as-category conversion reuses the
existing finite category validator. Host orders are bounded by 64.

Added ordinary DLM `std.finite_algebra` (orders <=8) and `std.prime` (canonical
prime-field residues p<=251, exponent limit 1024), including errors on raw bad
records, invalid laws/maps, size/domain violations and zero inversion. Source,
interpreter, host and C results are tested differentially. Five closed source
specifications use the existing finite-evaluation certificate path.

Fixed field-label metadata being mistaken for function dependencies, captures
or recursion references; real calls in the record operand still undergo all
checks. Existing executable APIs, IR grammar and proof formats are unchanged.

`docs/FINITE_ALGEBRA.md` defines operations, assurance, limits and remaining
algebra obligations. `docs/MATHEMATICS_PROGRESS.md` records per-patch criteria
and remaining work, keeping a coarse planning estimate separate from technical
readiness. No StaticProof, kernel extension, non-F2 matrix coefficients, general
algebra completion or higher mathematical theory is claimed by this patch.

Validation: 614 workspace tests passed, 0 failed, 0 ignored; 15 added tests.
Rust 1.99.1, Linux x86-64, C backend differential execution. All 59,081 table /
identity candidates of orders 2 and 3 are covered on the host. Source tests
include all order-2 tables, supported primality through 251, noncommutative
rings, quotient maps, F4 and failure of mere additivity to imply module linearity.
Clippy passes for the workspace/all targets with existing subsystem warnings;
no warnings refer to new algebra files. Native Windows/MinGW was not tested.

Remaining: the defined bounded 0.91 acceptance scope is closed (12/12), while
general law proofs, unbounded algebra, ideals and quotient rings/modules,
non-F2 matrices/homology, arbitrary precision and higher mathematics remain.
The coarse planning estimate for remaining work in the entire discussed
mathematical direction stays 90–95%; it is not a measured readiness score.

## v0.92.0 — Exact bounded rationals and oriented prime-field topology

Added private-invariant host Rational with exact u128 signed magnitudes,
wide checked arithmetic/cancellation and exact ordering; ordinary source
`std.rational` with canonical 32-bit profile and explicit errors. Added dense
matrices over primes <=251, pivot normalization and signed kernels, bounded
basis extension, oriented simplicial chains, homology/cohomology and induced
ranks. Source `std.matrix_fp`/`std.homology_fp` use existing language/compiler
paths and validate raw records. `dlm homology --prime p file.simp` exposes the
new coefficients; old F2 commands and standalone witness formats are unchanged.

`docs/NUMERIC_FIELDS.md` states numeric/resource profiles, assurance and open
obligations. This patch does not add arbitrary precision, Q matrices, integral
torsion, independent Fp/Q witnesses, kernel rules or a general field-law proof.
Six new closed specifications retain legacy finite-evaluation assurance.

Validation: 631 workspace tests passed, 0 failed, 0 ignored; 17 new tests.
Linux x86-64, Rust 1.99.1, GCC 13.3.0; Clippy workspace/all-targets passes with
existing subsystem warnings, none in the new numeric modules/tests. Rustfmt
checks pass for the changed Rust modules/tests. The combined new example has
identical interpreter/C output. Native Windows/MinGW was not tested.

Packaging: cumulative changed/new files only (97 files), verified ZIP CRC and
complete content overlay over the uploaded original or 0.91.0; 26 files differ
from 0.91.0. Per-patch acceptance is closed (12/12; 0/12 remaining), with open
numeric/proof obligations retained in NUMERIC_FIELDS.md and progress journal.
The rough planning estimate remains 90–95% of the broad mathematical direction
still to do; it is not a measured readiness or correctness score.

## v0.93.0 — Exact numeric budgets and rational matrices

Added separate limb-based exact Natural/Integer/Rational host APIs with private
canonical representations, configurable precision (1..16384 bits, default 4096)
and shared explicit work budgets. Added exact Q matrices: RREF/rank/kernel/image,
signed determinants, inverses and general solutions with homogeneous kernels,
including multiple right-hand sides and empty shapes. `dlm matrix-q` exposes
precision/work, inverse and solve controls with bounded strict text input and
no partial success output on failure.

Ordinary `std.matrix_q` provides bounded rational matrix construction/arithmetic,
RREF/rank/kernel/image with Result error propagation and interpreter/C parity.
It retains source rational 32-bit magnitudes and axes<=8. Existing Nat/Int,
0.92 profiles, IR/compiler and independent proof formats are preserved.

The two requested future topics are recorded in FUTURE_MATH_APPLICATIONS.md:
positive geometries (associahedra, cosmological polytopes, amplituhedra) and
formal symbolic huge-number research including the BIG FOOT question.
Both are deferred; current source arbitrary precision, general proofs and
these mathematical applications are not claimed complete.

Validation: 649 workspace tests passed, 0 failed, 0 ignored; 18 tests added.
Linux x86-64, Rust 1.99.1, GCC 13.3.0; 6426 independent integer/Fraction vectors
and 50 Fraction/Leibniz matrix fixtures, plus 625 exhaustive integer matrices
and 81 source matrices. Clippy workspace/all-targets and rustfmt checks pass;
existing subsystem warnings remain, with none in new files. The new source
example has identical interpreter/native-C output. Windows/MinGW not tested.

Packaging: 114 cumulative changed/new files only; 29 differ from 0.92. ZIP CRC
and full overlays over the uploaded source and 0.92.0 are verified. No source
deletions. The defined 0.93 scope is closed (12/12, 0/12 remaining). Source
arbitrary precision, Q witnesses/proofs, proof-kernel/resource obligations and
the deferred applications remain open. The coarse whole-direction remaining
work estimate stays 90–95%; it is not a measured readiness score.

## v0.94.0 — Exact integers and rationals in ordinary DLM

Added `std.exact`: canonical radix-65536 signed integers/rationals, source
precision 1..1024 bits, strict decimal/fraction input, exact operations/order,
Euclidean division, gcd, explicit immutable budget threading and Result errors.
Raw records are revalidated; zero signs/fraction normalization and domain/
precision/work errors are explicit. Private word algorithms use bounded Nat
intermediates and fixed input/step limits. Rational cancellation precedes final
precision checks. Two closed specifications use legacy finite evaluation.

This is an ordinary source library compiled through the existing C backend;
source Nat/Int, IR/compiler builtins, native numeric tags/layouts and old
rational/matrix profiles are preserved. Source reservation units differ from
host work counters and are not formal cost certificates or global gas tokens.
Source BigRational matrices, source precision extension, dense layouts and
independent arithmetic/Q evidence remain open. Both requested distant
application tracks stay deferred.

Validation: 659 workspace tests passed, 0 failed, 0 ignored; 10 tests added.
Linux x86-64, Rust 1.99.1, GCC 13.3.0; 124 independent Python integer/Fraction
vectors plus boundary/raw-record/budget/private-helper tests agree across
source, interpreter and native C. CLI run/build output agrees. Clippy/rustfmt
pass with existing subsystem warnings and no warnings in new files.
The native example passes AddressSanitizer/UBSan with leak detection disabled;
LSan cannot enumerate /proc tasks in this environment. No leak-free claim.
Native Windows/MinGW not tested.

The workspace run used CARGO_INCREMENTAL=0, CARGO_PROFILE_DEV_DEBUG=0 and
CARGO_PROFILE_TEST_DEBUG=0 after corrupted incremental object files. Debug
assertions/overflow checks stayed enabled; project build settings are unchanged.

Packaging: 121 cumulative changed/new files only, 17 differing from 0.93. ZIP
CRC, member contents and complete overlays over the uploaded source and 0.93
are verified. No source deletions. Defined 0.94 acceptance is closed (12/12,
0/12 remaining). Source BigRational matrices, precision expansion, dense
layouts, independent exact/Q witnesses and broad proof/mathematics remain open.
The rough whole-direction estimate remains 90–95% still to do, not a measured
readiness or correctness score.

## v0.95.0 — BigRational source matrices and independent Q witnesses

Added ordinary `std.matrix_bigq`: <=4-axis matrices over 1..1024-bit source
BigRational values, validation through new `bigrat_check`, strict budget
threading, arithmetic/transpose/selection, RREF/rank/kernel/image, determinant,
inverse, multiple-RHS systems and qmat-compatible text. Private augmented
systems are <=4x8; old scalar/matrix APIs and native representation persist.

Added separate exact Q rank/full-kernel certificates: A=C*R, L*A*U=I,
A*K=0, V*K=I. Checker uses only exact matrix identities, not elimination or
the producer. Source arithmetic and scalar algorithms are not thereby formally
proved. Exact inverse/solution residual APIs, bounded standalone .qproof codec,
export/replay and optional external matrix binding are implemented. New CLI
commands are `matrix-q-export` and `matrix-q-check`. No compiler builtin, IR,
Nat/Int, old proof format or proof-kernel authority changes.

Two closed matrix specifications retain legacy finite source evaluation.
SOURCE_BIGRATIONAL_MATRICES.md and Q_MATRIX_WITNESSES.md state scope, trusted
base, bounds and remaining work. Deferred applications stay deferred.
Validation: 677 workspace tests passed, 0 failed, 0 ignored; 18 tests added.
Linux x86-64, Rust 1.99.1, GCC 13.3.0; source/interpreter/host/C comparison,
81 small source matrices, 9 source and 50 host independent Fraction fixtures,
28 numerical certificate mutations, fixed hand-constructed transport fixture,
1024-bit source coefficients and encoding/decoding text caps. CLI replay after
source deletion and external mismatch/forgery rejection pass. Clippy/rustfmt
pass with existing warnings and none in new files. The native matrix example
passes AddressSanitizer/UBSan with detect_leaks=0; no leak-free claim.
Windows/MinGW not tested.

The workspace run used CARGO_INCREMENTAL=0, CARGO_PROFILE_DEV_DEBUG=0 and
CARGO_PROFILE_TEST_DEBUG=0 with debug assertions/overflow checks enabled;
project build settings were not changed.

Packaging: 134 cumulative changed/new files only, 27 differing from 0.94.
ZIP CRC, member bytes and full overlays over the uploaded source and 0.94
are verified; no source deletions. Defined 0.95 acceptance is closed (12/12,
0/12 remaining). The coarse whole-direction planning estimate remains 90–95%
still to do; it is not a measured readiness/correctness score. General proof,
resource/layout and deferred application obligations remain explicit.

## v0.96.0 — Exact scalar operations, numeric criterion N05

The active numeric-foundation section gains integer/rational powers, lcm and
integer square roots with exact remainders in host APIs and ordinary DLM/C.
Integer powers accept u128/Nat exponents; rational powers accept i128/Int,
including i128::MIN. The explicit empty-product convention is 0^0=1; negative
powers of zero and roots of negative integers are errors. Inputs are validated
before shortcuts, lcm divides before multiplying, and unused final power
squares are skipped. Successful source results carry the remaining budget;
internal compound calls do not reset it. EXACT_SCALAR_OPERATIONS.md specifies
domains, algorithms, profiles, errors, resources and trust.

Validation: 687 workspace tests passed, 0 failed, 0 ignored; 10 tests added.
2134 independent Python int/Fraction/lcm/isqrt vectors, 96 ordinary DLM/native-C
vectors, exhaustive root identities for 0..32768, 16384-bit host and 1024-bit
source cases, machine exponent extremes, work/precision/domain failures,
private helpers and CLI run/build. Fixture regeneration is byte-identical.
Linux x86-64, Rust 1.99.1, GCC 13.3.0; Windows/MinGW not tested. The new C
example passes AddressSanitizer/UBSan with leak detection disabled; no leak-free
claim is made. The workspace retains debug assertions and overflow checks;
environment-only CARGO_INCREMENTAL=0, CARGO_PROFILE_DEV_DEBUG=0 and
CARGO_PROFILE_TEST_DEBUG=0 avoid the previously observed artifact corruption.

N05 is closed in the existing precision profiles. The full numeric section has
4/16 criteria closed and 12/16 open, or 75% of checklist items remaining;
the count does not estimate effort. N04 source precision expansion, dense
layouts, real/interval/complex families, scalar witnesses and full platform
acceptance remain required. No later mathematical section is advanced, and no
compiler builtin, numeric tag, IR/ABI or kernel rule changes. Runtime results
do not gain StaticProof/optimizer authority.

Packaging: 141 cumulative changed/new files over the original uploaded source,
19 differing from 0.95.0 plus the section-order plan. CRC, extracted member
bytes and full overlays over the original and 0.95.0 are verified; no source
deletions. Existing old-profile contracts, format and API behavior are retained.

## v0.97.0 — Source/native exact precision through 16384 bits (N04)

Extended ordinary `std.exact` to the host precision ceiling. The <=1024-bit
profile retains 64 input words/work<=10^15; 1025..16384 bits allows 1024 words
and work<=u64::MAX. Default precision/work, Nat/Int, public signatures, numeric
tags, IR/ABI and kernel rules persist. The out-of-range precision test moves
from 1025 to 16385. Wide results remain budgeted Result computations.

Word-stream division, blocked Euclidean gcd, guarded nine-digit input,
16-character slash blocks, grouped output and word-fold roots cover public
and private bounds through 32769 bits/2049 words. Multiplication iterates the
shorter input and skips zero words; scaling/small division preserve canonical
form without redundant trimming. Immutable function AST sharing is an internal
interpreter dependency. Public Runtime/evaluation/recursion contracts persist.

Validation: 694 workspace tests passed, 0 failed, 0 ignored; 7 added. All ran
with release OPT_LEVEL=1, CODEGEN_UNITS=256, DEBUG=0, DEBUG_ASSERTIONS=true and
OVERFLOW_CHECKS=true via CARGO_PROFILE_RELEASE_*; CARGO_INCREMENTAL=0 and
CARGO_BUILD_JOBS=1. CARGO_TARGET_DIR was temporary tmpfs after overlayfs produced
empty release objects. Project Cargo profiles are unchanged. Command:
`cargo test --release --workspace --locked --offline`.

35 independent Python integer/Fraction cases compare every result word/sign
in interpreter/C, including 8192 root rounds and the >8192-step Fibonacci gcd.
Text tests reach 4933 output digits and 32771 input characters. Budget/domain/
privacy and CLI run/build pass. Maximum root/gcd C code passes ASan/UBSan with
detect_leaks=0; no leak-free claim. Linux x86-64, Rust 1.99.1, GCC 13.3.0;
Windows/MinGW not tested. Fixture regeneration is byte-identical.

N04 closes: 5/16 numeric criteria closed, 11/16 open, or 68.75% of checklist
items remaining. This is not an effort percentage. Dense layouts/performance,
new numerical families, scalar proofs and full platform acceptance remain open.
Runtime answers add no StaticProof/optimizer authority. Later mathematical
sections and deferred application tracks keep their waiting status.

## v0.98.0 — Exact complex numbers over Q (N06)

Added host `math::complex_q::ComplexQ` and ordinary `std.complex_q` on existing
exact rational components. Constructors, canonical checks, equality, negation,
add/sub/mul/div, reciprocal, conjugation, squared modulus and canonical display
are available. All fallible operations carry the caller's budget through every
step. Raw source records are revalidated, including the imaginary component
when real parts differ. Zero division reports an explicit domain error.

EXACT_COMPLEX_Q.md fixes the 1..16384-bit per-component numerator/denominator
profile and the requirement that each rational intermediate fit after its own
reduction. A later complex cancellation does not rescue an earlier precision
failure. No rounding, complex ordering or implicit loss of precision is added.
Source work remains explicit immutable scalar reservations; host retains limb
work counters. Full common resource/error categories are still N13.

Independent Python Fraction-pair fixtures cover 1059 cases; 179 selected cases
run in the interpreter and standalone C, including maximum numerators and
denominators, wide cancellation, reciprocal, norm and expected domain/precision
refusals. Expected source words/signs are produced independently and compared
without production complex/rational equality. Fixture regeneration is
byte-identical with Python 3.12.14. Separate tests cover 464 malformed/profile/
work refusals, 30 exact reservation/immutability assertions, 9 constructor/
text/sign/Int::MIN/MAX assertions, private helpers, host algebraic identities
and work thresholds, and CLI run/build. Eleven Rust tests were added.

All four generated C programs (682 assertions, including all 179 oracles) pass
ASan/UBSan with detect_leaks=0 and empty stderr; no leak-free claim is made.
Clippy passes with existing warnings; new Rust files add none. Linux x86-64,
Rust 1.99.1, GCC 13.3.0; Windows/MinGW not tested. Three closed module regression
theorems use existing legacy finite-evaluation certificates, not independent
general field-law proofs. Their erasure leaves these C files byte-identical.
No compiler builtin, numeric tag, IR/ABI or kernel rule changes. Runtime complex
values gain no new StaticProof/optimizer authority.

N06 closes: N01–N06 closed, N07–N16 open. This is 6/16 closed and 10/16 open,
or 62.5% of checklist items remaining, not an estimate of effort. Intervals
(N07) come next within numeric foundations. Later mathematical sections still
wait; dense data/performance, scalar witnesses and full platform acceptance
remain required. Positive geometry and huge-number application tracks retain
their deferred status.

Full acceptance: 705 workspace tests passed, 0 failed, 0 ignored (11 added).
Command: `cargo test --release --workspace --locked --offline -- --nocapture`.
The target directory was temporary tmpfs; CARGO_INCREMENTAL=0,
CARGO_BUILD_JOBS=1, CARGO_PROFILE_RELEASE_OPT_LEVEL=1, CODEGEN_UNITS=256,
DEBUG=0, DEBUG_ASSERTIONS=true, OVERFLOW_CHECKS=true (the last five are
CARGO_PROFILE_RELEASE_*). Debug assertions and overflow checks remain enabled;
project Cargo profiles are unchanged. Rustfmt checks for the new Rust files
pass. Existing 16384-bit root/Fibonacci-gcd, scalar, matrix, proof, module,
capability, error, runtime and compiler tests are included in the full run.

Packaging: 159 cumulative changed/new files over the original uploaded source,
20 differing from 0.97.0. CRC, unique names, every member byte, real extraction
and complete overlays over both the original source and 0.97.0 are verified;
no source deletions. The archive includes only changed/new files.

## v0.99.0 — Physical source identity (A/B)

Source FileIds and source tables are preserved through AST, resolution, checked
facts and executable IR. Physical parser/expression diagnostics keep original
imported file paths. Structural IR checks validate file identity, ranges and
origin consistency. SOURCE_IDENTITY.md records the bounded scope and remaining
full HIR/MIR and diagnostic obligations.

## v0.100.0 — Checked declaration identity (B)

Source DeclIds bind checked function contracts and record schemas to their AST
snapshots and contexts. Frontend facts carry nominal record types; lowering
requires completed, matching facts and the IR verifies declaration provenance.
DECLARATION_IDENTITY.md records APIs, negative cases and the trust boundary.
This closes the bounded declaration handoff. Full A/B/C and six-condition C-min
acceptance retain their separate open obligations.

## v0.100.1 — Complete cumulative source distribution

Date: 2026-10-30. This distribution contains all source files, libraries,
examples, fixtures, tools and documentation, assembled through 0.100.0 with the
separately supplied Q(i) extension integrated compatibly. Prior patches are
not needed when extracting the full archive to a fresh directory.

Existing math::complex_q::ComplexQ and source ComplexQ/complexq_* APIs retain
their declarations, display and tests. Additional math::complex::ComplexRational
and source BigComplex/bigc_* APIs include strict complex text, arithmetic,
1944 independent Fraction fixtures and 118 interpreter/native oracle cases.
The original CLI test target remains; the new example has its own test target.

The source separator scanner uses 16-character blocks for the existing
1..16384-bit profile. A combined-API regression checks a maximum-width component,
precision/work distinctions, parsing a common valid 65544-character input and
refusal one character above that bound in the interpreter and native C. Host
parsing checks the same maximum-length rational-pair form. Layer-specific
scalar text/work limits remain explicit in EXACT_COMPLEX.md.

The numeric count remains N01–N06 closed (6/16), N07–N16 open (10/16).
Source runtime, scalar algorithms, existing ComplexQ implementation, compiler
interfaces, kernel rules and native ABI retain their previous code/contracts.
The next engineering gate is C-min acceptance before D/E. The research program
R0–R6 is included as a deferred design document, with no research implementation.

Current cumulative acceptance: 746 workspace tests passed, 0 failed, 0 ignored
(9 added over 0.100.0). Command:
`cargo test --workspace --locked --offline --no-fail-fast`.
Environment: CARGO_INCREMENTAL=0, CARGO_BUILD_JOBS=2,
CARGO_PROFILE_TEST_OPT_LEVEL=2, DEBUG=0, CODEGEN_UNITS=256,
DEBUG_ASSERTIONS=true and OVERFLOW_CHECKS=true (the last five are
CARGO_PROFILE_TEST_*). The Cargo target directory is temporary tmpfs;
project build profiles are unchanged. Linux x86-64, Rust 1.99.1, GCC 13.3.0.

`cargo clippy --workspace --all-targets --locked --offline` completes without
errors, with 392 existing warnings; none references the additional Rust files.
Rustfmt checks for the additional Rust files pass. Regeneration of the 1944
Fraction fixtures is byte-identical. Existing wide root/Fibonacci-gcd,
matrices, numeric, proof, source-identity, declaration and compiler tests are
included in the full workspace run. Windows acceptance is not claimed here.

The full distribution is extracted into a new directory and successfully builds
with `cargo build --release --workspace --locked --offline`. This extracted
release compiler runs and builds both exact_complex_q.dlm and complex_q_demo.dlm;
the standalone native executables produce the same expected output as the
interpreter. No prior patch or source checkout is used by that build.

The complete archive contains 631 project files. Safe unique paths, ZIP CRC,
every member byte, and actual extraction against the complete source tree are
verified. Rust build output and the toolchain are not distribution sources.

## v0.101.0 — Приёмка C-min и рациональные интервалы (N07)

Дата: 2026-10-30. Основа — полный комплект 0.100.1.

### Этап C: приёмка C-min

Все шесть условий C-min сопоставлены с отдельными тестами (`c_min_acceptance.rs`, по одному на условие, и
`cli_c_min.rs`): проверка DAG без checker/runtime/поиска; области переменных, ссылки, корень и подмена цели/шага
после пересчёта fingerprint; аксиомы только из внешнего контекста; строгий формат, циклы и бюджеты; аудит
`kernel-replay`/`legacy-trusted`; граница гарантий (теорема о теле функции не закрывает машинное обязательство,
не меняет сгенерированный C и не убирает ошибку переполнения). Найдено и исправлено два недостатка:

* кодек принимал разные написания одного доказательства (пустые строки, лишние пробелы, CR, `+`, ведущие нули) —
  теперь допускается только канонический текст `to_text(from_text(t)) = t`;
* `dlm proof-check` возвращал код 1 и для неверного доказательства, и для исчерпания бюджета — теперь 1 Invalid,
  2 нечитаемый ввод, 3 UnknownWithinBudget.

Правила ядра `nat-semiring-v1` не изменены. C-min — Validated; полный C остаётся Open. Следующий инженерный
этап — D (владение и ресурсы).

### Числовая основа: N07

Добавлены `crates/dlm_core/src/math/interval_q.rs` (`IntervalQ`, `IntervalOrder`), `lib/std/interval_q.dlm`,
`docs/EXACT_INTERVALS.md`, `tools/generate_interval_q101_oracle.py`, `examples/valid/interval_q_demo.dlm`.
Операции: проверка порядка границ, принадлежность, вложение, hull, пересечение, сравнение (неопределённость при
перекрытии), ширина, середина, сложение, вычитание, отрицание, умножение, квадрат, модуль, обращение и деление
(отказ для интервала с нулём), квадратный корень с гарантированными границами и погрешностью не более `2^-k`,
строгий текст. Профиль <=16384 бит; бюджеты передаются явно. N07 закрыт: 7 из 16 критериев закрыты, 9 открыты.

### Тесты

```text
c_min_acceptance.rs  6 тестов, по одному на условие C-min
cli_c_min.rs         1 тест: проверка без исходника, пустой контекст аксиом, коды выхода 1/2/3
interval_q.rs        6 тестов: 5055 эталонов и 4559 границ точности в host, включение и точность на сетке,
                     гарантированные корни, текст и бюджеты; 233 эталона в DLM/native C (77 границ),
                     ошибки сырых записей, приватность
cli_interval_q.rs    2 теста: run и build примера
```

Пример интервалов проходит ASan/UBSan с проверкой утечек и даёт тот же вывод под Windows (C, собранный
MinGW-w64, запуск под Wine; не нативный Windows).

## v0.102.0 — Владение байтовыми буферами (D-min) и вычислимые вещественные числа (N08)

Дата: 2026-10-30. Основа — полный комплект 0.100.1 с изменениями 0.101.0.

### Этап D: D-min

Новый тип `ByteBuf` — изменяемый буфер байтов (0..255, не более 2^28) с единственным владельцем.
Операции: `buf_new`, `buf_from_list`, `buf_len` и `buf_get` (заимствование), `buf_set`, `buf_push`,
`buf_to_list`, `buf_free` (перемещение). Все они — эффект `runtime@byte_buffer` в контрактах функций;
функции-значения не могут работать с буферами. Новый проход `crate::ownership` (ошибка `E0943
OwnershipError`) проверяет все пути управления: использование после перемещения, двойное перемещение в одном
вызове и между соседними аргументами, перемещение в ленивом операнде `and`/`or`/`unwrap_or`, захват
замыканием, буферы в контейнерах, записях, типах `Fn`, аргументах типов, глобальных значениях и утверждениях,
неименованный буфер в позиции заимствования. Владелец, израсходованный только в одной ветви `if_then_else`,
освобождается в конце другой ветви.

План освобождения хранится в Typed Core (`released_params`, `released_lets`, `released_branches`); lowering
вставляет `__release`, верификатор IR проверяет его операнд и запрещает вложенный `ByteBuf` в типах узлов.
Интерпретатор хранит буфер как значение; C runtime держит данные вне регионов вызовов (в регионе — неизменяемый
заголовок), меняет байты на месте и освобождает живые буферы при завершении программы ошибкой исполнения.
`DLM_OWNERSHIP_TRACE=1` печатает число созданных буферов, освобождённых в области и освобождённых при выходе.
D-min — Validated; полный D (внешние дескрипторы и ошибки закрытия, отмена, заимствование частей, буферы
внутри структур данных) остаётся Open. Документ: `docs/OWNERSHIP.md`.

### Числовая основа: N08

Добавлены `crates/dlm_core/src/math/creal.rs` (`CReal`, `CNode`, `CRealOp`), `lib/std/creal.dlm`,
`docs/COMPUTABLE_REALS.md`, `tools/generate_creal102_oracle.py`, `examples/valid/creal_demo.dlm`. Число задаётся
DAG-программой до 64 узлов со строгой текстовой грамматикой; `approx(n)` даёт интервал ширины не более `2^-n`,
гарантированно содержащий число, по фиксированному расписанию уточнения на интервалах N07; ошибка области —
только если её доказывают границы, иначе UnknownWithinBudget; сравнение возвращает «не решено» (2) при
перекрытии. N08 закрыт: 8 из 16 критериев закрыты, 8 открыты.

### Тесты

```text
ownership.rs              4 теста: 9 программ interpreter/C, 11 буферов освобождены в областях, число
                          вставленных освобождений; 6 программ с ошибкой исполнения (одинаковые сообщения,
                          очистка при выходе); 26 отрицательных сценариев lifetime; код и позиция E0943
creal.rs                  7 тестов: 343 эталона и 340 границ точности в host, включение/ширина/уточнение,
                          ошибки области, неразрешённые сравнения, грамматика; 88 эталонов в DLM/native C
                          (30 границ), сырые записи, бюджеты, приватность, round-trip текста host/source
cli_creal_ownership.rs    3 теста: run и build обоих примеров (0 буферов при выходе), dlm check с E0943
```
ir_verify.rs              типовые правила IR для восьми операций буфера и `__release`
```

Полный прогон `cargo test --workspace --locked --offline --no-fail-fast` (Linux x86-64): 775 тестов, 0 ошибок.
Программы владения (обычный путь и путь с ошибкой исполнения), оба примера и все 118 source-векторов
вычислимых вещественных проходят ASan/UBSan/LSan без утечек и дают тот же вывод под Windows (C, собранный
MinGW-w64, запуск под Wine; не нативный Windows); на пути с ошибкой живые буферы освобождаются при выходе.

## v0.103.0 — Заимствование буферов в вызовах и комплексные числа с вычислимыми компонентами (N09)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.102.

* D: параметры `&ByteBuf` (заимствование на время вызова, без перемещения и освобождения в вызываемой
  функции), запрет одного буфера как заимствованного и перемещаемого аргумента одного вызова, операции
  `buf_copy`, `buf_slice`, `buf_append`, `buf_eq`, `buf_from_text`, `buf_to_text`; проверка IR для заимствованных
  слотов. Пример `byte_buffer_demo.dlm` использует `&ByteBuf`.
* N09: `math/ccomplex.rs`, `CRealBuilder`, `lib/std/ccomplex.dlm`, `tools/generate_ccomplex103_oracle.py`
  (генератор `creal102` теперь импортируемый, фикстуры не изменились), `examples/valid/ccomplex_demo.dlm`.
  N09 закрыт: 9/16.
* Тесты: `ownership.rs` (6), `ccomplex.rs` (4), `ir_verify.rs` (+15 правил), `cli_creal_ownership.rs` (4).
  ASan/UBSan/LSan и MinGW-w64 + Wine для новых программ, примера и 130 source-векторов N09.
* Полный прогон `cargo test --workspace --locked --offline --no-fail-fast` на наложении на 0.100.1: 782 теста, 0 ошибок.

## v0.104.0 — Элементарные функции (N10) и TextBuf

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.103.

* N10: `math/elementary.rs`, узлы `exp`, `log`, `sin`, `cos`, `pi` в `math::creal` и `std.creal`, генератор
  `tools/generate_elementary104_oracle.py` (эталонный алгоритм + mpmath), `examples/valid/elementary_demo.dlm`.
  N10 закрыт: 10/16.
* D: `TextBuf` (`tbuf_new`, `tbuf_push`, `tbuf_len`, `tbuf_finish`), `&TextBuf`; общий признак ресурса.
* Производительность: история паспортов ограничена (`HISTORY_LIMIT` = 1024, проверка `std.creal` 4.2 → 0.19 с),
  без клонирования `CheckedFn` при вызове; `std.exact` пропускает НОД для двоично-рациональных значений.
* Тесты: `elementary.rs` (3), `ownership.rs` (+1), `ir_verify.rs` (+4 правила), `cli_creal_ownership.rs` (+1);
  ASan/UBSan/LSan и MinGW-w64 + Wine для примера, программ TextBuf и 32 source-векторов N10.
* Полный прогон `cargo test --workspace --locked --offline --no-fail-fast` на наложении на 0.100.1: 787 тестов, 0 ошибок.

## v0.105.0 — Числовые преобразования (N11) и переносимый формат (N12)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.104.

* N11: `math/convert.rs`, `lib/std/convert.dlm` — режимы floor/ceil/trunc/half_even, `cv_round`, `cv_dyadic`,
  `cv_decimal`, `cv_decimal_exact`, `cv_integer_exact`, `cv_parse_decimal`, `cv_to_nat`, `cv_to_int`,
  `cv_creal_round`, `cv_creal_decimal` (решение по включениям, иначе `UnknownWithinBudget`).
* N12: `math/portable.rs` — `dlm1:<семейство>:<текст>` для int/rat/ivq/cq/creal/ccomplex, строгий разбор,
  отказы версии/семейства/неканоничности; в DLM `pn_*`, `pn_check`, `pn_category`. N11, N12 закрыты: 12/16.
* `Integer::from_parts`; генератор `tools/generate_convert105_oracle.py` (Fraction + mpmath), 1876 эталонов;
  `examples/valid/convert_demo.dlm`; `tools/__pycache__` убран из дерева, добавлен в `.gitignore`.
* Тесты: `convert.rs` (4: host, диапазоны, 234 DLM/C-вектора, машинные целые и пределы), `cli_creal_ownership.rs` (+1);
  ASan/UBSan/LSan и MinGW-w64 + Wine для 234-векторной программы, программы пределов и примера — вывод совпадает.
* Полный прогон `cargo test --workspace --locked --offline --no-fail-fast` на наложении на 0.100.1: 792 теста, 0 ошибок.

## v0.106.0 — Контракты ошибок и бюджета (N13), скалярные свидетельства (N15)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.105.

* N13: `math/contract.rs`, `lib/std/numeric.dlm` — категории domain/unrepresentable/invalid_input/limit/budget/undecided,
  каталог `MESSAGES` всех отказов host и source, `metered`, `chain`, `required_work`, `check_threshold`; `nm_*`.
  `pn_category` в `std.convert` больше не выдаёт бюджетную ошибку за `invalid` (возвращает `budget`).
* N15: `math/scalar_witness.rs`, `lib/std/scalar_witness.dlm` — `dlmw1|kind|...|digest`, виды quotient, round, decimal,
  sqrt (checked) и enclosure (replayed), производители и проверка, одинаковые сообщения отказов.
* Тесты: `numeric_contract.rs` (4), `scalar_witness.rs` (2). N13, N15 закрыты: 14/16.
* Проверены только целевые тесты (`numeric_contract`, `scalar_witness`, `example_corpus`, release); полный прогон
  workspace, ASan и Wine для этой версии не выполнялись.

## v0.107.0 — Плотные числа и профиль затрат (N14)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.106.

* Новый обычный тип `BigNat` (32-битные limbs, до 2^20 бит, копируемый): `crate::dense_nat` в интерпретаторе,
  `DV_BIGNAT`/`dvn_*` в нативном C (те же алгоритмы, Knuth D), 18 builtins `bn_*` в checker/IR verify/lowering,
  `DLM_DENSE_TRACE`.
* `std.exact`: сложение, вычитание, умножение, деление, сравнение, десятичный ввод/вывод и корень идут через
  плотное ядро; значения, отказы и единицы бюджета прежние (весь release-прогон `dlm_core` — 751 тест — прошёл).
* `std.dense` (`DInt` с `BigNat`), `math::profile` (7 нагрузок, пороги работы и памяти). N14 закрыт: 15/16.
* Тесты: `dense_profile.rs` (2), `dense_nat` (2 unit), `ir_verify.rs` (+18 образцов). Полный debug-прогон workspace,
  ASan и Wine не выполнялись.

## v0.108.0 — Приёмка числового раздела (N16)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.107.

* `math/acceptance.rs` — карта N01–N16 → тесты, фикстуры, модули, документы; `build_c_executable_with_flags`.
* `tests/numeric_acceptance.rs` (3): доказательства существуют, у каждой фикстуры есть генератор и читающий тест;
  41 сквозной случай по всем семействам совпадает с host (значение или категория отказа) в интерпретаторе, C,
  ASan/UBSan/LSan и MinGW-w64 + Wine (с `DLM_REQUIRE_*=1` проверено здесь); `std.complex_q` отдельно.
* `std.dense` теперь расширяет `StdScalarWitness` (одна программа видит все семейства).
* `tools/numeric_acceptance.sh`, `tools/numeric_acceptance.ps1`. Числовой раздел: 16/16.

## v0.109.0 — Файловые дескрипторы (этап D)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.108.

* Тип `File` (линейный ресурс): `file_open`, `file_ok`, `file_error`, `file_write`, `file_write_bytes`,
  `file_read_line`, `file_read_all`, `file_read_bytes`, `file_flush`, `file_close`, `file_remove`; `&File`.
* Проверка владения: файл нельзя отпустить неявно (не закрыт / закрыт на одной ветви — E0943).
* Ошибки — «липкие» значения дескриптора, `file_close` их сообщает; интерпретатор (`FileSlot`) и C (`dv_file`)
  дают одинаковые тексты; открытые после ошибки исполнения дескрипторы закрываются при выходе (`DLM_FILE_TRACE`).
* Тесты: `files.rs` (3), `ir_verify.rs` (+11 образцов). Полный прогон не выполнялся.

## v0.110.0 — Обобщения (этап E-min)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.109.

* `fn f<T>(...)`, `record R<T>(...)`, явные аргументы `f<Nat>(x)`, `R<Text>(...)`, типы `R<List<Nat>>`.
* `crate::generics`: кодирование имён экземпляров (`mangle`/`demangle`), разбор `<...>` в выражениях,
  мономорфизация при построении `Program` (`Program::generic_diagnostics` → checker); `OrdinaryType` для `R<...>`.
* `lib/std/collections.dlm` — обобщённые списки, стек, очередь, ассоциативный массив.
* Тесты: `generics.rs` (5). Полный прогон не выполнялся.

## v0.111.0 — Вывод аргументов типов и ограничения (этап E)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.110.

* `generics::infer_type_arguments`: вызовы шаблонов без `<...>` получают аргументы из типов аргументов
  (локальный вывод и унификация); иначе понятная ошибка.
* Ограничения `T: Eq`, `T: Ord` (проверка на экземпляре); builtins `equal`, `less` (checker, IR verify,
  интерпретатор, C `dv_equal`).
* Экземпляр с записями, невидимыми теории шаблона, размещается в запрашивающей теории.
* `std.collections`: функции с ограничениями. Тесты: `generics.rs` (+2), `ir_verify` (+2 образца).

## v0.112.0 — Классы типов (этап E)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.111.

* `class Name<T> { fn m(...) -> R }`, `instance Name<Type> { ... }`, `instance<A: C> Name<List<A>> { ... }`
  (`ClassInfo` в `FnDecl`, блоки в парсере); вызовы методов разрешаются по выведенному типу аргумента.
* Ограничения-классы `T: Show + Size` проверяются на экземпляре; такие шаблоны инстанцируются в
  запрашивающей теории; сгенерированные экземпляры ставятся после своих зависимостей.
* Проверки экземпляров: неизвестный класс, пропущенные/лишние методы, типы методов, дубли, перекрытия, отсутствие.
* `lib/std/classes.dlm` (Show, Monoid). Тесты: `type_classes.rs` (3). Полный прогон не выполнялся.

## v0.113.0 — Суперклассы и методы по умолчанию (этап E)

Дата: 2026-10-30. Основа — 0.100.1 с изменениями 0.101–0.112.

* `class C<T>: S1 + S2 { ... }`: экземпляр требует экземпляров суперклассов (транзитивно, в т.ч. через обобщённые).
* Метод класса с телом — метод по умолчанию (`apply_defaults`), экземпляр может его переопределить.
* `std.classes`: `Semigroup` (+ `mtimes` по умолчанию), `Monoid: Semigroup`.
* Диагностики владения называют тип ресурса (`File`, `TextBuf`, `ByteBuf`).
* Тесты: `type_classes.rs` (+2). Полный прогон не выполнялся.

## v0.114.0 — Машинные вещественные числа и численные методы

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.113.

* Типы `Float` (IEEE-754 binary64) и `FloatVec`; литералы `1.5`, `-0.25`; ~70 встроенных `fl_*`/`fv_*` в проверке, интерпретаторе, IR и C.
* Линейная алгебра на FloatVec: `fv_matmul`, `fv_transpose`, `fv_solve` (Гаусс с частичным выбором).
* `std.numerics`: корни (бисекция, Ньютон, секущие), производная, трапеции/Симпсон, RK4 (скаляр/вектор), золотое сечение, градиентный спуск, интерполяция, Горнер, статистика, линейная регрессия, ГПСЧ (LCG, равномерное, нормальное).
* C: `-ffp-contract=off`, `-lm`.
* Тесты: `floats.rs` (4), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.115.0 — Графы, геометрия, вероятности

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.114.

* Тип `NatVec` и 18 встроенных `nv_*`; 11 нативных графовых алгоритмов `gr_*` (BFS, DFS, топосортировка, компоненты, SCC, двудольность, Дейкстра, Беллман–Форд, Флойд, Краскал, максимальный поток) в интерпретаторе и C.
* `std.graphs`: граф на рёбрах, кратчайший путь с восстановлением, MST, поток.
* `std.geometry`: 2D/3D векторы, предикаты, пересечения, многоугольники, выпуклая оболочка, аффинные преобразования, 4×4, лучи.
* `std.probability`: специальные функции, распределения, выборки, квантили, гистограммы, t/Уэлч/χ²-тесты.
* Тесты: `graphs.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.116.0 — Численная линейная алгебра

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.115.

* 16 нативных `la_*`: LU с выбором, определитель, обратная, Холецкий, QR Хаусхолдера, МНК, собственные значения/векторы (Якоби), сингулярные числа, ранг, CSR-умножение, сопряжённые градиенты. Интерпретатор и C совпадают побитово.
* `std.linalg`: `Matrix`, разложения, число обусловленности, степень, степенной метод, CSR из триплетов, лапласиан.
* Тесты: `linalg.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.117.0 — Анализ и оптимизация

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.116.

* Нативные `fft_forward/inverse/convolve`, `lp_solve/lp_status` (двухфазный симплекс) в интерпретаторе и C.
* `std.analysis`: автодифференцирование (`Dual`), градиенты/якобианы, Гаусс–Лежандр, адаптивный Симпсон, Дорманд–Принс, сплайны, многочлены и их комплексные корни, спектр мощности.
* `std.optimize`: Нелдер–Мид, BFGS, Левенберг–Марквардт, линейное программирование.
* Тесты: `analysis.rs` (2), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.118.0 — Теория чисел, конечные поля, группы перестановок

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.117.

* 31 нативная функция: модульная арифметика на u128, Миллер–Рабин, разложение (Брент–Поллард), КТО, Тонелли–Шенкс, решето; многочлены над F_p и тест неприводимости Рабина; перестановки и порядок группы (Шрайер–Симс).
* `std.number_theory`: Z/mZ, функции Мёбиуса/σ, первообразный корень, дискретный логарифм, RSA, GF(p^k), классические группы.
* Тесты: `number.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.119.0 — SAT, гомологии над Z, персистентность

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.118.

* Нативные `sat_solve/sat_status` (CDCL), `tp_homology` (нормальная форма Смита), `tp_rips` (баркоды Рипса) в интерпретаторе и C.
* `std.logic`: CNF, кардинальные ограничения, Цейтин, n ферзей, судоку, раскраска графов, принцип Дирихле.
* `std.topology`: гомологии с кручением, числа Бетти, эйлерова характеристика, сферы, тор, RP², персистентность.
* Тесты: `logic_topology.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.120.0 — Базисы Грёбнера, категория FinSet

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.119.

* Нативные `mv_add/sub/mul/normalize`, `gb_basis` (приведённый базис Грёбнера; lex/grlex/grevlex), `gb_reduce` в интерпретаторе и C.
* `std.ideals`: многочлены `MPoly`, принадлежность идеалу, единичный идеал (несовместность системы), исключение переменных.
* `std.categories`: FinSet — композиция, образы, (ко)произведения, (ко)уравнители, пулбэки, пушауты, характеристические отображения.
* Тесты: `groebner.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.121.0 — Сертификаты SAT, подсчёт моделей, целочисленное программирование

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.120.

* `sat_proof` (выученные клаузы CDCL как сертификат), `sat_check` (независимая RUP-проверка), `sat_count` (точный #SAT), `ilp_solve/ilp_status` (ветви и границы) в интерпретаторе и C.
* `std.logic`: `refutation`, `verified_unsat`, `model_count`, `entails`; `std.optimize`: `integer_program`, `knapsack`.
* Тесты: `proofs_ilp.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.122.0 — Упорядоченные словари

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.121.

* Типы `NatMap` и `TextMap`, 28 встроенных `km_*`/`tm_*` (вставка, поиск, удаление, floor/ceil, диапазоны, слияние, счётчики, подсчёт слов) в проверке, интерпретаторе, IR и C.
* `std.maps`: обход, фильтр, свёртка, обращение, сложение счётчиков, частоты, `most_common`.
* Тесты: `maps.rs` (4), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.123.0 — Разреженные решатели, надёжная геометрия, сигналы, статистика

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.122.

* 16 нативных функций: PCG (Якоби), BiCGSTAB, GMRES(m); точные orient2d/incircle/orient3d; триангуляция Делоне (флипы Лоусона); k-d деревья (kNN, радиус); 2-D FFT, STFT, DWT/IDWT (Haar, D4), IIR; SHA-256. Интерпретатор и C совпадают.
* `std.signal`, `std.statistics` (многомерная статистика, PCA, байесовские обновления, MCMC, AR), `std.spatial` (сетки, проверка Делоне); `std.linalg`: обёртки решателей, 2-D лапласиан, масштабирование строк.
* Тесты: `kernels.rs` (5), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.124.0 — Крупные простые, BPSW, BVH

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.123.

* `nt_*`: 256-битное произведение и шаг Кнута в `mulmod`, BPSW в `nt_is_prime`, расширенный бюджет ρ.
* BigNat: `bn_powmod`, `bn_modinv`, `bn_is_prime` (BPSW), `bn_next_prime`, `bn_random`, `bn_to_hex`, `bn_from_hex`.
* `bvh_raycast`, `bvh_winding`, `bvh_pairs`; `std.spatial`: площадь/объём/точка в сетке; `std.bigprime`: RSA на BigNat, Лукас–Лемер, CRT.
* Тесты: `bignum_bvh.rs` (3), `ir_verify` (сэмплы). Полный прогон не выполнялся.

## v0.125.0 — Циклы, перечисления, файловая система

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.124.

* `fn f(...) -> T loop`: хвостовые циклы без меры (partial) в постоянном стеке; `dlm run --interactive` (живой stdin, потоковый вывод).
* `enum Name { A(x: T), B }`: конструкторы, образцы `A(x)`/`B` в `match`, статическая проверка полноты.
* `file_seek`, `file_tell`, `file_size`, `file_truncate`, `file_set_buffered`, `file_exists`, `file_is_dir`, `file_size_of`, `file_mtime`, `file_list_dir`, `file_walk`, `file_make_dir`, `file_remove_dir`, `file_rename`, `file_copy`, `file_write_atomic`; ошибки с кодом ОС.
* `std.files`. Тесты: `apps.rs` (4), `ir_verify` (сэмплы).

## v0.126.0 — Рекурсивные данные, процессы, сеть

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.125.

* Рекурсивные записи (через Option/List/...), рекурсивные и обобщённые `enum`, мера `decreasing` по записи.
* `proc_run`, `proc_spawn`, `proc_poll`, `proc_wait`, `proc_kill`, `proc_env`; `net_resolve`, `net_listen`, `net_local_port`, `net_accept`, `net_connect`, `net_send`, `net_recv_line`, `net_recv_all`, `net_shutdown`, `net_close`.
* `std.process`, `std.net` (HTTP/1.0 клиент и последовательный сервер). Тесты: `os_records.rs` (5).

## v0.127.0 — Собственный GUI, этап G1

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.126.

* План GUI G1–G4 (`docs/GUI.md`).
* `gx_new`, `gx_width`, `gx_height`, `gx_get`, `gx_fill_rect`, `gx_line`, `gx_circle`, `gx_text`, `gx_text_width`, `gx_blit`, `gx_checksum`, `file_write_png`.
* `std.ui`: виджеты, раскладка, отрисовка, hit-test, модель/вид/обновление, HTML-бэкенд и сервер приложения.
* Мера по записи: рекурсия в элементы списка-поля через `map`/`fold`; типы-аргументы `List<...>` в enum.
* Пример `examples/valid/gui_counter.dlm`. Тесты: `gui.rs` (3).

## v0.128.0 — Векторный UI и нативные окна

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.127.

* `vx_render`, `vx_render_region`, `vx_bounds`, `vx_svg`, `vx_text`, `vx_text_width`: векторные сцены с сглаживанием, градиентами, клипами, растром, текстом.
* Кириллица в шрифте (`gx_text`, векторный текст).
* `win_open`, `win_present`, `win_poll`, `win_size`, `win_close`: X11 (интерпретатор и C), Win32 (C).
* `std.vector`: векторный вид виджетов с состояниями, HiDPI, SVG, инкрементальная перерисовка, `check_ui`, `run_window`, `serve_vector_app`.
* Тесты: `gui_vector.rs` (4).

## v0.129.0 — Текст всех письменностей, списки и прокрутка

Дата: 2026-10-31. Основа — 0.100.1 с изменениями 0.101–0.128.

* `file_font_load`, `file_font_find`, `vx_text_fonts`, `vx_font_width/has/advance/metrics`, `vx_codepoints`, `vx_from_codepoints`; команда сцены 9.
* Упрощённый bidi, арабские формы; `\u{...}`, `\x..` в текстовых литералах.
* `Scroll`, `ListView`, `Table`; `Select`, `Wheel`; `*_f`-варианты со шрифтами.
* Окна: XIM/UTF-8 ввод, суррогатные пары Win32, колесо (событие 8), коды клавиш без пересечения с Unicode.
* Исправлено: имена `near`/`far` в C ломали сборку под Windows (макросы windows.h).
* Тесты: `gui_text.rs` (5).


## v0.130.0 — Явные эффекты функций и прикладные обработчики

Дата: 2026-11-01. Этапы A/B, E, F/G. Накопительная основа — исправленная 0.129.0.

* `FnEffect<Bounds, ..., R>` и проверяемый `effect_fn`: эффекты/Partial сохраняются
  через параметры, результаты, записи, коллекции и лексические замыкания.
* `Callable`, `UnaryCallable`, `NullaryCallable`, `PureCallable`; встроенные
  `ArgOf`, `ResultOf`, `ItemOf`, `OkOf`, `ErrorOf` после generic-подстановки.
* Девять итеративных effect-traversal builtins; `std.effects`, короткое замыкание
  предикатов и первый Result-error без лишних вызовов.
* `std.files`: each_line, try_each_line, transform_text, try_transform_text;
  `std.net`: обобщённый HTTP-handler с файловым I/O и журналированием.
* Единый лексический анализ captures; независимые проверки эффектов/Partial в IR.
* Контракт: `docs/EFFECT_FUNCTIONS.md`. Тесты: `effect_functions.rs`,
  `effect_library.rs`, полный набор mutation-проверок IR. Примеры:
  `effect_functions_demo.dlm`, `effect_file_transform.dlm`.
* Открыты пользовательские ассоциированные типы, HKT, общие владеющие типы,
  изменяемые коллекции/заимствования, async/задачи/отмена и полная верификация.
  Recursive Discovery R0–R6 учтён и отложен.

## v0.131.0 — Аппаратное ускорение отрисовки (GPU)

Дата: 2026-11-01. Основа — 0.130.0.

* `win_gpu`, `win_present_scene`, `win_read`: отрисовка векторных сцен через OpenGL (GLX/WGL, загрузка во время выполнения, MSAA через FBO).
* Интерпретатор (Linux) и нативный C (Linux, Windows) используют одинаковый список отрисовки.
* Программный путь сохранён как запасной.
* Тесты: `gui_gpu.rs` (2).
