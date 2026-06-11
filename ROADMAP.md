# ROADMAP.md — DLM / ЯРД

## 0. Назначение дорожной карты

Этот документ фиксирует текущую стратегию развития **DLM / ЯРД** после анализа текущего состояния проекта и материалов из `BOOKS.zip`.

Главная цель дорожной карты — не просто перечислить будущие версии, а удержать архитектурную дисциплину проекта:

- не превратить `checker.rs` в монолит;
- не смешать `Truth`, `Provable`, `Proof`, `RuntimeWitness`, `Axiom` и `Unsafe`;
- не потерять passport-семантику при росте языка;
- заранее заложить IR-слои, passes, proof kernel, тестирование инвариантов и audit-модель;
- развивать DLM как формальное ядро плюс цепочку проверяемых преобразований.

DLM должен оставаться языком, где программа не только выдаёт результат, но и объясняет:

- что это за результат;
- из какой теории он пришёл;
- чем он доказан;
- какие аксиомы использованы;
- какой trust-level назначен;
- какие bridge-переходы были сделаны;
- какие границы soundness не были пересечены.

---


## 0.1. Актуализация после v0.31.2: сначала hardening, потом новая метаматематика

После проверки `v0.31.2` курс ROADMAP уточнён. Reflection / Self-Reference Guard стабилизирован, но hotfix-цепочка `v0.31.0 -> v0.31.1 -> v0.31.2` показала главный практический риск: если safety-sensitive правила остаются локальными списками внутри `checker.rs`, легко забыть одну опасную форму и получить неправильный класс ошибки или soundness-дыру.

Поэтому ближайшие версии переставлены в пользу архитектурного укрепления:

```text
v0.31.2 — stable Reflection / Self-Reference Guard
v0.32.0 — Semantic Core Hardening ✅
v0.33.0 — Span / Diagnostic Foundation ✅
v0.34.0 — ID / Resolver Skeleton ✅
v0.35.0 — Checker Orchestration / First Pass Split
v0.36.0 — Property-Based Invariant Tests
v0.37.0 — Meta-Level Stratification
```

Главный принцип:

```text
Сначала сделать DLM устойчивым для дальнейшей разработки агентами.
Потом продолжать наращивать метаматематику.
```

`v0.32.0` начал этот разворот: bridge preservation, trust policy и capability/passport rules вынесены из монолитного чекера в отдельные модули.

`v0.33.0` продолжает hardening-линию: добавлен `SourceSpan` и foundation для точных diagnostics без изменения синтаксиса языка и без пересборки AST-модели.

`v0.34.0` добавляет первый ID / resolver skeleton: typed IDs, `IdAllocator`, `Resolver`, `ResolvedModule` и `SymbolTable`. Это не меняет публичный язык, но готовит AST -> HIR -> ResolvedHIR pipeline.

---

## 1. Текущее состояние проекта

**DLM — Deductive Logic Machine** сейчас уже не просто черновой синтаксис языка. Это работающий Rust-прототип формального языка, где математические объекты, программы, доказательства, trust-уровни, provenance, validation state, theory context и derivation history представлены как части единой паспортной модели.

Главная идея проекта уже реализуется практически:

> Значение в DLM — это не только runtime object, но и объект с логическим паспортом.

Текущая стабильная линия уже умеет:

```text
dlm check <file.dlm>
dlm run <file.dlm>
dlm explain <file.dlm>
```

На данный момент подтверждённая стабильная база:

```text
v0.31.2 — Reflection / Self-Reference Guard stable baseline
```

Текущий активный слой:

```text
v0.34.0 — ID / Resolver Skeleton
```

Текущая baseline-проверка для `v0.31.2` и последующих hardening-патчей:

```powershell
cargo check
cargo test
cargo run -p dlm_cli -- check examples\valid\reflection_self_reference_guard.dlm
cargo run -p dlm_cli -- run examples\valid\reflection_self_reference_guard.dlm
cargo run -p dlm_cli -- explain examples\valid\reflection_summary_axiom.dlm
```

После успешной проверки `v0.33.0` архитектурный hardening продолжен этапом `v0.34.0 ID / Resolver Skeleton`.

---

## 2. Что уже построено

### 2.1. Базовый язык и CLI

Уже есть минимальный язык `.dlm`:

```dlm
theory Meta {
    let x = 7
    print_decimal(x)
}
```

Реализованы:

- parser;
- AST;
- checker;
- diagnostics;
- runtime;
- CLI;
- examples;
- tests;
- docs.

Команды:

```text
dlm check
dlm run
dlm explain
```

### 2.2. Passport-модель

Главное ядро проекта — **паспорт значения**.

Паспорт сейчас содержит:

```text
TypeKind
ConstructionMode
CostClass
TrustLevel
Provenance
ValidationState
TheoryContext
LocationContext
CapabilitySet
HistoryChain
```

То есть каждое значение знает:

- что оно такое;
- как оно построено;
- какова цена его проверки;
- можно ли ему доверять;
- из какой теории оно пришло;
- какие операции над ним разрешены;
- какая у него история происхождения.

Ключевой принцип:

```text
Операция разрешается не по одному типу, а по паспорту:
type + capabilities + trust + theory + history
```

### 2.3. Trust policy

Уже реализованы режимы доверия:

```text
research mode
trusted-only mode
allow-unsafe mode
```

Работают trust-уровни:

```text
Checked
Builtin
Axiom
Oracle
Unsafe
```

Главный закон:

```text
Axiom / Oracle / Unsafe не должны бесшумно становиться Checked.
```

В `--trusted-only` axiom/unsafe пути отрезаются.

### 2.4. HistoryChain

История значения уже стала полноценной ordered chain, а не set.

Это важно, потому что:

```text
A -> B -> C
```

и

```text
A -> C
```

логически разные derivation paths.

История сохраняет:

```text
created:literal_nat
runtime_input:read_nat
proof_kernel:term:true_intro
proof_kernel:check
bridge:quote
bridge:soundness
consistency:claim
consistency:axiom
truth:from_provable_axiom
```

---

## 3. Что добавлено в дорожную карту из BOOKS.zip

Материалы из `BOOKS.zip` дают не просто список книг, а конкретные архитектурные решения для DLM. Их нужно встроить в план разработки как обязательные инженерные и математические ориентиры.

### 3.1. Nanopass: дробить язык на маленькие passes

Источник: `nanopass.pdf`.

Главная польза для DLM:

> Не строить один огромный checker. Строить цепочку маленьких проверяемых проходов.

Целевая архитектура:

```text
source.dlm
  ↓
lexer
  ↓
parser
  ↓
RawAST
  ↓
AST validation pass
  ↓
HIR
  ↓
name / theory resolution pass
  ↓
ResolvedHIR
  ↓
type checking pass
  ↓
TypedIR
  ↓
proof-term checking pass
  ↓
ProofIR
  ↓
passport construction pass
  ↓
PassportIR
  ↓
bridge / policy validation pass
  ↓
CheckedModule
  ↓
soundness / audit summary
```

Практическое правило:

```text
Каждый pass должен иметь:
- входной IR;
- выходной IR;
- явно записанные инварианты;
- собственные ошибки;
- valid/invalid tests;
- regression tests.
```

Это должно стать главным архитектурным принципом после v0.31.

### 3.2. Essentials of Compilation: развивать язык слоями

Источник: `essentials-of-compilation.pdf`.

Главная польза для DLM:

> Не добавлять фичи хаотично. Каждая версия языка должна вводить один слой семантики и один набор инвариантов.

Для DLM это означает:

```text
DLM-0: Nat, Bool, theory, let
DLM-1: passport lattice
DLM-2: bridge taxonomy
DLM-3: proof terms
DLM-4: static proof kernel
DLM-5: modules/imports
DLM-6: HIR / TypedIR / PassportIR
DLM-7: verified transformations
```

Каждый новый слой должен иметь:

```text
grammar
AST / IR
checker rules
runtime/explain behavior
valid examples
invalid examples
property tests
invariant documentation
```

### 3.3. PLAI: не смешивать syntax, value и meaning

Источник: `plai-v325.pdf`.

Главная польза для DLM:

> Семантика языка важнее синтаксической формы.

Для DLM это значит, что нужно жёстко различать:

```text
Term<PA.Nat>       != PA.Nat
syntax_eq          != value_eq
value_eq           != proof_eq
ProofTerm<P>       != StaticProof<P>
Provable<P>        != TruthClaim<P>
RuntimeWitness<P>  != StaticProof<P>
```

Будущие опасные зоны:

```text
quote
macro
alias
desugaring
import
qualified names
syntax-level equality
runtime-level equality
proof-level equality
```

Каждая из этих зон должна иметь отдельный pass и отдельные ошибки.

### 3.4. Software Foundations / LF: proof objects и минимальное логическое ядро

Источник: `lf/`.

Главная польза для DLM:

> Доказательство должно быть объектом, а не комментарием к объекту.

В DLM это напрямую поддерживает:

```text
ProofTerm
StaticProof
kernel_check
ProofObjects
Curry–Howard style
Inductive propositions
Relations
Extraction-style thinking
```

Будущий proof kernel должен развиваться не как набор случайных функций, а как маленькая формальная система с явными конструкторами proof terms.

### 3.5. Software Foundations / PLF: formal semantics, type soundness, preservation/progress

Источник: `plf/`.

Главная польза для DLM:

> У языка должны быть свойства, которые можно формулировать как теоремы.

Для DLM нужно постепенно зафиксировать такие законы:

```text
Type preservation:
если term имеет тип T до шага вычисления,
то после допустимого шага он остаётся в T.

Passport preservation:
если value имеет passport P,
то допустимый pass не может удалить trust/provenance/history без явного правила.

Progress:
если checked module принят,
то он либо исполним, либо корректно остановлен на известной proof/trust/runtime границе.

Bridge preservation:
bridge сохраняет только те свойства, которые указаны в BridgeProfile.

Trust monotonicity:
Axiom / Oracle / Unsafe taint не исчезает неявно.
```

Это должно лечь в `docs/METATHEORY.md` и в будущие regression/property tests.

### 3.6. QuickChick: property-based тестирование инвариантов

Источник: `qc/`.

Главная польза для DLM:

> Примеры вручную не поймают все ошибки passport/bridge/trust lattice.

Для Rust-реализации DLM нужно использовать аналоги:

```text
proptest
quickcheck
insta snapshots
golden tests
```

Генераторы, которые нужно добавить:

```text
random Nat expressions
random Bool expressions
random Prop expressions
random theory names
random bridge kinds
random BridgeProfile combinations
random Passport values
random TrustLevel chains
random HistoryChain sequences
random invalid proof lifts
random quote/transport/soundness/reflection chains
```

Свойства, которые нужно тестировать:

```text
unsafe taint never disappears
axiom taint is monotonic
quote never preserves value
transport never preserves proof by default
soundness bridge always requires axiom taint
reflection never creates TruthClaim without explicit axiom path
runtime witness never becomes static proof directly
passport join is monotonic
history order is preserved
trusted-only rejects axiom/unsafe paths
```

### 3.7. Security Foundations: trust/passport как taint- и IFC-модель

Источник: `secf/`.

Главная польза для DLM:

> TrustLevel и passport можно рассматривать как security labels / taint labels.

Для DLM это означает:

```text
низкодоверенный источник не должен неявно стать высокодоверенным результатом;
unsafe/axiom/oracle taint не должен исчезать;
bridge должен явно описывать, что именно он сохраняет;
history chain должен фиксировать все trust-changing transitions.
```

Целевой закон:

```text
Если input passport имеет trust = Unsafe,
то output passport не может иметь trust = Checked,
если нет отдельного trusted proof-kernel path.
```

Это нужно оформить в будущем `policy.rs` и `passport_rules.rs`.

### 3.8. CPDT: reflection, dependent types и certified programming

Источник: `cpdt.pdf`.

Главная польза для DLM:

> Reflection — опасная зона. Она не должна бесплатно превращать синтаксис, доказуемость и истину друг в друга.

Для DLM важно:

```text
reflection не должен быть бесплатным;
soundness bridge должен быть явно axiom-tainted;
truth не должна появляться из provability без записанного допущения;
proof by reflection требует контролируемого kernel path;
каждый reflection path должен попадать в explain/audit.
```

Этот материал особенно важен для v0.31–v0.35 и будущего proof assistant слоя.

### 3.9. SLPJ: функциональное ядро и будущий evaluator/runtime

Источник: `slpj-book-1987-searchable.pdf`.

Главная польза для DLM:

> Если DLM будет расти в сторону функционального ядра, нужен отдельный Core language.

Это пригодится после стабилизации proof/passport слоя:

```text
lambda calculus
core language
graph reduction
lazy/strict evaluation boundary
functional IR
closure representation
```

Сейчас это не первый приоритет, но это важный ориентир для runtime/evaluator этапа.

### 3.10. SLF / VC / VFA: долгосрочная верификация runtime, памяти и алгоритмов

Источники: `slf/`, `vc/`, `vfa/`.

Главная польза для DLM:

```text
SLF — ownership/resource reasoning, memory/resource logic;
VC  — verified low-level/runtime/backend reasoning;
VFA — verified data structures and algorithms.
```

Это полезно позже для:

```text
remote memory regions
GPU memory
checkpoint/restore
distributed execution
verified stdlib data structures
verified backend/runtime contracts
```

Сейчас эти материалы нужно держать как поздний ориентир, а не тащить в MVP.

---

## 4. Уже закрытые математические блоки

### v0.22 — Universe Levels / Set vs Class

Реализовано:

```text
U0()
U1()
U2()
universe_succ(u)
set_of(u)
class_of(u)
set_lives_in(set)
class_level(class)
```

Главный закон:

```text
Set<U n> lives in U(n+1)
Class<U n> is not Set<U n>
set_of_all_sets() запрещён
```

Это первая защита от Russell-style universe ошибок.

### v0.23 — Definability Passport

Реализовано:

```text
Language
Encoding
MetaLevel
DefinableNat
```

Функции:

```text
language_L0()
encoding_godel()
meta_level(k)
definable_nat(language, encoding, bound, meta_level)
definability_bound(x)
definability_meta_level(x)
```

Заблокированы Berry-style конструкции:

```text
berry_number()
smallest_undefinable()
undefinable_nat()
bare definable_nat
```

Главный закон:

```text
Definability requires explicit language + encoding + bound + meta-level.
```

### v0.24 — BigNumber Hierarchy

Реализованы:

```text
Graham()
TREE(n)
BB(n)
fast_growing(level)
growth_parameter(x)
```

Главный закон:

```text
BigNat is finite but not decimal-printable by default.
```

Например:

```text
TREE(3) можно print_symbolic
TREE(3) нельзя print_decimal
BB(1000) finite but uncomputable
```

### v0.25 — Minimal Proof Kernel

Реализовано первое proof-kernel ядро:

```text
ProofTerm
StaticProof
check_proof(...)
```

Функции:

```text
proof_true()
true_intro()
proof_gt(a, b)
gt_intro(a, b)
check_proof(term)
kernel_check(term)
verify_proof(term)
```

Главная цепочка:

```text
kernel constructor
→ ProofTerm
→ check_proof
→ StaticProof
```

### v0.26 — Passport Soundness / Formal Metatheory

Добавлена команда:

```text
dlm explain <file.dlm>
```

Она показывает:

```text
values checked
static proofs
proof terms
kernel-checked proofs
runtime witnesses
axiom-tainted values
unsafe-tainted values
bridge events
soundness guarantee
invariant issues
```

Главный смысл:

```text
dlm check говорит: программа допустима.
dlm explain говорит: на каких trust/soundness основаниях она допустима.
```

### v0.27 — Formal Bridge Theory

Классифицированы bridge-типы:

```text
definitional
conservative
quote
transport
soundness
reflection
migration
materialize
unsafe
```

Для bridge теперь есть soundness profile:

```text
preserves_syntax
preserves_value
preserves_proof
preserves_truth
requires_axiom
is_conservative
is_reflective
is_reversible
taint
role
```

Главные законы:

```text
quote       preserves syntax only
transport   preserves value role only
soundness   requires axiom taint
reflection  requires explicit reflective controls
unsafe      has no safe preservation law
```

### v0.28 — Extended Infinity Mathematics

Расширена теория бесконечностей:

```text
Infinity<cardinal>
Infinity<ordinal>
Infinity<limit>
Infinity<potential>
Infinity<class>
Infinity<universe>
```

Функции:

```text
cardinal_add(a, b)
ordinal_add(a, b)
limit_omega()
potential_infinity()
potential_step(p)
class_infinity(class)
universe_infinity(universe)
```

Главный закон:

```text
режимы бесконечности не смешиваются.
```

### v0.29 — Provability / Truth Boundary

Реализовано разделение:

```text
Prop
Provable
TruthClaim
StaticProof
```

Главный закон:

```text
Provable_T(phi) ≠ Truth(phi)
```

Функции:

```text
prop_true()
prop_gt(a, b)
provable_of(static_proof)
truth_from_provable(...)
truth_from_provable_axiom(...)
```

Правила:

```text
truth_from_provable(...) запрещён без soundness/axiom lift.
truth_from_provable_axiom(...) разрешён, но становится trust=Axiom.
```

### v0.30 — Consistency / Incompleteness Boundary

Реализовано:

```text
ConsistencyClaim
IncompletenessBoundaryError
```

Функции:

```text
consistency_claim()
consistency_of_current()
consistent_current()
prove_consistency(claim)
prove_own_consistency(claim)
assume_consistency(claim)
consistency_axiom(claim)
consistency_from_axiom(claim)
```

Главный закон:

```text
Consistency<T> is a claim object, not a proof.
```

То есть:

```dlm
let c = consistency_claim()
let p = prove_consistency(c)
```

запрещено.

А вот:

```dlm
let p = assume_consistency(c)
```

разрешено, но только как:

```text
trust=Axiom
```

### v0.31 — Reflection / Self-Reference Guard

Выданный следующий слой.

Цель:

```text
запретить опасную reflection/self-reference без явного axiom-tainted пути.
```

Добавляется:

```text
ReflectionClaim
SelfReferenceClaim
ReflectionBoundaryError
```

Функции:

```text
reflection_claim(provable)
reflection_axiom(reflection_claim)
self_reference(prop)
godel_sentence()
self_reference_axiom(self_reference_claim)
```

Запрещаются опасные формы:

```text
reflect_provable(...)
prove_self_reference(...)
truth_of_self_reference(...)
liar_sentence()
truth_of_own_truth()
```

Главные законы:

```text
Reflection<T.phi> ≠ Truth(phi)
SelfReference<T.phi> ≠ Proof(phi)
SelfReference<T.phi> ≠ Truth(phi)
```

---

## 5. Где мы остановились

Проект остановился на переходе от общей паспортной математики к защите от метаматематических ловушек.

Уже закрыты:

```text
truth/provability boundary
consistency/proof boundary
bridge soundness classification
universe/class separation
definability guard
big number guard
minimal proof kernel
soundness explain layer
```

Следующий активный фокус:

```text
reflection/self-reference/metalevel safety
```

То есть сейчас мы строим не просто язык, а систему, которая не позволяет случайно смешать:

```text
объектный уровень;
мета-уровень;
доказуемость;
истину;
самоссылку;
аксиоматическое предположение;
проверенное доказательство.
```

---

## 6. Архитектурный план после анализа книг

### 6.1. Главный риск: монолитный checker

Текущий MVP нормально держится на `checker.rs`, но дальше это станет узким местом.

Запрещённое направление развития:

```text
parser.rs
checker.rs  ← всё: имена, типы, паспорта, proof, bridges, diagnostics
runtime.rs
```

Целевое направление:

```text
crates/dlm_core/src/
  lexer.rs
  parser.rs
  ast.rs
  span.rs
  ast_validate.rs
  ids.rs
  hir.rs
  lower.rs
  resolve.rs
  types.rs
  typeck.rs
  proof.rs
  proofck.rs
  passport.rs
  passport_rules.rs
  passport_infer.rs
  bridge.rs
  bridgeck.rs
  policy.rs
  soundness.rs
  audit.rs
  diagnostics.rs
  runtime.rs
```

### 6.2. IR-слои, которые нужно ввести

Нужно перейти от прямой работы по AST к последовательной модели:

```text
RawAST
  только синтаксис и исходные имена

ValidatedAST
  синтаксически корректная структура без грубых AST-ошибок

HIR
  high-level IR: теории, объявления, выражения, но ещё с unresolved names

ResolvedHIR
  все имена, theory references, imports и bridges переведены в IDs

TypedIR
  типы проверены и записаны явно

ProofIR
  proof terms отделены от runtime expressions

PassportIR
  каждому value/term назначен паспорт

CheckedModule
  финальный модуль после bridge/policy/soundness checks
```

### 6.3. ID-модель вместо строковых имён

Чтобы imports/modules не сломали систему, нужно подготовить:

```rust
pub struct FileId(pub u32);
pub struct ModuleId(pub u32);
pub struct TheoryId(pub u32);
pub struct ValueId(pub u32);
pub struct TypeId(pub u32);
pub struct BridgeId(pub u32);
pub struct ProofId(pub u32);
```

Строковые имена должны жить только на ранних этапах (`RawAST`, `HIR`). После resolution всё должно ссылаться на IDs.

### 6.4. Span вместо line

Сейчас `line: usize` достаточно для MVP, но мало для нормального языка.

Целевая модель:

```rust
pub struct Span {
    pub file_id: FileId,
    pub start: BytePos,
    pub end: BytePos,
}

pub struct BytePos(pub u32);
```

Это нужно для:

```text
точных diagnostics;
многофайловых imports;
IDE/LSP;
JSON diagnostics;
passport diff;
source mapping;
golden tests.
```

---

## 7. Ближайший план: v0.31 → v0.35

### v0.31 — Reflection / Self-Reference Guard

Статус:

```text
патч выдан;
нужна локальная проверка.
```

Definition of done:

```text
cargo check OK
cargo test OK
reflection_self_reference_guard.dlm OK
reflection_summary_axiom.dlm explain показывает axiom-taint
invalid examples корректно падают
README/docs/test matrix обновлены
```

После успеха:

```powershell
git add .
git commit -m "v0.31: add reflection and self-reference guard"
git tag v0.31.0
git push
git push origin v0.31.0
```

### v0.32 — Meta-Level Stratification

Цель:

```text
жёстко разделить object theory, meta theory, meta-meta theory.
```

Нужно добавить:

```text
MetaLevel<T, n>
ObjectLevel
MetaTheory
ReflectionLevel
```

Функции:

```text
object_level()
meta_level_of(theory)
lift_to_meta(value)
meta_quote(value)
```

Главный закон:

```text
объектный уровень не может говорить о собственной truth/provability без явного подъёма в meta-level.
```

Новые ошибки:

```text
MetaLevelError
LevelEscapeError
```

Дополнение из книг:

```text
- использовать LF/PLF как ориентир для object/meta-language separation;
- не смешивать object syntax и meta syntax;
- meta_quote должен создавать Term<T>, а не TruthClaim<T>;
- каждый lift_to_meta должен записываться в HistoryChain.
```

### v0.34.0 — ID / Resolver Skeleton

Статус:

```text
реализовано как hardening-патч перед HIR / ResolvedHIR.
```

Добавлено:

```text
ids.rs
resolve.rs
FileId / ModuleId / TheoryId / ValueId / TypeId / BridgeId / ProofId
IdAllocator
Resolver
ResolvedModule / ResolvedTheory / ResolvedValue / ResolvedBridge
SymbolTable
```

Главный закон:

```text
после успешного resolver pass каждая declared theory/value/bridge имеет typed ID,
а bridge endpoints указывают на существующие TheoryId.
```

Ограничение:

```text
checker.rs ещё не переведён на ResolvedModule; это будет следующий orchestration шаг.
```

### v0.33 — Statement / Theorem Layer

Цель:

```text
отделить proposition от theorem declaration.
```

Добавить:

```text
Statement
Theorem
Goal
Hypothesis
```

Функции:

```text
statement(prop)
theorem(name, prop)
goal(prop)
assume_hypothesis(prop)
```

Главный закон:

```text
Prop is not Theorem.
Theorem requires checked proof or explicit axiom status.
```

Дополнение из книг:

```text
- theorem должен иметь явный trusted base;
- theorem без StaticProof должен попадать в AxiomRegistry;
- Statement должен быть синтаксическим/логическим объектом, но не proof object;
- HypothesisSet должен быть частью ProofContext, а не глобального состояния.
```

### v0.34 — Proof Goal System MVP

Цель:

```text
начать строить маленькую proof assistant модель внутри DLM.
```

Добавить:

```text
GoalState
ProofContext
HypothesisSet
TacticStep
```

Функции:

```text
begin_proof(goal)
intro(ctx)
exact(ctx, proof)
close_proof(ctx)
```

Главный результат:

```text
proof scripts begin to produce StaticProof only through kernel-checked closure.
```

Дополнение из книг:

```text
- опираться на LF ProofObjects и PLF STLC/STLCProp;
- ProofContext должен быть иммутабельной/контролируемой структурой;
- close_proof не должен обходить kernel_check;
- каждый tactic step должен сохранять историю вывода.
```

### v0.35 — Axiom Registry / Trusted Base Accounting

Цель:

```text
каждая аксиома должна быть видима в отчёте.
```

Добавить:

```text
AxiomRegistry
TrustedBase
AssumptionSet
```

Команда `dlm explain` должна показывать:

```text
trusted base:
  builtin rules: N
  explicit axioms: N
  unsafe assumptions: N
  soundness assumptions: N
  consistency assumptions: N
  reflection assumptions: N
```

Главный закон:

```text
нет скрытых аксиом.
```

Дополнение из книг:

```text
- CPDT: reflection assumptions считать частью trusted base;
- Security Foundations: axiom taint должен вести себя как taint label;
- QuickChick/proptest: добавить генеративные проверки, что axiom taint не исчезает.
```

---

## 8. Архитектурный hardening: v0.36 → v0.42

Этот блок добавлен после анализа `BOOKS.zip`. Его лучше выполнить до большого расширения синтаксиса.

### v0.36 — BridgeProfile Unification

Цель:

```text
сделать единый BridgeProfile, которым пользуются checker, soundness, explain и audit.
```

Нужно добавить/вынести:

```text
bridge.rs
bridgeck.rs
BridgeProfile
BridgePreservation
BridgeTaint
BridgeRole
```

Главный закон:

```text
Ни checker, ни soundness.rs не должны иметь отдельную несогласованную логику bridge-preservation.
```

Definition of done:

```text
- все bridge-типы описаны через один BridgeProfile;
- checker использует BridgeProfile;
- explain использует BridgeProfile;
- tests проверяют quote/transport/soundness/reflection/unsafe;
- docs/BRIDGE_THEORY.md обновлён.
```

### v0.37 — Passport Lattice / Policy Rules Split

Цель:

```text
вынести правила trust/capability/passport из общего checker.
```

Нужно добавить:

```text
policy.rs
passport_rules.rs
TrustLattice
CapabilityRules
PassportJoin
PassportMeet
TaintPropagation
```

Главные законы:

```text
Axiom taint is monotonic.
Unsafe taint is never hidden.
Capabilities define allowed operations.
Passport join is monotonic.
```

Definition of done:

```text
- отдельные unit tests для trust lattice;
- отдельные unit tests для capability checks;
- no direct ad-hoc trust comparison inside checker.rs;
- trusted-only mode использует policy.rs.
```

### v0.38 — HIR / Resolution MVP

Цель:

```text
подготовить нормальную module/import/theory архитектуру.
```

Добавить:

```text
ids.rs
hir.rs
lower.rs
resolve.rs
NameResolver
TheoryResolver
BridgeResolver
SymbolTable
```

Главный закон:

```text
после resolution ключевые ссылки должны быть ID-based, а не String-based.
```

Definition of done:

```text
- RawAST → HIR работает;
- HIR → ResolvedHIR работает;
- TheoryId используется внутри checker;
- diagnostics сохраняют Span исходного имени;
- старые examples не ломаются.
```

### v0.39 — TypedIR / ProofIR / PassportIR MVP

Цель:

```text
разделить типизацию, proof-checking и passport inference на разные слои.
```

Добавить:

```text
TypedIR
ProofIR
PassportIR
typeck.rs
proofck.rs
passport_infer.rs
```

Главный закон:

```text
ProofTerm не должен быть обычным runtime expression.
StaticProof не должен появляться без proofck/kernel path.
Passport inference не должен менять типовую семантику.
```

Definition of done:

```text
- checker.rs становится orchestration layer, а не местом всей логики;
- типовые ошибки идут из typeck.rs;
- proof errors идут из proofck.rs;
- passport errors идут из passport_infer.rs/passport_rules.rs;
- explain работает поверх PassportIR/CheckedModule.
```

### v0.40 — Span / Diagnostic Engine MVP

Цель:

```text
перейти от line-only diagnostics к полноценным Span.
```

Добавить:

```text
span.rs
FileId
BytePos
Span
DiagnosticBuilder
RelatedDiagnostic
PassportDiff
```

Новые возможности:

```text
primary error
secondary labels
reason chain
passport diff
suggested fix
minimal example
JSON diagnostics groundwork
```

### v0.41 — Property-Based Test Layer

Цель:

```text
добавить генеративные проверки паспортов, мостов и trust policy.
```

Rust-инструменты:

```text
proptest
quickcheck
insta snapshots
```

Свойства:

```text
unsafe taint never disappears
axiom taint is monotonic
quote never preserves value
transport never preserves proof by default
soundness bridge always requires axiom taint
reflection never creates TruthClaim without explicit axiom path
runtime witness never becomes static proof directly
passport join is monotonic
history order is preserved
trusted-only rejects axiom/unsafe paths
```

### v0.42 — Metatheory Documentation Pass

Цель:

```text
зафиксировать формальные свойства DLM в документации.
```

Добавить:

```text
docs/METATHEORY.md
docs/PASSPORT_LATTICE.md
docs/BRIDGE_THEORY.md
docs/TYPE_SOUNDNESS_NOTES.md
docs/TRUST_IFC_MODEL.md
```

Минимальные свойства:

```text
Progress
Preservation
Passport preservation
Trust monotonicity
Bridge preservation
Axiom accounting
Runtime/proof separation
Reflection boundary
```

---

## 9. Средний план: v0.43 → v0.55

### v0.43 — Quantifier MVP

Добавить:

```text
Forall
Exists
BoundVar
Predicate
```

Функции:

```text
forall(x, prop)
exists(x, prop)
instantiate(forall_proof, value)
witness(exists_claim, value)
```

Главные риски:

```text
scope capture
free variable leakage
runtime-dependent quantifier misuse
```

Дополнение из книг:

```text
- использовать PLF/STLC подход к variables, substitution, contexts;
- добавить alpha-equivalence и capture-avoiding substitution;
- bound/free variables должны быть отдельным pass.
```

### v0.44 — Equality / Rewrite Kernel

Добавить:

```text
EqProof
RewriteRule
Congruence
Substitution
```

Функции:

```text
refl(x)
symm(eq)
trans(eq1, eq2)
rewrite(term, eq)
```

Главный закон:

```text
value equality, syntax equality, proof equality и rewrite equality остаются разными слоями.
```

Дополнение из книг:

```text
- rewrite должен быть proof-level operation;
- нельзя переписывать TruthClaim через syntax_eq;
- каждый rewrite должен иметь EqProof или explicit trusted rule.
```

### v0.45 — Nat Induction MVP

Добавить:

```text
InductionScheme<Nat>
BaseCase
StepCase
InductionProof
```

Функции:

```text
nat_induction(base, step)
prove_by_induction(goal, induction_scheme)
```

Ограничение MVP:

```text
только статические finite Nat propositions.
```

### v0.46 — Module / Import System

Добавить:

```text
import
export
public/private theorem
module passport
dependency graph
```

Команды:

```text
dlm check project
dlm explain project
```

Новые риски:

```text
циклические imports;
trusted dependency leakage;
public/private theorem leakage;
theory alias confusion;
shadowing;
version mismatch.
```

### v0.47 — Kernel Audit Report

Расширить `dlm explain` до полноценного аудита.

Команда:

```text
dlm audit <file.dlm>
```

Аудит должен показывать:

```text
proof kernel rules used
axioms used
bridges used
unsafe sources
runtime witnesses
universe lifts
reflection/self-reference guards
consistency assumptions
trusted base
policy mode
property/invariant warnings
```

### v0.48 — Standard Core Library

Создать `std/core`:

```text
Nat
Bool
Prop
Proof
Eq
Order
Set
Class
Universe
Infinity
Definability
BigNat
```

Правило:

```text
stdlib не должна быть скрытой trusted base.
Каждый builtin/theorem/axiom из std должен иметь явный passport.
```

### v0.49 — Better Parser / Syntax Stabilization

Цель:

```text
уменьшить экспериментальность синтаксиса.
```

Добавить:

```text
better error spans
comments
multi-line expressions
theorem syntax
proof blocks
import syntax
```

### v0.50 — Human-Friendly Diagnostics

Добавить:

```text
primary error
reason chain
passport diff
suggested fix
minimal example
```

Пример:

```text
error: cannot use Provable(phi) as Truth(phi)

because:
  value is Provable<Meta.phi>
  required TruthClaim<phi>
  no soundness bridge found
  no axiom lift requested

suggestion:
  use truth_from_provable_axiom(...) if you intentionally accept Axiom taint
```

### v0.51 — Documentation Rebuild

Собрать документацию в структуру:

```text
docs/
  00_OVERVIEW.md
  01_LANGUAGE_SYNTAX.md
  02_PASSPORT_MODEL.md
  03_TRUST_MODEL.md
  04_PROOF_KERNEL.md
  05_THEORY_BRIDGES.md
  06_UNIVERSES.md
  07_PROVABILITY_TRUTH.md
  08_CONSISTENCY.md
  09_REFLECTION.md
  10_RUNTIME_MODEL.md
  11_GPU_CLUSTER_MODEL.md
  12_METATHEORY.md
  13_TESTING_STRATEGY.md
  14_ROADMAP.md
```

### v0.52 — Public Alpha Cut

Цель:

```text
сделать первую понятную публичную alpha-версию.
```

Критерии:

```text
cargo check/test stable
README актуален
ROADMAP.md актуален
docs readable
examples grouped
invalid examples documented
GitHub tags exist
release notes exist
no hidden target/build artifacts in repository
```

### v0.53 — Small-Step Semantics Notes MVP

Цель:

```text
зафиксировать операционную семантику хотя бы для малого ядра языка.
```

Добавить:

```text
docs/SMALL_STEP_SEMANTICS.md
step relation for core expressions
value forms
stuck states
trusted boundary states
runtime witness states
```

### v0.54 — Type Soundness Notes MVP

Цель:

```text
описать progress/preservation для малого DLM-core.
```

Минимум:

```text
progress for checked core expressions
preservation for type/passport under safe evaluation
explicit stuck states for unsafe/runtime/proof boundary
```

### v0.55 — Proof Kernel Rules Table

Цель:

```text
сделать proof kernel полностью обозримым.
```

Добавить:

```text
docs/PROOF_KERNEL_RULES.md
KernelRule enum
rule IDs
rule explanations
rule usage in audit
```

---

## 10. Дальнейший план: v0.56 → v0.70

### v0.56 — Verified Transformation Checks

Цель:

```text
каждый IR-pass должен иметь проверяемые до/после-инварианты.
```

Добавить:

```text
Pass trait
PassInvariant
PassReport
Debug IR dump
--dump-ir
--verify-passes
```

### v0.57 — Golden Tests / Snapshot Tests

Цель:

```text
стабилизировать вывод explain/audit/diagnostics.
```

Добавить:

```text
insta snapshot tests
golden diagnostics
golden explain reports
golden audit reports
```

### v0.58 — Desugaring Pass

Цель:

```text
отделить удобный синтаксис от core language.
```

Добавить:

```text
SurfaceAST
CoreAST
DesugarPass
```

Главный закон:

```text
Desugaring не должен добавлять proof/truth/trust без явного правила.
```

### v0.59 — Core Language Freeze Candidate

Цель:

```text
выделить минимальное стабильное ядро DLM-core.
```

Критерии:

```text
Core syntax stable
Core IR stable
Proof kernel MVP stable
Passport semantics stable
Trust lattice stable
BridgeProfile stable
```

### v0.60 — Real Runtime Architecture

Сейчас distributed/GPU слой в основном passport-symbolic.

Нужно разделить:

```text
symbolic runtime
local runtime
remote runtime
GPU runtime
cluster runtime
```

Главный закон:

```text
физическое исполнение не должно менять математический passport без явного materialize/migration bridge.
```

### v0.65 — Real Worker Runtime

Добавить:

```text
worker node
job scheduling
remote execution
serialization
checkpoint files
restore files
```

### v0.70 — GPU Backend Prototype

Позже можно добавить реальный backend:

```text
CUDA optional backend
ROCm optional backend
CPU fallback backend
```

Но важно:

```text
математический паспорт должен остаться выше физического исполнения.
GPU execution не должен ломать trust/proof semantics.
```

---

## 11. Поздний план: v0.71 → v0.90

### v0.75 — Package / Project System

Добавить:

```text
yard.toml package metadata
dependency lockfile
theory imports
trusted dependency policy
```

### v0.80 — IDE / LSP MVP

Добавить:

```text
syntax highlighting
diagnostics
go to definition
passport hover
theory graph view
proof status view
trust/audit hover
bridge path view
```

### v0.85 — Verified Data Structures Layer

Источник-ориентир: `vfa/`.

Добавить:

```text
verified maps
verified sets
verified tries
verified ordering structures
proof-carrying stdlib algorithms
```

### v0.90 — Runtime Resource Logic

Источник-ориентир: `slf/`.

Добавить основу для рассуждений о ресурсах:

```text
ownership of runtime resources
remote region capabilities
GPU region capabilities
checkpoint ownership
restore validity
resource separation
```

---

## 12. Долгосрочная цель: v1.0

v1.0 должна означать не «всё готово», а:

```text
язык имеет стабильное ядро;
passport semantics documented;
proof kernel MVP reliable;
trust model reliable;
theory boundary reliable;
basic stdlib exists;
examples and docs complete enough for external users.
```

Минимальные критерии v1.0:

1. стабильный синтаксис MVP;
2. стабильный passport model;
3. стабильный trust lattice;
4. стабильная bridge theory;
5. proof kernel MVP;
6. theorem/goal MVP;
7. universe/set/class layer;
8. provability/truth/consistency/reflection guards;
9. project-level checking;
10. public documentation;
11. ROADMAP.md актуален;
12. `dlm audit` показывает trusted base;
13. property-based tests покрывают trust/passport/bridge laws;
14. IR pipeline отделён от parser/checker монолита;
15. diagnostics имеют Span и reason chains.

---

## 13. Главные инварианты проекта

Эти правила нельзя ломать без осознанного решения.

### 13.1. Provability is not Truth

```text
Provable<T.phi> ≠ Truth(phi)
```

Любой переход от provability к truth требует:

```text
explicit soundness bridge
```

или

```text
axiom-tainted lift
```

### 13.2. Consistency is not Proof

```text
Consistency<T> ≠ Proof(Consistency<T>)
```

Любая consistency assumption должна быть видна как:

```text
trust=Axiom
```

### 13.3. Reflection is not implicit

```text
Reflection is explicit only.
```

Самоссылка и reflection не должны появляться как побочный эффект quote/provability/truth операций.

### 13.4. Axiom taint is monotonic

Если объект получил:

```text
trust=Axiom
```

он не должен самопроизвольно становиться:

```text
trust=Checked
```

### 13.5. Unsafe taint is never hidden

Любой Unsafe-путь должен быть виден в:

```text
check
explain
audit
history
```

### 13.6. Bridge does not magically preserve everything

Каждый bridge обязан явно сказать, что он сохраняет:

```text
syntax
value
proof
truth
```

Нельзя считать, что `transport` или `quote` переносят truth.

### 13.7. HistoryChain is semantic data

`HistoryChain` — это часть смысла значения.

Его нельзя оптимизировать как set, потому что порядок событий важен.

### 13.8. Capabilities define allowed operations

Операция разрешается не по имени типа, а по capability.

Примеры:

```text
Nat with CanPrintDecimal можно печатать десятично.
BigNat without CanPrintDecimal нельзя.
Remote<Nat> нельзя print_decimal без materialize.
GpuValue<Nat> нельзя print_decimal без copy_from_gpu.
```

### 13.9. RuntimeWitness is not StaticProof

```text
RuntimeWitness<P> ≠ StaticProof<P>
```

Runtime-наблюдение может быть полезным, но оно не становится статическим доказательством без явного проверяемого пути.

### 13.10. Syntax is not value

```text
Term<T> ≠ T
```

`quote` создаёт синтаксический объект, а не значение и не proof/truth.

### 13.11. ProofTerm is not checked proof

```text
ProofTerm<P> ≠ StaticProof<P>
```

Только `kernel_check` / `check_proof` может поднимать proof term до `StaticProof`.

### 13.12. Desugaring must not add trust

Desugaring может менять форму программы, но не должен добавлять:

```text
TruthClaim
StaticProof
Checked trust
Axiom trust
bridge preservation
```

без явного правила.

---

## 14. Стандартный формат каждого следующего патча

Каждый новый патч должен содержать:

1. изменение версии в `Cargo.toml`;
2. обновление `README.md` при изменении публичной модели;
3. обновление `ROADMAP.md`, если меняется направление;
4. обновление `IMPLEMENTATION_NOTES.md`;
5. обновление `docs/*.md`;
6. обновление `docs/STD_CORE.md`;
7. обновление `docs/TEST_MATRIX.md`;
8. valid examples;
9. invalid examples;
10. regression tests;
11. unit tests для новых правил;
12. property-based tests для trust/passport/bridge, если затронута эта зона;
13. `cargo check`;
14. `cargo test`;
15. ZIP только с изменёнными файлами.

Минимальная проверка после каждого патча:

```powershell
cargo check
cargo test
cargo run -p dlm_cli -- check examples\valid\<new_example>.dlm
cargo run -p dlm_cli -- run examples\valid\<new_example>.dlm
cargo run -p dlm_cli -- explain examples\valid\<new_summary>.dlm
cargo run -p dlm_cli -- check examples\invalid\<new_invalid>.dlm
```

Если добавлен audit:

```powershell
cargo run -p dlm_cli -- audit examples\valid\<new_audit_example>.dlm
```

Если добавлен IR/pass:

```powershell
cargo run -p dlm_cli -- check examples\valid\<new_example>.dlm --verify-passes
cargo run -p dlm_cli -- check examples\valid\<new_example>.dlm --dump-ir
```

---

## 15. Git workflow

После успешной проверки патча:

```powershell
git status
cargo check
cargo test
git add .
git commit -m "v0.31: add reflection and self-reference guard"
git tag v0.31.0
git push
git push origin v0.31.0
```

Для следующих версий:

```powershell
git commit -m "v0.32: add meta-level stratification"
git tag v0.32.0

git commit -m "v0.33: add statement and theorem layer"
git tag v0.33.0

git commit -m "v0.34: add proof goal MVP"
git tag v0.34.0
```

Для архитектурных hardening-патчей:

```powershell
git commit -m "v0.36: unify bridge profiles"
git tag v0.36.0

git commit -m "v0.37: split passport policy rules"
git tag v0.37.0

git commit -m "v0.38: add HIR and name resolution MVP"
git tag v0.38.0
```

---

## 16. Главные подводные камни и как их обойти

### 16.1. Подводный камень: один огромный checker

Симптомы:

```text
checker.rs растёт быстрее всего;
каждая новая фича требует править старые ветки;
bridge/trust/type/proof ошибки перемешаны;
explain начинает расходиться с checker;
тесты становятся хрупкими.
```

Решение:

```text
ввести passes;
ввести HIR/TypedIR/PassportIR;
checker.rs оставить orchestration layer;
каждый pass тестировать отдельно.
```

### 16.2. Подводный камень: строковые имена вместо ID

Симптомы:

```text
теории сравниваются как String;
imports ломают lookup;
qualified names становятся нестабильными;
shadowing ведёт к ошибкам;
модульность невозможно проверить строго.
```

Решение:

```text
FileId, ModuleId, TheoryId, ValueId, BridgeId, ProofId;
NameResolver;
SymbolTable;
ResolvedHIR.
```

### 16.3. Подводный камень: quote переносит больше, чем должен

Симптом:

```text
Term<T> начинает использоваться как T или Proof<T>.
```

Решение:

```text
quote preserves syntax only;
quote never preserves value/proof/truth by default;
BridgeProfile должен это enforce-ить.
```

### 16.4. Подводный камень: reflection становится дырой в soundness

Симптом:

```text
provability превращается в truth через reflection без явной аксиомы.
```

Решение:

```text
reflection path всегда explicit;
reflection assumptions видны в AxiomRegistry;
reflection events видны в explain/audit;
truth_from_reflection без axiom запрещён.
```

### 16.5. Подводный камень: runtime witness становится доказательством

Симптом:

```text
наблюдение во время выполнения начинает считаться StaticProof.
```

Решение:

```text
RuntimeWitness отдельный тип/роль;
RuntimeWitness → StaticProof запрещён без kernel/certified path;
explain показывает runtime witnesses отдельно.
```

### 16.6. Подводный камень: diagnostics невозможно улучшить из-за line-only

Симптомы:

```text
ошибки показывают только строку;
невозможно нормальное IDE;
невозможно точное указание imported source;
passport diff трудно связать с исходным кодом.
```

Решение:

```text
Span { file_id, start, end };
DiagnosticBuilder;
source labels;
related notes.
```

### 16.7. Подводный камень: ручные tests не ловят lattice ошибки

Симптомы:

```text
examples/valid проходят;
examples/invalid проходят;
но случайная цепочка bridge/trust ломает инвариант.
```

Решение:

```text
proptest/quickcheck;
random Passport;
random BridgeProfile;
random HistoryChain;
monotonicity properties;
taint preservation tests.
```

---

## 17. Карта материалов из BOOKS.zip по назначению

```text
nanopass.pdf
  Назначение:
    архитектура маленьких passes, IR pipeline, checker decomposition.
  Где применять:
    v0.36–v0.39, весь дальнейший compiler/checker architecture.

essentials-of-compilation.pdf
  Назначение:
    дисциплина staged compiler construction, IR, incremental language growth.
  Где применять:
    все версии, особенно v0.38–v0.56.

plai-v325.pdf
  Назначение:
    semantics через interpreters, scope, desugaring, syntax/value separation.
  Где применять:
    parser/HIR/desugaring/evaluator/diagnostics.

lf/
  Назначение:
    logic, proof objects, inductive propositions, Curry–Howard base.
  Где применять:
    proof kernel, StaticProof, ProofTerm, theorem layer.

plf/
  Назначение:
    operational semantics, Hoare logic, type systems, STLC, preservation/progress.
  Где применять:
    typeck, proofck, METATHEORY.md, soundness docs.

qc/
  Назначение:
    property-based testing methodology.
  Где применять:
    v0.41, tests for passport/trust/bridge laws.

secf/
  Назначение:
    noninterference, information-flow control, taint-like trust model.
  Где применять:
    policy.rs, TrustLattice, passport taint propagation.

cpdt.pdf
  Назначение:
    dependent types, proof automation, reflection, certified programming.
  Где применять:
    reflection guard, proof assistant layer, future dependent proof model.

slpj-book-1987-searchable.pdf
  Назначение:
    functional language implementation, core language, graph reduction.
  Где применять:
    future evaluator/runtime/core functional language.

slf/
  Назначение:
    separation logic, ownership/resource reasoning.
  Где применять:
    future remote/GPU/checkpoint resource semantics.

vc/
  Назначение:
    verified C / low-level verification.
  Где применять:
    future verified runtime/backend.

vfa/
  Назначение:
    verified functional algorithms and data structures.
  Где применять:
    future verified stdlib.
```

---

## 18. Концептуальная цель проекта

DLM движется к языку, где математический объект не просто записан, а полностью объясним:

```text
что это;
где это живёт;
из какой теории пришло;
чем доказано;
какой уровень доверия;
какая история вывода;
какие операции разрешены;
какие аксиомы использованы;
какие bridge-переходы были сделаны;
какие риски soundness есть.
```

Конечная цель:

```text
программирование + доказательства + trust accounting + symbolic mathematics + formal metatheory
```

в одной системе.

DLM должен стать языком, где программа может ответить:

```text
я не только посчитала результат,
я могу объяснить, почему этот результат допустим,
какие предположения использованы,
какие границы не были пересечены,
и где начинается аксиома, runtime witness или unsafe zone.
```

---

## 19. Краткая команда на ближайшие действия

Сейчас правильный порядок такой:

```text
1. Допрогнать и стабилизировать v0.31.
2. Закоммитить и затегать v0.31.0.
3. Добавить ROADMAP.md в корень репозитория.
4. Следующим патчем идти не только в v0.32, но и начать подготовку архитектурного hardening:
   - bridge.rs;
   - policy.rs;
   - passport_rules.rs;
   - ids.rs;
   - span.rs.
5. Не расширять язык слишком быстро, пока не начато разделение AST/HIR/TypedIR/PassportIR.
```

Главный приоритет:

```text
DLM должен расти не как набор функций, а как маленькое формальное ядро плюс цепочка проверяемых passes.
```

## v0.35.0 status note — Checker orchestration started

`v0.35.0` adds the first explicit pass pipeline around the checker:

```text
raw_ast_accepted -> name_resolution -> legacy_checker
```

This keeps the current language behavior stable while making the checker architecture ready for the planned HIR / ResolvedHIR / TypedIR / ProofIR / PassportIR split.

## v0.36.0 status note — Property-style invariant layer started

`v0.36.0` adds the first generated/enumerative invariant tests for the DLM semantic core.

The active protected surfaces are now:

```text
TrustLevel lattice
CheckPolicy thresholds
BridgeProfile / bridge_law consistency
quote / transport / soundness / reflection / unsafe bridge boundaries
passport trust preservation
HistoryChain order and multiplicity
```

This is intentionally dependency-free for now. The current finite lattices are exhaustively enumerated by deterministic tests. Later property-test work can add randomized AST/passport generators after the IR pipeline is more stable.

## v0.37.0 status note — Meta-Level Stratification foundation

`v0.37.0` adds the first explicit meta-level API before introducing theorem declarations or proof goals.

The implemented foundation is deliberately conservative:

```text
M0 = object level
M1 = meta level
M2 = meta-meta level
```

The new invariant is:

```text
syntax/provability/truth/self-reference of level N requires observer level > N
```

This prepares later reflection, theorem, HIR and ProofIR work without changing current `.dlm` syntax or weakening the legacy checker.

## v0.38.0 completed — Statement / Theorem foundation

`v0.38.0` adds the first internal theorem-declaration layer without introducing new surface syntax yet.

Implemented foundation:

```text
Statement<P>
Theorem<name:P>
Goal<P>
Hypothesis<P>
```

The main invariant is now represented in code and regression tests:

```text
Theorem requires StaticProof or explicit Axiom admission.
```

This prepares the next proof-assistant steps: `ProofContext`, `HypothesisSet`, `TacticStep` and `close_proof(...)`.


## v0.39.0 completed — Proof Context foundation

`v0.39.0` adds the first internal proof-context layer on top of the statement/theorem foundation.

Implemented foundation:

```text
ProofContext
HypothesisSet
TacticStep
ProofObligation
ProofClosure
```

The protected closure invariant is now explicit:

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```

This prepares later `ProofIR`, tactic syntax and theorem-checker passes without changing current `.dlm` syntax.


## v0.40.0 completed — Tactic Script foundation

`v0.40.0` adds the first internal tactic-script model on top of the proof-context foundation.

Added:

```text
crates/dlm_core/src/tactic.rs
crates/dlm_core/tests/tactic_script.rs
docs/TACTIC_SCRIPT.md
```

Protected laws:

```text
Assume<P> keeps the goal open.
ExactStaticProof closes only through the existing ProofContext rule.
AdmitAxiom closes with explicit Axiom taint.
A closing tactic must be the final tactic in a script.
```

No public `.dlm` syntax was added. This prepares the future ProofIR/TacticIR layer without changing checker/runtime behavior.

## v0.41.0 completed — Proof Certificate foundation

`v0.41.0` adds the first internal proof-certificate model on top of tactic execution and proof closure.

New module:

```text
crates/dlm_core/src/certificate.rs
```

New focused tests:

```text
crates/dlm_core/tests/proof_certificate.rs
```

Important implementation laws:

```text
ProofCertificate is an audit artifact, not new proof evidence.
Only closed ProofClosure values can emit certificates.
Open tactic reports cannot be certified.
Axiom-admitted closures remain status=AxiomAdmitted and trust>=Axiom.
Certificate identity must match the theorem passport exactly.
Certificate fingerprint depends on trace order and certificate contents.
```

This is a foundation layer for later certificate serialization, proof audit reports and proof-kernel boundary hardening.


## v0.42.0 completed — Proof Certificate Audit / Export foundation

`v0.42.0` adds deterministic proof-certificate export and audit reporting.

This prepares the future CLI/API surface for emitting proof artifacts without yet adding new language syntax.


## v0.43.0 completed — Equality Proof / Rewrite foundation

`v0.43.0` adds the first core layer for equality proofs and rewrite certificates.

Completed:

- `EqProof` passport kind;
- `RewriteRule` passport kind;
- `RewriteCertificate` passport kind;
- forward/reverse rewrite application;
- ordered rewrite traces;
- axiom-taint preservation through rewrite certificates;
- regression tests for boolean equality separation, runtime/static separation, trace order, and trust preservation.

This prepares future theorem/tactic automation while keeping current `.dlm` programs stable.


## v0.44.0 completed — Rewrite Normalization / Audit foundation

`v0.44.0` adds bounded rewrite normalization and audit/export reporting over the equality rewrite layer.

Completed:

- ordered forward rewrite normalization;
- step-bound guard against cyclic rewrite systems;
- `RewriteNormalizationReport`;
- audit validation for trace/certificate endpoint consistency;
- stable text export for normalization reports;
- taint-preserving normalization certificates.

This prepares future simplification passes and theorem/tactic automation while keeping existing `.dlm` programs stable.


### v0.45.0 — Completed: Nat Induction MVP

Implemented the internal Nat induction proof foundation:

```text
InductionScheme<Nat,P>
BaseCase<P(0)>
StepCase<forall n:Nat. P(n) -> P(succ(n))>
InductionProof<forall n:Nat. P(n)>
```

The layer is intentionally core-only: no new `.dlm` syntax and no runtime/checker behavior changes.


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

Status: planned/patch delivered.

This is part of stage 1: Metamathematical Foundation. It introduces explicit axiom registries and ordered dependency audits before moving to ordinary language mathematics.

New follow-up targets for stage 1:

- theorem dependency graph;
- global metatheory closure report;
- conservative extension audit;
- bridge assumption inventory;
- proof-kernel dependency contract.

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
