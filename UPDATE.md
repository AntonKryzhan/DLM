# UPDATE.md

## v0.32.0 — Semantic Core Hardening

Дата: 2026-06-10

### Цель патча

После стабилизации `v0.31.2` следующий шаг сделан не в сторону новой метаматематической фичи, а в сторону укрепления архитектурного ядра. Это ответ на главный риск ROADMAP: не дать `checker.rs` превратиться в монолит, где одновременно живут trust policy, bridge preservation, capability checks и passport-инварианты.

`v0.32.0` начинает Nanopass-style hardening: поведение языка сохраняется, но safety-sensitive правила вынесены в отдельные модули.

### Добавлено

```text
crates/dlm_core/src/bridge.rs
crates/dlm_core/src/policy.rs
crates/dlm_core/src/passport_rules.rs
crates/dlm_core/tests/semantic_invariants.rs
```

### BridgeProfile как единый источник истины

Добавлен модуль `bridge.rs`.

Теперь preservation law для bridge-типов описан централизованно:

```text
BridgeKind -> BridgeLaw -> BridgeProfile
```

Это фиксирует единую таблицу:

```text
quote       preserves syntax only
transport   preserves value only
soundness   preserves proof/truth but requires Axiom taint
reflection  preserves syntax/proof, never truth, requires Axiom taint
unsafe      remains Unsafe-tainted
unknown     treated as unsafe for metatheory
```

`soundness.rs` больше не содержит независимую копию bridge-profile логики. Он использует `BridgeProfile` из `bridge.rs`.

### Trust policy вынесена из checker

Добавлен модуль `policy.rs`.

Туда вынесены:

```text
CheckPolicy
join_trust(...)
join_many_trust(...)
is_allowed_by_policy(...)
validate_policy(...)
history_requires_at_least_axiom(...)
history_requires_unsafe(...)
```

Для обратной совместимости `CheckPolicy` остаётся доступен через публичный API:

```text
dlm_core::CheckPolicy
dlm_core::checker::CheckPolicy
```

Главный закон остаётся прежним:

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

Обычная операция не должна бесшумно понижать trust.

### Passport/capability rules вынесены из checker

Добавлен модуль `passport_rules.rs`.

Туда вынесена первая часть правил:

```text
require_capability(...)
trust_join_from_sources(...)
passport_is_axiom_or_worse(...)
passport_is_unsafe(...)
history_contains_ordered_subsequence(...)
```

`checker.rs` теперь вызывает общий helper `passport_rules::require_capability(...)`, а не держит весь capability error path внутри себя.

### Новый semantic invariant test layer

Добавлен тестовый файл:

```text
crates/dlm_core/tests/semantic_invariants.rs
```

Он проверяет не конкретные `.dlm` программы, а базовые законы ядра:

```text
trust join is monotonic
trusted-only rejects Axiom
allow-unsafe accepts Unsafe
quote does not preserve value/proof/truth
transport does not preserve proof/truth
soundness bridge requires Axiom taint
reflection bridge does not preserve truth
unsafe bridge remains Unsafe
BridgeLaw matches BridgeProfile
HistoryChain order is semantic data, not a set
```

### Изменённые файлы

```text
Cargo.toml
Cargo.lock
UPDATE.md
ROADMAP.md
crates/dlm_core/src/lib.rs
crates/dlm_core/src/checker.rs
crates/dlm_core/src/runtime.rs
crates/dlm_core/src/soundness.rs
crates/dlm_core/src/bridge.rs
crates/dlm_core/src/policy.rs
crates/dlm_core/src/passport_rules.rs
crates/dlm_core/tests/semantic_invariants.rs
```

### Что принципиально НЕ менялось

```text
синтаксис .dlm
семантика reflection/self-reference
valid/invalid examples
runtime output
public README model
```

Это архитектурный hardening-патч: он должен давать то же поведение, но с более безопасной внутренней декомпозицией.

---

## v0.31.2 — Self-unprovability boundary hotfix

Дата: 2026-06-10

### Причина

После `v0.31.1` общий `cargo check` проходил, а основной `checker_smoke` проходил 116/116, но отдельный regression-test `reflection_guard.rs` всё ещё падал на форме:

```text
says_unprovable_self()
```

Причина: `says_unprovable_self` ещё не входил в список специально распознаваемых опасных reflection/self-reference форм и поэтому отклонялся как обычный `NameError`, а не как `ReflectionBoundaryError`.

### Исправление

`says_unprovable_self` добавлен в явный boundary guard рядом с:

```text
reflect_provable
prove_self_reference
truth_of_self_reference
truth_of_self
liar_sentence
truth_of_own_truth
```

Теперь эта форма стабильно отклоняется как:

```text
E0907 ReflectionBoundaryError
```

### Новые regression-файлы

```text
examples/invalid/says_unprovable_self_rejected.dlm
```

### Тестовый смысл

Этот hotfix закрывает ещё один вариант self-reference/paradox формы: утверждение вида «я сам недоказуем» не должно исчезать в `NameError`. Для DLM это именно метаматематическая boundary-ошибка, потому что такая конструкция пересекает границу между self-reference, provability и truth без явного claim/axiom path.

---

## v0.31.1 — Reflection bridge enforcement hotfix

Дата: 2026-06-10

### Цель патча

Исправлен regression после `v0.31.0`: `reflection_claim(...)` больше не считается локально безопасной операцией по умолчанию. Теперь даже локальная reflection-операция требует явного bridge-декларатора:

```text
bridge Name : Source -> Target {
    kind = reflection
}
```

Это делает reflection таким же видимым boundary-переходом, как quote/soundness/transport, и убирает неявное пересечение meta-level границы.

### Что исправлено

```text
reflection_claim(provable)
```

теперь проверяет наличие:

```text
BridgeKind::Reflection
```

от `object_theory` провозглашения к текущей `ambient_theory`.

Если bridge отсутствует, checker возвращает:

```text
E0907 ReflectionBoundaryError
```

### Дополнительная защита

В список опасных implicit self-truth форм добавлена функция:

```text
truth_of_self()
```

Теперь она отвергается тем же `ReflectionBoundaryError`, что и:

```text
liar_sentence()
truth_of_own_truth()
truth_of_self_reference(...)
```

### Обновлённые examples

В valid reflection-примеры добавлен явный локальный bridge:

```text
bridge Meta_reflection : Meta -> Meta {
    kind = reflection
}
```

Добавлены regression examples:

```text
examples/invalid/reflection_claim_requires_bridge.dlm
examples/invalid/truth_of_self_rejected.dlm
```

### Смысл исправления

Reflection не должен быть бесплатной операцией даже внутри той же теории. Если система разрешает `reflection_claim(...)` без явного bridge, то появляется скрытый meta-level переход. Для DLM это опасно, потому что reflection должен оставаться частью проверяемой истории, а не неявной возможностью любого proof/provability объекта.

---

## v0.31.0 — Reflection / Self-Reference Guard

Дата: 2026-06-10

### Цель патча

Патч продолжает дорожную карту ЯРД/DLM после блока `v0.30 — Consistency / Incompleteness Boundary` и добавляет следующий защитный слой: явное разделение reflection, self-reference, provability, truth и proof.

Главный закон патча:

```text
Reflection<T.phi> != Truth(phi)
SelfReference<T.phi> != Proof(phi)
SelfReference<T.phi> != Truth(phi)
```

Reflection и самоссылка теперь представлены как отдельные claim-объекты. Они не дают доказательство и не дают истину напрямую. Любой переход к доказательству через reflection/self-reference должен быть явным и получает `trust=Axiom`.

### Что добавлено

#### Новые типы паспортов

```text
Reflection<object_theory.proposition>
SelfReference<proposition>
```

Они добавлены в `TypeKind` и отображаются через `print_symbolic` / `dlm explain`.

#### Новые capability-флаги

```text
can_reflection_reason
can_self_reference_reason
```

Они отделяют reasoning-операции над reflection/self-reference claim от обычных proof/truth операций.

#### Новые builtin-функции

```text
reflection_claim(provable)
reflection_axiom(reflection_claim)
self_reference(prop)
godel_sentence()
self_reference_axiom(self_reference_claim)
```

Смысл:

```text
reflection_claim(...)        создает Reflection<...>, но не Truth и не Proof
reflection_axiom(...)        создает StaticProof<reflection_axiom:...>, trust=Axiom
self_reference(...)          создает SelfReference<...>, но не Truth и не Proof
godel_sentence()             создает специальный SelfReference<godel_sentence>
self_reference_axiom(...)    создает StaticProof<self_reference_axiom:...>, trust=Axiom
```

#### Новая диагностика

```text
E0907 ReflectionBoundaryError
```

Она используется, когда программа пытается пересечь reflection/self-reference boundary неявно.

#### Запрещенные опасные формы

```text
reflect_provable(...)
prove_self_reference(...)
truth_of_self_reference(...)
liar_sentence()
truth_of_own_truth()
```

Эти операции теперь отвергаются как неявный переход через границу reflection/self-reference.

### Изменения в soundness summary

`dlm explain` теперь учитывает:

```text
reflection claims
axiom reflection assumptions
self-reference claims
axiom self-reference assumptions
```

Axiom-tainted reflection/self-reference пути делают summary не clean, как и consistency/truth axiom lifts.

### Новые valid examples

```text
examples/valid/reflection_self_reference_guard.dlm
examples/valid/reflection_summary_axiom.dlm
```

### Новые invalid examples

```text
examples/invalid/reflect_provable_requires_axiom.dlm
examples/invalid/prove_self_reference_fails.dlm
examples/invalid/truth_of_self_reference_fails.dlm
examples/invalid/liar_sentence_rejected.dlm
examples/invalid/truth_of_own_truth_rejected.dlm
examples/invalid/reflection_axiom_requires_claim.dlm
examples/invalid/self_reference_axiom_requires_claim.dlm
examples/invalid/reflection_axiom_rejected_by_trusted_only.dlm
examples/invalid/self_reference_axiom_rejected_by_trusted_only.dlm
```

### Тесты

Добавлены regression-тесты для:

```text
reflection/self-reference valid path
reflection/self-reference axiom accounting in SoundnessSummary
implicit reflection rejection
implicit self-reference rejection
reflection_axiom argument validation
self_reference_axiom argument validation
trusted-only rejection for reflection axioms
trusted-only rejection for self-reference axioms
```

### Инженерный смысл

Этот патч закрывает очередную метаматематическую ловушку: язык больше не может случайно превратить provability/reflection/self-reference в truth/proof без явного Axiom-tainted следа в паспорте и истории.

Это подготавливает следующий слой ROADMAP:

```text
v0.32 — Meta-Level Stratification
```

Там нужно будет жестко разделить object-level, meta-level и meta-meta-level, чтобы reflection и quote не позволяли объектному уровню говорить о собственной truth/provability без явного подъема.
