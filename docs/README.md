> **Current guides for DLM v0.131.0 / Актуальные руководства:** [Русский](LANGUAGE_GUIDE_RU.md) · [English](LANGUAGE_GUIDE_EN.md). The historical version notes below are retained for compatibility and research context.

# DLM / ЯРД — Implementation Documentation Pack v1.0

**Актуальная навигация для исходников v0.100.1:** основной инженерный план —
[`ROADMAP.md`](../ROADMAP.md), реализация и открытые обязательства —
[`LAW_STATUS.md`](LAW_STATUS.md), числовой раздел —
[`MATHEMATICS_PROGRESS.md`](MATHEMATICS_PROGRESS.md). Пометка `v1.0` в
названии этого документа относится к исторической спецификации MVP и не
означает, что универсальный DLM v1.0 выпущен.

v0.99: [`SOURCE_IDENTITY.md`](SOURCE_IDENTITY.md) — идентичность исходных файлов,
физические диагностики и проверка происхождения узлов IR (A/B).

v0.100: [`DECLARATION_IDENTITY.md`](DECLARATION_IDENTITY.md) — проверенные
контракты/схемы по ID объявлений и номинальные типы из frontend (B).

v0.100.1: [`CUMULATIVE_BUILD.md`](CUMULATIVE_BUILD.md) — полный исходный комплект
и совместимое объединение двух Q(i) API; [`EXACT_COMPLEX.md`](EXACT_COMPLEX.md) —
дополнительный текстовый интерфейс.

[`RECURSIVE_DISCOVERY_RESEARCH_PROGRAM.md`](RECURSIVE_DISCOVERY_RESEARCH_PROGRAM.md) —
исследовательский план R0–R6, реализация отложена.

Этот пакет — рабочий комплект документов для поэтапной реализации языка DLM/ЯРД.

DLM/ЯРД — язык программирования и формальной математики, в котором любое значение имеет не только тип, но и паспорт: способ задания, capabilities, cost, trust, provenance, validation, universe, equality и theory context.

Главный закон языка:

```text
Операция разрешена не потому, что тип подходит,
а потому что паспорт значения содержит нужную capability,
уровень trust достаточен для текущего режима компиляции,
а объект находится в допустимом TheoryContext.
```

## Порядок чтения и реализации

1. `SPEC.md` — полная спецификация синтаксиса и семантических принципов.
2. `GRAMMAR.md` — EBNF-грамматика `.dlm`.
3. `AST_IR.md` — структуры AST, HIR, TypedIR, PassportIR.
4. `PASSPORT_LATTICE.md` — решётки паспортов, partial order, join/meet, capability rules.
5. `COMPILER_PASSES.md` — пайплайн компилятора.
6. `DIAGNOSTICS.md` — коды ошибок и формат диагностик.
7. `STD_CORE.md` — минимальная стандартная библиотека.
8. `TEST_MATRIX.md` — валидные и невалидные тестовые программы.
9. `MVP_ACCEPTANCE.md` — критерии готовности MVP.

## Статус

Это не финальная академическая спецификация языка на годы вперёд, а инженерная спецификация MVP-1.0. Она намеренно ограничивает ряд сложных возможностей: пользовательские passport transformers, полноценный dependent proof checker, automatic soundness/reflection bridges и SMT-проверку монотонности.

Цель MVP — построить рабочий компилятор-проверяльщик, который умеет:

- парсить `.dlm`;
- строить module/theory/bridge graph;
- выводить паспорта литералов и простых выражений;
- проверять capabilities;
- разделять StaticProof и RuntimeWitness;
- отслеживать Trust taint;
- запрещать невалидированный внешний ввод;
- запрещать межтеоретические переходы без TheoryBridge;
- выдавать качественные диагностики.


## v0.22 Universe Levels

Added the first mathematical universe hierarchy layer:

- explicit `U0()`, `U1()`, `U2()` constructors;
- `Set<U n -> U n+1>` as a level-raising object;
- `Class<U n>` as a meta-level view;
- `UniverseLevelError` for bare universes and set-of-all-sets style mistakes;
- `HistoryChain` events for universe, set and class formation.

See `UNIVERSE_HIERARCHY.md`.

- [Metatheory Dependencies](METATHEORY_DEPENDENCIES.md)

- [METATHEORY_CLOSURE.md](METATHEORY_CLOSURE.md) — metatheory closure report foundation.

- `CONSERVATIVE_EXTENSION.md` — conservative extension audit foundation.

- `THEOREM_DEPENDENCY_GRAPH.md` — global metatheory inventory and theorem dependency graph foundation.

- [SOUNDNESS_BOUNDARY_LEDGER.md](SOUNDNESS_BOUNDARY_LEDGER.md) — explicit ledger for soundness/reflection/consistency/oracle/unsafe boundaries.
- [HIGH_PERFORMANCE_COMPILATION.md](HIGH_PERFORMANCE_COMPILATION.md) — future DLM-Fast/native compilation, proof-erasure, passport-erasure, LLVM/MLIR backend and benchmark strategy.

- `TRUSTED_BASE_CLOSURE.md` — final metatheory-foundation gate combining registry, dependency, closure, inventory and soundness-boundary evidence.

- `METATHEORY_FOUNDATION_EXIT.md` — final metatheory foundation exit checklist and phase-2 readiness gate.


- `STAGE_READINESS_MODEL.md` — local/architectural/fundamental readiness model for controlling stage transitions.

- `LOGIC_QUANTIFIERS.md` — logical connectives and quantifier-object MVP for ordinary mathematics.


- `SUBSTITUTION_ALPHA.md` — variable scope, alpha-equivalence and capture-avoiding substitution foundation.

<!-- RUNTIME_HARDWARE_LAYERING_PRINCIPLE_BLOCK -->
### `RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md`

Defines the four-layer separation between source semantics, compiler IR, runtime control and hardware execution. It explains why passports should guide CPU/compiler scheduling rather than becoming GPU payload.

<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->
### `DLM_ARCHITECTURAL_LAWS.md`

The architectural constitution of DLM / ЯРД. It defines 43 numbered sections (laws 1–40 plus review, readiness and final principles 41–43) covering semantic layer separation, passport-governed operations, proof erasure, compact runtime data, bridge preservation contracts, trust monotonicity, checker passes, ID-based resolution, Span preservation, batch-first GPU execution, verified optimization, visible trusted base, explainability, AI-agent-friendly development, minimal proof kernel, and honest status downgrade.

<!-- V0_57_FUNCTION_LAMBDA_APPLICATION_BLOCK -->
### `FUNCTION_LAMBDA_APPLICATION.md`

Documents the v0.57 ordinary function foundation: function types, lambda terms, application reports, domain/codomain checking, lambda captures, application status, trust preservation and the boundary that function application is not theorem proving.


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

- `SEQUENCE_TYPES.md` — finite List/Sequence foundation, explicit indexing boundary, no hidden null/exception/infinite stream.

- `TRAVERSAL_BOUNDARY.md` — finite map/fold traversal boundary with explicit fuel and FunctionContract requirements.

- `RECURSION_BOUNDARY.md` — explicit recursion schemes, well-founded measures and fuel boundaries.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.


## v0.66.0 — Standard Algebraic Prelude Foundation

The project now has a checked algebraic prelude boundary for Nat/Bool/Option/Result/List/Sequence operations. Standard prelude operations are explicit contracts with signatures, budgets, taint propagation and stable fingerprints.


## v0.67 — Prelude Evaluation

See `PRELUDE_EVALUATION.md` for the small-step algebraic evaluator for verified standard prelude contracts.

## v0.68.0 — Prelude Lowering / Verified Erasure Boundary

Adds `PRELUDE_LOWERING.md` and `crates/dlm_core/src/prelude_lowering.rs`.

This layer turns a verified `PreludeEvaluationReport` into an explicit `PreludeLoweringReport` for audit-only, interpreter, native-scalar, native-vector, GPU-batch, or remote-batch targets. It verifies erasure boundaries without producing hidden runtime/compiler magic.

- `BACKEND_CAPABILITY.md` — backend capability and lowering-target contract layer for scalar/vector/GPU/remote lowering.


- `BACKEND_LAYOUT.md` — backend ABI/layout descriptor boundary for scalar/vector/GPU/remote lowering artifacts.

- `DENSE_RUNTIME_DESCRIPTOR.md` — dense runtime descriptor boundary for compact value/region payloads after backend layout validation.

- `RECORD_SCHEMAS.md` — checked nominal record schemas in executable IR (v0.88, stage B).
- `EQUATIONAL_REPLAY.md` — structured equational evidence and independent proof replay (v0.88, stage C foundation).

- `FINITE_MATHEMATICS.md` — bounded exact F2 matrices, homology, chain complexes and finite categories (v0.89).

- `MATH_SOURCE_AND_WITNESSES.md` — ordinary DLM matrix/homology libraries, Result patterns, dominance guards and independent finite F2 witnesses (v0.90).

- `FINITE_ALGEBRA.md` — bounded finite groups/rings/fields/modules, maps, quotients and source APIs (v0.91).
- `MATHEMATICS_PROGRESS.md` — per-patch acceptance scope and remaining mathematical work.

- `NUMERIC_FIELDS.md` — bounded exact rationals, oriented prime-field matrices and homology (v0.92).

- `EXACT_RATIONAL_MATRICES.md` — explicit precision/work budgets, exact host integers/rationals, Q matrices and bounded source/C libraries (v0.93).
- `FUTURE_MATH_APPLICATIONS.md` — deferred positive geometries and formal symbolic huge-number research.

- `SOURCE_EXACT_ARITHMETIC.md` — ordinary source multiprecision integers/rationals, explicit source budgets and interpreter/native-C parity (v0.94).

- `SOURCE_BIGRATIONAL_MATRICES.md` — ordinary budgeted BigRational matrices, systems and source/native-C parity (v0.95).
- `Q_MATRIX_WITNESSES.md` — standalone algorithm-independent Q rank/full-kernel checking and residual APIs (v0.95).

- `EXACT_SCALAR_OPERATIONS.md` — exact integer/rational powers, lcm and integer square roots in host/DLM/C (v0.96, numeric criterion N05).

- `SOURCE_EXACT_PRECISION.md` — 16384-bit source/native precision, bounded block algorithms and compatible low-profile limits (v0.97, N04).

- `EXACT_COMPLEX_Q.md` — exact rational-pair complex arithmetic, squared modulus, validation and shared budgets (v0.98, N06).
