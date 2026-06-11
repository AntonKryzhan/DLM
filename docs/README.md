# DLM / ЯРД — Implementation Documentation Pack v1.0

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

The architectural constitution of DLM / ЯРД. It defines 25 global laws covering semantic layer separation, passport-governed operations, proof erasure, compact runtime data, bridge preservation contracts, trust monotonicity, checker passes, ID-based resolution, Span preservation, batch-first GPU execution, verified optimization, visible trusted base, explainability, AI-agent-friendly development, minimal proof kernel, and honest status downgrade.

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
