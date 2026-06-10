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
