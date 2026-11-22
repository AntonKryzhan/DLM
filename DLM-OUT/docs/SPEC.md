# SPEC.md — DLM / ЯРД Language Specification v1.0 MVP

## 1. Назначение языка

DLM/ЯРД — язык программирования, статического анализа и формальной математики, построенный вокруг идеи паспортов объектов.

Обычный язык программирования проверяет в основном типы:

```text
x : Nat
```

DLM/ЯРД проверяет тип и паспорт:

```text
x : Nat<Passport>
```

Паспорт описывает:

- как значение построено;
- какие операции над ним разрешены;
- насколько дорого с ним работать;
- откуда оно пришло;
- проверено ли оно;
- какому уровню доверия оно соответствует;
- в каком universe level живёт;
- какой тип равенства к нему применим;
- в какой теории объект валиден.

## 2. Основная формула значения

```text
Value = Type + Passport
```

```text
Passport = Construction
         × Capabilities
         × Cost
         × Trust
         × Provenance
         × Validation
         × Universe
         × Equality
         × TheoryContext
```

## 3. Главный закон компилятора

```text
Операция разрешена не потому, что тип подходит,
а потому что паспорт значения содержит нужную capability,
уровень trust достаточен для текущего режима компиляции,
а объект находится в допустимом TheoryContext.
```

## 4. Запрещённые по умолчанию идеи

В DLM/ЯРД запрещены «голые» объекты:

```text
Nat
Infinity
Proof
Set
exists x
A = B
```

Каждая такая сущность должна иметь контекст:

```text
Nat<construction, access, cost, trust, theory>
Infinity<mode>
Proof<theory, proposition, trust>
Set<universe>
exists x under construction mode
A ==[equality_mode] B
```

## 5. Module / Theory / Bridge

Файл `.dlm` задаёт физический модуль.

```text
1 file = 1 module
1 module = many theories + many bridges
1 theory = one logical world / AmbientTheory scope
1 bridge = explicit transition between theories
```

Пример:

```dlm
module demo.pa_meta

pub theory PA {
    type Nat
    let seven = 7
}

pub theory MetaArithmetic {
    import PA as object_theory
    type Term<T>
}

pub bridge PA_quote : PA -> MetaArithmetic {
    kind = quote
}
```

## 6. AmbientTheory

`AmbientTheory` — текущий логический мир компиляции.

Он задаётся:

```dlm
in theory PA {
    let n = 7
}
```

или телом декларации:

```dlm
theory PA {
    let n = 7
}
```

На верхнем уровне модуля запрещены value-level математические декларации. Разрешены только:

- `module`;
- `import`;
- `theory`;
- `bridge`;
- `alias`;
- `test` / `expect_error` в тестовых файлах;
- metadata.

## 7. TheoryContext

Каждое значение имеет `TheoryContext`:

```text
TheoryContext {
    home: TheoryId,
    valid_in: Set<TheoryId>,
    assumptions: Set<AxiomId>,
    bridge_trace: List<TheoryBridgeId>
}
```

Объект нельзя использовать вне своей теории без явного `TheoryBridge`.

## 8. TheoryBridge

`TheoryBridge` — явный переход между теориями.

Виды мостов MVP:

```text
DefinitionalExtension
ExplicitImport
Quote
Transport
```

Опасные мосты не включаются автоматически:

```text
SoundnessBridge
ReflectionBridge
UnsafeTheoryCast
```

### 8.1 Quote

`quote` переводит объект в синтаксис.

```text
PA.Nat -> MetaArithmetic.Term<PA.Nat>
```

При quote объект теряет вычислительные capabilities и получает синтаксические capabilities.

### 8.2 Transport

`transport` переносит объект в расширенную или совместимую теорию, если bridge явно объявляет, что сохраняется.

### 8.3 SoundnessBridge

`SoundnessBridge` позволяет переходить от:

```text
Provable_T(phi)
```

к:

```text
phi
```

Это опасный мост. В MVP он разрешён только как `trusted` или `axiom` bridge и загрязняет результат через Trust taint.

## 9. Syntax / Provability / Truth

DLM/ЯРД строго различает:

```text
phi                 — утверждение в объектной теории;
quote(phi)          — синтаксический код утверждения;
Provable(T, phi)    — утверждение метатеории о доказуемости phi в T;
Proof<T, phi>       — доказательство phi внутри T;
Truth(phi)          — семантическая истинность, если задана модель или soundness bridge.
```

Из:

```text
Proof<PA, phi>
```

в метатеории автоматически следует только:

```text
Proof<Meta, Provable(PA, quote(phi))>
```

Но не:

```text
Proof<Meta, phi>
```

Для последнего нужен `SoundnessBridge`.

## 10. StaticProof и RuntimeWitness

DLM/ЯРД разделяет статический и рантайм-миры.

```text
StaticProof<P>    — проверено компилятором/ядром/доверенным proof source.
RuntimeWitness<P> — проверено во время исполнения.
```

Внешний ввод не может создавать `StaticProof` напрямую.

Пример:

```dlm
let n = io.read_nat(stdin)
let w = require(n > 0)       // RuntimeWitness<n > 0>
```

Запрещено:

```dlm
let n = io.read_nat(stdin)
proof p : StaticProof<n > 0> = assume(n > 0)
```

если нет `Unsafe` / `Axiom` / `Oracle`.

## 11. External input

Главное правило:

```text
External input is not a value.
External input is a claim candidate.
```

Пайплайн:

```text
Raw bytes
→ parsed value
→ validated value
→ constrained value
→ usable runtime value
```

Любой `read` возвращает `External<Bytes, Raw, Untrusted>` или `Result<...>`.

## 12. Trust taint

Trust levels:

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

Если результат зависит от `Axiom`, итоговый trust не может быть лучше `Axiom`.

Если результат зависит от `Unsafe`, итоговый trust становится `Unsafe`.

В строгом режиме сборки `--trusted-only` запрещены `Axiom`, `Oracle`, `Unsafe`, если они не разрешены конфигурацией.

## 13. Capabilities

Capabilities задают разрешённые действия:

```text
can_print_decimal
can_symbolic_print
can_compare_direct
can_compare_by_proof
can_compute_modular
can_expand
can_inspect_ast
can_quote
can_transport
can_use_in_static_proof
can_use_in_runtime
requires_oracle
```

Результирующие capabilities бинарной операции не равны union capabilities операндов.

Безопасное правило:

```text
result.capabilities ⊆ preserved(lhs.capabilities ∩ rhs.capabilities, operation)
```

Новые capabilities могут появиться только через:

- builtin rule;
- checked proof;
- trusted axiom;
- oracle;
- unsafe rule.

## 14. Cost

Cost отражает не только теоретическую вычислимость, но и практическую доступность.

Примеры:

```text
Trivial
SmallFinite
LargeFinite
Compressed
Recursive
NonExpandable
ProofRequired
Uncomputable
OracleRequired
```

`finite` не означает `writable`, `computable`, `expandable` или `comparable`.

## 15. Infinity modes

В MVP запрещён голый `Infinity`.

Разрешены только типизированные режимы:

```text
Infinity<potential>
Infinity<cardinal>
Infinity<ordinal>
Infinity<limit>
Infinity<class>
Infinity<universe>
```

Операции с бесконечностью обязаны указывать режим.

## 16. Compilation modes

Минимальные режимы:

```bash
dlm check --research
dlm check --strict
dlm check --no-axioms
dlm check --trusted-only
dlm check --allow-unsafe
```

Режим влияет на допустимый максимальный `TrustLevel`.

## 17. MVP restrictions

MVP не реализует:

- полноценный dependent type checker;
- пользовательские passport transformers;
- автоматическое доказательство монотонности;
- SMT solver;
- automatic soundness bridges;
- full proof language;
- code generation to machine code.

MVP реализует:

- parser;
- AST/HIR;
- module resolver;
- theory resolver;
- bridge resolver;
- passport inference для базовых случаев;
- capability check;
- trust taint;
- I/O provenance model;
- diagnostics;
- test matrix.


## v0.22 Universe Levels

Added the first mathematical universe hierarchy layer:

- explicit `U0()`, `U1()`, `U2()` constructors;
- `Set<U n -> U n+1>` as a level-raising object;
- `Class<U n>` as a meta-level view;
- `UniverseLevelError` for bare universes and set-of-all-sets style mistakes;
- `HistoryChain` events for universe, set and class formation.

See `UNIVERSE_HIERARCHY.md`.
