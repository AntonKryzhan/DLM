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
