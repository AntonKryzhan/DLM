# DLM Architectural Laws — Конституция архитектуры ЯРД

## 0. Назначение документа

Этот документ фиксирует обязательные архитектурные законы DLM / ЯРД.

Цель: язык общего назначения с детерминированным вычислительным ядром, явными эффектами, контролируемым владением ресурсами, многоуровневой проверкой свойств и эффективным машинным представлением.

Требование закона, проектное решение и реализованная возможность — разные статусы. Этот текст задаёт нормативную цель и критерии приёмки; он не подтверждает реализацию механизмов в конкретной версии. Упоминания состояния 0.86 отражают исходный документ, а не новый аудит исходников. Имена будущих типов, IR, режимов и команд обозначают проектные сущности, пока их поддержка не подтверждена отдельно.

Нумерация законов 1–29 сохранена. Новые законы 30–40 уточняют семантику, системные возможности и математическое развитие; разделы обзора, готовности и итогового принципа перенесены в 41–43.

Это набор обязательных ограничений, которые должны удерживать язык от опасных деградаций:

```text
1. Математически красиво, но soundness-размыто.
2. Формально богато, но аппаратно медленно.
3. Быстро патчится AI-агентами, но архитектурно расползается.
4. Быстро исполняется, но небезопасно по владению, aliasing and lifetime.
5. Компилируется в native/backend, но не имеет стабильного layout/ABI-контракта.
6. Запускается в разных режимах, но не объявляет реальный уровень гарантий.
7. Проверяется формально, но может зависнуть на normalization/proof-search без бюджета.
8. Доказывает завершение или математическое равенство, но скрывает машинные ошибки.
9. Объявляется универсальным и быстрым без прикладных проверок и измерений.
```

Главная формула DLM:

```text
Смысл должен быть голографическим.
Исполнение должно быть плотным.
Память должна иметь владение.
Представление должно иметь контракт.
Сборка должна объявлять профиль гарантий и область их действия.
Нормализация должна иметь предел.
Аудит должен объяснять результат назад.
```

Или в технической форме:

```text
Meaning-rich above.
Ownership-safe in memory.
Layout-stable at boundaries.
Execution-dense below.
Assurance-explicit by property and build mode.
Normalization-bounded by fuel.
Audit-complete backward.
```

Эти законы применяются ко всем будущим слоям:

```text
Source / mathematical layer
IR / compiler layer
Runtime control layer
Hardware execution layer
Proof / audit layer
Memory ownership and resource layer
ABI / layout / serialization layer
Build-mode / assurance layer
High-performance native compilation layer
AI-agent development workflow
```

Если новый патч временно нарушает один из законов ради MVP, это должно быть явно записано как:

```text
Technical Debt
Open Obligation
Known Incomplete Law Enforcement
```

Для такого отклонения фиксируются затронутый закон, точная граница гарантии, зависимые компоненты и проверяемый критерий закрытия. Запись долга не разрешает объявлять непроверенное свойство доказанным, скрывать unsafe или выпускать несовместимый артефакт под прежним контрактом.

---

## 1. Разделяй смысловые слои

### Формулировка

DLM должен жёстко разделять слои смысла, проверки, управления исполнением и физического исполнения:

```text
1. Source / mathematical layer
   Полный смысл: proof, passport, theory, trust, provenance, history, audit.

2. IR / compiler layer
   Проверка, lowering, optimization, bridge policy, proof/passport erasure, audit reports.

3. Runtime control layer
   Compact passport descriptors, capabilities, location, scheduling, materialization.

4. Hardware execution layer
   Raw memory, dense buffers, kernels, minimal metadata.
```

### Зачем это нужно

Без разделения слоёв DLM станет либо формально тяжёлым, либо аппаратно неэффективным. Proof и history нужны наверху, но не должны попадать внутрь каждого машинного действия.

### Что запрещено

```text
proof checking внутри GPU kernel;
полный HistoryChain на каждый элемент массива;
полный Passport на каждый байт dense buffer;
branching по trust-level внутри каждого SIMD/SIMT lane;
dynamic dispatch внутри каждого GPU thread;
смешивание syntax/value/proof/truth/runtime в одном IR.
```

### Что разрешено

```text
богатые паспорта на source/IR level;
compact descriptors на runtime level;
raw dense buffers на hardware level;
audit reconstruction через provenance/fingerprint/region metadata.
```

### Проверка закона

Каждый новый слой должен явно указывать, к какому уровню он принадлежит:

```text
source-only
compiler-only
runtime-control
hardware-execution
audit-only
```

Если структура пересекает уровни, должен быть bridge или lowering-pass.

### Влияние

```text
checker architecture;
IR pipeline;
runtime representation;
GPU/CPU scheduler;
proof/passport erasure;
high-performance compiler backend.
```

---

## 2. Управляй операциями через паспорт

### Формулировка

В DLM операция разрешается не только типом, а полным паспортом объекта:

```text
type + capabilities + trust + provenance + validation + theory + location + history
```

### Зачем это нужно

Один и тот же тип может иметь разный смысл. Например, `Nat` может быть локальным, remote, GPU-resident, runtime input, axiom-tainted, checked, compressed, printable or non-printable.

### Что запрещено

```text
разрешать операцию только по имени TypeKind;
печатать BigNat без capability;
использовать Remote<Nat> как Local<Nat> без materialization;
использовать RuntimeWitness как StaticProof;
использовать Unsafe value в trusted-only режиме.
```

### Что разрешено

```text
Nat + can_print_decimal => print_decimal;
GpuValue + can_copy_from_gpu => copy_from_gpu;
StaticProof + checked validation => theorem construction;
Axiom-tainted proof => theorem with visible axiom trust.
```

### Проверка закона

Новые операции должны иметь capability checks и trust-policy checks. Любой обход через raw TypeKind считается архитектурным долгом.

### Влияние

```text
checker;
passport_rules;
policy;
runtime permissions;
optimization eligibility;
CPU/GPU routing.
```

---

## 3. Проверяй proof наверху, стирай proof внизу

### Формулировка

Proof, theorem evidence, certificate and audit data должны проверяться на source/compiler level и стираться из hot runtime code после проверки.

```text
proof-carrying compile time
proof-erased runtime
```

### Зачем это нужно

Иначе DLM будет математически богатым, но медленным. Proof нужен, чтобы доказать безопасность и допустимость оптимизаций, но не должен исполняться в каждом цикле.

### Что запрещено

```text
хранить full proof object в hot runtime loop;
проверять proof внутри GPU thread;
таскать proof certificates через PCIe;
ветвиться по proof status внутри SIMD loop;
строить StaticProof во время обычного numerical runtime.
```

### Что разрешено

```text
compile-time proof checking;
proof certificate export;
proof-erased native code;
runtime witness as runtime witness, not static proof;
optional debug/audit build with retained metadata.
```

### Проверка закона

Будущие backend-патчи должны иметь explicit erasure pass:

```text
ProofIR -> ErasedRuntimeIR
PassportIR -> CompactRuntimeDescriptor
```

### Влияние

```text
high-performance compilation;
proof kernel;
runtime model;
LLVM/MLIR backend;
verified optimization.
```

---

## 4. Держи паспорта на регионах, а не на каждом байте

### Формулировка

Для dense runtime data паспорт должен быть привязан к региону, buffer, tensor, matrix, array slice or memory object, а не к каждому байту или элементу.

### Зачем это нужно

Полный паспорт на каждый элемент массива уничтожит кеши, SIMD, GPU throughput и memory bandwidth.

### Что запрещено

```text
Passport per byte;
Full HistoryChain per tensor element;
TrustLevel branch per GPU lane;
per-element dynamic capability lookup;
per-element provenance object in dense numeric buffers.
```

### Что разрешено

```text
RegionPassport;
BufferDescriptor;
TensorPassport;
MemoryRegion capability;
compact taint summary;
per-region audit fingerprint.
```

### Проверка закона

Будущий runtime должен отличать:

```text
semantic object passport
runtime region passport
hardware buffer descriptor
```

### Влияние

```text
runtime representation;
GPU buffers;
SIMD arrays;
database columnar execution;
zero-copy execution;
memory layout.
```

---

## 5. Делай pure core детерминированным

### Формулировка

Pure core языка должен быть детерминированным: одинаковый checked input при одинаковой семантике операций и числовом профиле даёт одинаковый наблюдаемый исход.

Чистота не означает завершение или отсутствие ошибки. Детерминированный исход может быть определённой арифметической ошибкой; потенциальное незавершение указывается отдельно. Для успешно полученных результатов passport summary и semantic fingerprint воспроизводимы при одинаковых зависимостях. Случайные пути сборки, длительность и wall-clock timeout не входят в идентичность проверенного смысла.

### Зачем это нужно

Детерминизм нужен для proof checking, caching, reproducible builds, verified optimization and audit.

### Что запрещено

```text
скрытый randomness в pure core;
скрытый IO;
зависимость от wall-clock time;
недетерминированный iteration order в fingerprint-sensitive местах;
плавающий trust result при одинаковом input.
```

### Что разрешено

```text
явный RuntimeWitness;
explicit random source with provenance;
explicit nondeterministic effect boundary;
stable sorting before fingerprint where order is semantic-neutral;
order-sensitive fingerprint where order is semantic.
```

### Проверка закона

Pure operations must be snapshot/fingerprint stable. Нестабильность должна быть effectful boundary.

### Влияние

```text
logic layer;
substitution;
normalization;
proof certificates;
incremental compilation;
cache checked meaning.
```

---

## 6. Все эффекты вводи через явную границу

### Формулировка

IO, network, filesystem, clock, randomness, external oracle, unsafe input, GPU execution, remote execution and runtime observation must cross explicit effect boundaries.

### Зачем это нужно

Эффекты нельзя смешивать с proof/truth. Runtime observation не должен становиться StaticProof.

### Что запрещено

```text
runtime read -> StaticProof;
network oracle -> Checked theorem;
GPU kernel result -> local value without materialization;
randomness inside pure proof;
unsafe external file -> trusted value without taint.
```

### Что разрешено

```text
RuntimeWitness;
OracleInput;
UnsafeExternal;
ExplicitEffectBoundary;
MaterializationBridge;
Axiom-tainted assumption;
RuntimeChecked validation.
```

### Проверка закона

Все effectful operations должны быть отражены в типизированном контракте и доступной политике аудита. Набор эффектов распространяется через цепочки вызовов и функциональные значения; для высших порядков нужна параметризация по эффектам.

Эффекты включают IO, сеть, время, случайность, процессы, изменение состояния и внешний код. Потенциальное незавершение отражается в контракте вычисления. Отсутствие эффекта IO не доказывает отсутствие переполнений.

### Влияние

```text
runtime;
proof/truth boundary;
trust model;
security model;
external integrations;
Web3/database/container domains.
```

---

## 7. Capabilities используй как маршрутизатор вычислений

### Формулировка

Capabilities должны не только разрешать операции, но и направлять вычисления: CPU, GPU, remote, vectorized, batch, cached, portable, serializable.

### Зачем это нужно

Паспорт должен быть не грузом, а планировщиком вычислений.

```text
Passport is not a GPU payload.
Passport is a CPU/compiler/runtime scheduling instruction.
```

### Что запрещено

```text
отправлять на GPU без capability;
копировать с GPU без materialization capability;
делать batch fusion без purity/noalias/vectorizable capability;
исполнять remote operation как local;
игнорировать location/cost capabilities при scheduling.
```

### Что разрешено

```text
can_compile_gpu_kernel => GPU codegen candidate;
can_serialize_for_migration => remote scheduling candidate;
can_symbolic_print => symbolic output;
NoAlloc + Pure + NoAlias + compatible arithmetic/error order => vectorization candidate;
GpuResident + Batchable => keep result on GPU.
```

### Проверка закона

Scheduler/optimizer должен читать capabilities, а не делать ad-hoc decisions.

### Влияние

```text
runtime scheduler;
GPU backend;
distributed execution;
compiler optimizer;
high-performance native compilation.
```

---

## 8. Bridge должен иметь контракт сохранения

### Формулировка

Каждый bridge обязан явно указывать, что он сохраняет:

```text
syntax
value
proof
truth
location
trust
capabilities
history
```

### Зачем это нужно

Bridge — основная зона soundness-риска. Нельзя считать, что quote, transport, reflection, soundness or materialization сохраняют всё автоматически.

### Что запрещено

```text
quote preserves value;
transport preserves proof by default;
reflection creates truth;
materialization changes location without history;
unsafe bridge pretends checked;
unknown bridge preserves anything silently.
```

### Что разрешено

```text
BridgeProfile;
BridgePreservationContract;
Axiom-tainted soundness bridge;
explicit materialization bridge;
reflection with visible axiom boundary.
```

### Проверка закона

Каждый новый BridgeKind требует BridgeProfile, tests and docs.

### Влияние

```text
soundness;
compiler lowering;
module migration;
GPU materialization;
proof/truth boundary;
audit.
```

---

## 9. Trust только ухудшается или явно доказывается

### Формулировка

Trust не должен улучшаться неявно. Axiom/Oracle/Unsafe taint не может исчезнуть без явного доказанного trusted path.

### Зачем это нужно

Это центральный taint-law DLM.

### Что запрещено

```text
Unsafe -> Checked silently;
Oracle -> Builtin silently;
Axiom -> Checked by formatting/export/import;
trust downgrade hidden in runtime;
trusted-only accepts axiom path.
```

### Что разрешено

```text
trust join conservatively combines dependencies under an explicit trust order;
explicit kernel-checked proof path;
explicit audit report showing taint;
trusted-only rejection;
status downgrade when proof is missing.
```

### Проверка закона

Property tests must check trust monotonicity and taint preservation.

### Влияние

```text
policy;
passport rules;
audit;
optimizer safety;
proof certificates;
trusted base.
```

### Доказанный путь не стирает зависимости

Проверка ядром устанавливает корректность вывода относительно объявленных предпосылок. Если доказательство использует аксиому, oracle или unsafe-предположение, зависимость сохраняется. Независимое доказательство без этой зависимости может обосновать новое утверждение с меньшей trusted base; это отдельный проверенный путь с собственным provenance, а не очистка исходного значения.

---

## 10. History полная в audit, компактная в runtime

### Формулировка

Full HistoryChain статических доказательств и преобразований должен сохраняться или адресоваться в audit/proof/explain layer. Runtime может использовать compact history descriptor or fingerprint.

Для долгоживущих процессов задаются границы и политика хранения динамического аудита: потоковая запись, checkpoint, срок хранения или явно ограниченное окно. Не требуется бесконечная история в RAM. Fingerprint без доступного исходного артефакта не позволяет восстановить его содержимое; неполное покрытие истории явно обозначается.

### Зачем это нужно

Full history нужна для объяснимости. Но runtime не должен таскать большие цепочки в hot path.

### Что запрещено

```text
терять audit history, требуемую объявленным профилем, без указания неполноты;
нести full history per byte/per element;
использовать runtime compact history как полный proof audit;
оптимизировать HistoryChain как set, если порядок семантичен.
```

### Что разрешено

```text
FullHistory in audit;
HistoryFingerprint in runtime;
RegionHistorySummary;
ordered audit trace;
compact runtime descriptor.
```

### Проверка закона

Runtime erasure must preserve ability to reconstruct or reference audit history.

### Влияние

```text
explain;
audit;
runtime performance;
incremental compilation;
compiled artifact metadata.
```

---

## 11. Checker разбит на passes

### Формулировка

Checker must be orchestration layer, not a monolith. The language must be checked through explicit passes.

### Целевая цепочка

```text
Source
 -> Parser / Surface AST
 -> HIR / Resolved HIR
 -> Typed Core
 -> Proof / Passport / Bridge / Policy checks
 -> CheckedModule
 -> Executable MIR
 -> Representation IR
 -> Backend input (C or another target)
```

### Зачем это нужно

Монолитный checker ломает AI-agent-friendly development, diagnostics, tests, proof boundaries and future compiler backend.

### Что запрещено

```text
добавлять все новые semantic rules прямо в checker.rs;
смешивать name resolution, type checking, proof checking, passport inference;
делать diagnostics без pass context;
обходить passes ради быстрого MVP без Open Obligation.
```

### Что разрешено

```text
small focused pass modules;
pass-local invariants;
pass reports;
pass-specific diagnostics;
checker.rs as orchestrator.
```

### Проверка закона

Каждый новый semantic layer должен указать target pass or planned pass.

### Влияние

```text
compiler architecture;
future IDE/LSP;
project-level checking;
verified transformations;
AI-agent patch safety.
```

### Контракт Typed Core и проверка IR

Typed Core — основной носитель семантики. Для каждой операции хранятся или однозначно выводятся типы операндов и результата, эффекты, требования владения, ошибки, арифметическая модель, Span, доказанные условия и происхождение преобразования.

Executable MIR фиксирует порядок исполнения, блоки и операции ресурсов. Representation IR фиксирует физические типы, размеры, выравнивание и ABI. Proof IR хранит контексты и доказательные термы; эти границы не требуют отдельных копий дерева для каждого прохода.

Backend получает проверенные свойства из frontend. Восстановление исходных типов эвристикой не заменяет их передачу через Typed Core.

Каждое преобразование обязано сохранять корректность IR: существование операндов, согласованность типов, определение до использования, допустимые переходы, владение, эффекты и применимость доказательных предпосылок. В debug-конфигурации структурный verifier запускается после каждого прохода. Его успешность сама по себе не доказывает эквивалентность преобразования.

---

## 12. После resolution только ID, не String

### Формулировка

После resolution ключевые ссылки должны использовать IDs, а не raw String names.

```text
ModuleId
TheoryId
ValueId
TypeId
ProofId
BridgeId
SymbolId
```

### Зачем это нужно

String-based resolution ломается на imports, aliases, shadowing, qualified names, project checking and caching.

### Что запрещено

```text
сравнивать resolved theory by String;
хранить imported theorem reference как raw name;
искать bridge после resolution через string lookup;
строить dependency graph по display name;
кэшировать checked meaning только по строковому имени.
```

### Что разрешено

```text
names in Source/HIR;
IDs in ResolvedHIR and below;
name table for diagnostics;
Span/name mapping for user output.
```

### Проверка закона

New resolved structures must use IDs or explicitly mark temporary String debt.

Локальные `FunctionId`, `ValueId`, `RegionId` и другие ID компиляционной сессии отделяются от стабильной идентичности экспортируемого объявления и от имени для диагностики. Индекс в текущем массиве не служит долговременным идентификатором пакета.

### Влияние

```text
modules/imports;
proof dependencies;
incremental build;
project checker;
IDE/LSP;
compiler cache.
```

---

## 13. Каждый смысловой объект имеет Span

### Формулировка

Every semantic object originating from source must carry Span or source mapping.

`Span` включает идентификатор файла и точный диапазон. Раскрытие конструкций и оптимизации сохраняют цепочку происхождения. Байтовые смещения, индексы Unicode-скаляров и позиции протокола редактора преобразуются явно.

### Зачем это нужно

Diagnostics, proof audit, explain, LSP, source mapping and AI-agent debugging require precise source origins.

### Что запрещено

```text
line-only diagnostics for new complex features;
semantic object without source mapping;
error without primary span when source exists;
imported theorem without origin trace.
```

### Что разрешено

```text
Span;
SourceMap;
GeneratedSpan for compiler-generated objects;
SyntheticOrigin for generated audit-only objects;
RelatedDiagnostic labels.
```

### Проверка закона

New parser/checker features must preserve Span into relevant IR.

### Влияние

```text
diagnostics;
IDE/LSP;
audit trace;
proof object debugging;
AI-agent patch review.
```

---

## 14. Runtime данные плотные

### Формулировка

Runtime representation must be dense, cache-friendly and backend-friendly. Semantic richness must not imply runtime object bloat.

Требование плотности относится прежде всего к числовым буферам и горячим проходам. Персистентные списки, деревья, словари и арены графов могут иметь иное представление с явной стоимостью. Название `List` не задаёт асимптотику и не означает плотный вектор.

### Зачем это нужно

Плотные буферы, предсказуемое представление и контроль выделений создают условия для быстрого исполнения. Сопоставимая с C++/ASM скорость подтверждается измерениями, а не выбором архитектурных терминов.

### Что запрещено

```text
boxed dynamic object for every Nat in hot path;
full passport attached to scalar in tight loop;
hidden heap allocation in pure numeric kernel;
virtual dispatch per element;
metadata-interleaved dense numeric array.
```

### Что разрешено

```text
unboxed values;
dense arrays;
SoA/AoS layout decisions;
aligned buffers;
region descriptors;
proof/passport-erased kernel code.
```

### Проверка закона

Fast/runtime layer must specify memory representation and metadata placement.

### Влияние

```text
native compilation;
GPU backend;
SIMD;
DB columnar execution;
HPC kernels.
```

---

## 15. GPU только batch-first

### Формулировка

GPU execution should be batch-first. Small scalar operations should not be offloaded to GPU unless explicitly justified.

### Зачем это нужно

GPU has high throughput but nontrivial launch and transfer costs. DLM must avoid mathematically elegant but hardware-inefficient GPU use.

### Что запрещено

```text
GPU launch per small scalar operation;
CPU↔GPU round trip after every small step;
proof checking in GPU kernel;
trust-level branching per GPU thread;
unbatched tensor operations with heavy metadata transfer.
```

### Что разрешено

```text
batch fusion;
keep data GPU-resident;
compact GPU buffer descriptors;
kernel batching;
GPU eligibility via capabilities;
materialize only when explicit.
```

### Проверка закона

GPU scheduler must prefer batching and diagnose inefficient offload plans.

### Влияние

```text
GPU backend;
runtime scheduler;
capability routing;
performance model;
materialization bridge.
```

---

## 16. Location — часть паспорта

### Формулировка

Location is semantic runtime-control data and must be part of passport/descriptor model.

```text
local
remote
GPU
distributed
checkpointed
materialized
```

### Зачем это нужно

A value on GPU is not the same operational object as a local CPU value, even if mathematical type is same.

### Что запрещено

```text
using GPU value as local value without copy/materialization;
printing remote value directly;
checkpoint restore without location validation;
ignoring location in scheduling;
location changes without history event.
```

### Что разрешено

```text
LocationContext;
MaterializationBridge;
copy_to_gpu/copy_from_gpu;
remote checkpoint descriptors;
location-aware capabilities.
```

### Проверка закона

Location-changing operations require explicit history and bridge/profile rules.

### Влияние

```text
runtime;
GPU;
remote execution;
checkpoint/restore;
cluster scheduling.
```

---

## 17. Materialization — явный bridge

### Формулировка

Any transition from remote/GPU/symbolic/compressed/checkpointed object to local concrete value must be explicit materialization bridge.

### Зачем это нужно

Materialization changes operational status, cost, location and sometimes trust/provenance. It cannot be implicit.

### Что запрещено

```text
Remote<Nat> -> Nat silently;
GpuBuffer<T> -> LocalArray<T> silently;
CompressedNat -> PrintableNat silently;
SymbolicTerm -> Value silently;
Checkpoint -> RestoredRegion without bridge.
```

### Что разрешено

```text
materialize(...);
copy_from_gpu(...);
restore_checkpoint(...);
explicit BridgeKind::Materialize;
history event + cost/trust/location update.
```

### Проверка закона

Any location/representation boundary must require materialization profile.

### Влияние

```text
runtime correctness;
GPU/remote execution;
bridge theory;
cost model;
audit.
```

---

## 18. Cost-class — часть модели

### Формулировка

CostClass is semantic and operational metadata. It must guide checking, scheduling and optimization.

### Зачем это нужно

Not all values/proofs are equal. Some are trivial, compressed, symbolic, proof-required, runtime-costly, GPU-worthy or uncomputable.

### Что запрещено

```text
ignoring cost in scheduler;
treating proof-cost as runtime-cost;
decimal-printing BigNat without cost/capability check;
GPU offload without cost justification;
normalization without step bounds.
```

### Что разрешено

```text
CostClass in Passport;
proof cost vs runtime cost separation;
normalization step limits;
GPU batch cost model;
compile-time cost reports.
```

### Проверка закона

New expensive operations must declare cost behavior and bounds if applicable.

### Влияние

```text
optimizer;
runtime scheduler;
proof checking;
normalization;
GPU batching;
performance diagnostics.
```

---

## 19. Оптимизация должна быть verified

### Формулировка

Compiler optimizations must be either verified, justified by explicit preservation contract, or clearly marked as unchecked/unsafe/lower-assurance.

### Зачем это нужно

DLM cannot be proof-aware at source level and then silently trust unverified lowering transformations.

### Что запрещено

```text
optimization that changes observable semantics under the declared profile;
proof/passport erasure without erasure report;
rewrite claimed as verified without checked evidence or a verified pass;
trust/capability removal without verified transform;
compiler pass that silently drops taint/history needed for audit.
```

### Что разрешено

```text
PassInvariant;
OptimizationCertificate;
VerifiedRewrite;
ErasureReport;
unchecked optimization with honest status downgrade.
```

### Проверка закона

Every optimization pass must produce pass report and preservation statement.

### Влияние

```text
compiler backend;
FastIR;
LLVM/MLIR lowering;
proof/passport erasure;
release-fast safety.
```

### Сохраняй наблюдаемое поведение

Для допустимых входов и явно согласованных предположений требуется:

\[
\operatorname{Obs}(P,x)=\operatorname{Obs}(P',x).
\]

Учитываются результат, порядок внешних действий, определённые ошибки, ленивость ветвей и незавершение. Заранее указывается, рассматривается ли нехватка физических ресурсов; при предположении достаточных ресурсов стоимость анализируется отдельно.

Математическое равенство не разрешает менять промежуточные переполнения, округление или порядок отказов. `monus(M+1,1)` при максимальном машинном `M` нельзя заменить на `M`, если исходное сложение обязано завершиться ошибкой. Чистое неиспользуемое вычисление нельзя удалять, пока оно может наблюдаемо ошибиться или не завершиться.

Fusion `map(g,map(f,xs))` меняет чередование вызовов `f` и `g`; нужны достаточные доказанные условия либо сохранение исходного наблюдаемого порядка. То же относится к векторизации и параллельным редукциям.

Свёртка констант, удаление ветвей, inlining, специализация, unboxing, хвостовые вызовы, удаление проверок и escape analysis имеют отдельные предпосылки. Отрицательные тесты подтверждают отказ от преобразования при их нарушении.

Pass report и preservation contract документируют основание доверия, но не являются самостоятельным доказательством. Статус `verified` требует проверенного свидетельства или доказанной корректности прохода в указанной модели; остальные проходы остаются видимой частью trusted base.

---

## 20. Кэшировать нужно checked смысл

### Формулировка

Caching must be based on checked semantic identity, not raw text alone.

### Зачем это нужно

String/text cache is fragile. DLM must cache proof/audit/compiler artifacts by semantic fingerprint, dependency graph and trusted base state.

### Что запрещено

```text
cache compiled kernel only by filename;
reuse proof certificate after axiom registry changed;
reuse theorem after dependency fingerprint changed;
reuse optimized code after capability/trust/location changed.
```

### Что разрешено

```text
CheckedMeaningFingerprint;
TrustedBaseFingerprint;
DependencyGraphFingerprint;
PassReportFingerprint;
CompilerArtifactFingerprint.
```

### Проверка закона

Caches must include all semantic dependencies and trusted base fingerprints.

### Влияние

```text
incremental build;
proof cache;
compiled kernel cache;
package manager;
CI/release artifacts.
```

### Полный ключ артефакта

Ключ учитывает исходный и проверенный смысл, транзитивные зависимости, версию семантики, логические правила, компилятор, target, runtime, флаги, ABI/layout, профиль гарантий, числовой профиль и разрешённые аппаратные возможности. Внутрисессионный ID не является устойчивым ключом.

Изменение значимой зависимости инвалидирует запись. Подпись и hash подтверждают происхождение или целостность; они не заменяют логическую проверку сертификата.

---

## 21. Trusted base всегда видна

### Формулировка

Every axiom, oracle, unsafe assumption, soundness bridge, reflection assumption, consistency assumption and trusted compiler component must be visible in audit.

### Зачем это нужно

Hidden trusted base is the enemy of DLM.

### Что запрещено

```text
hidden builtin axiom;
implicit soundness bridge;
compiler pass trusted without report;
reflection assumption not in audit;
unsafe external source not marked;
stdlib theorem without trusted base entry.
```

### Что разрешено

```text
AxiomRegistry;
TrustedBaseClosure;
SoundnessBoundaryLedger;
MetatheoryFoundationExitReport;
compiler trusted components list.
```

### Проверка закона

`dlm explain/audit` must be able to show trusted base summary.

### Влияние

```text
proof trust;
stdlib;
compiler;
runtime unsafe zones;
external review.
```

### Разделяй доверие к доказательству и исполнению

Проверка исходного доказательства не доказывает корректность исполняемого файла. Для C backend отдельно показываются frontend DLM, lowering/генератор C, runtime DLM, внешний C-компилятор и системная среда в той мере, в какой от них зависит гарантия.

Для каждого сертифицированного свойства фиксируются область проверки, предположения и доверенные компоненты. Безопасная FFI-обёртка не устраняет доверие к сторонней библиотеке. Статическая capability не является изоляцией недоверенного машинного кода.

---

## 22. Любой результат объясним назад

### Формулировка

Every important result must be traceable backward to its source, proof, assumptions, transforms, runtime observations and trusted base.

### Зачем это нужно

DLM exists to make results explain themselves.

### Что запрещено

```text
result without provenance;
compiled artifact without semantic source;
runtime result without execution/materialization trace;
theorem without proof/axiom status;
optimization without pass report;
cache hit without dependency fingerprint.
```

### Что разрешено

```text
explain;
audit;
history chain;
fingerprint chain;
source spans;
proof certificates;
pass reports;
runtime descriptors.
```

### Проверка закона

Each new artifact type should answer:

```text
what is it?
where did it come from?
what assumptions were used?
what transformations happened?
what trust does it have?
```

### Влияние

```text
all DLM subsystems;
AI audit;
debugging;
formal review;
production traceability.
```

---

## 23. Архитектура должна быть AI-agent-friendly

### Формулировка

DLM must be structured so AI agents can safely extend it without accumulating hidden soundness debt.

### Зачем это нужно

The project is actively developed through iterative AI-assisted patches. The architecture must resist accidental degradation.

### Что запрещено

```text
large monolithic files with mixed concerns;
implicit invariants only in human memory;
features without tests/docs;
copy-paste rule lists in checker/soundness/audit;
stringly-typed unresolved references deep in pipeline;
no clear patch definition of done.
```

### Что разрешено

```text
small modules;
explicit laws;
test matrix;
docs per feature;
readiness deltas;
centralized invariants;
pass boundaries;
clear diagnostics.
```

### Проверка закона

Every patch should include:

```text
what changed;
what invariant is introduced;
what tests cover it;
which acceptance criteria are now satisfied, with evidence;
what remains open.
```

### Влияние

```text
project maintainability;
future agents;
review quality;
roadmap discipline;
technical debt control.
```

---

## 24. Proof kernel должен быть минимальным

### Формулировка

The trusted proof kernel must be as small, explicit and auditable as possible. Rich tactics and automation must elaborate down to kernel-checkable proof terms/certificates.

### Зачем это нужно

Large trusted kernel means large soundness risk. DLM should trust little and audit much.

### Что запрещено

```text
tactics directly create theorem without kernel path;
rewrite automation trusted as primitive proof;
reflection automation bypasses kernel;
large hidden proof rules;
stdlib theorem without certificate/axiom accounting.
```

### Что разрешено

```text
small KernelRule enum;
ProofTerm;
StaticProof;
ProofCertificate;
CertificateAudit;
tactic reports that close through kernel/certificate path.
```

### Проверка закона

Every new proof-producing feature must state whether it is:

```text
kernel rule;
tactic elaboration;
axiom admission;
certificate import;
unchecked/open obligation.
```

### Влияние

```text
soundness;
proof assistant layer;
stdlib;
external review;
machine-checked future spec.
```

### Независимая проверка доказательного объекта

Поиск и проверка доказательства разделены. Тактики, нормализатор, SMT и синтезатор предлагают структурированный ProofTerm или DAG. Минимальное ядро проверяет суждение:

\[
\Sigma;\Gamma\vdash p:\varphi.
\]

`Sigma` содержит определения и разрешённые аксиомы, `Gamma` — локальные гипотезы. Проверяются области действия, типы, подстановки и условия индукции.

Первый набор правил включает рефлексивность, симметрию и транзитивность равенства, конгруэнтность, подстановку, введение/устранение логических связок, универсальное введение и применение, индукцию по натуральным и спискам, обращение к явно объявленной аксиоме. Новое правило требует точных предпосылок и отрицательных тестов.

Сертификат содержит версии формата и логических правил, утверждение, необходимые определения, доказательный терм/DAG, зависимости и использованные аксиомы. Достаточно этих данных для проверки без повторного запуска исходной тактики. Строковая трасса, совпадение паспорта и пересчитанный fingerprint не доказывают допустимость логических шагов.

Нормализация выдаёт проверяемое свидетельство преобразования. Для линейных неравенств проверяются допустимые комбинации ограничений; целочисленные правила отделяются от рациональных. Ответ внешнего решателя не становится `StaticProof` без kernel-check.

Мутационные тесты меняют правило, предпосылку, гипотезу, область переменной, индукционный шаг и аксиому; поддельный сертификат отвергается даже после пересчёта hash. Исчерпание бюджета возвращает неопределённость проверки, а не логическое опровержение.

---

## 25. Если максимум невозможен — честное понижение статуса

### Формулировка

If DLM cannot prove the strongest status, it must honestly downgrade the object/status instead of pretending success.

### Зачем это нужно

This is the honesty law. DLM should be precise not only when it succeeds, but also when it cannot fully justify something.

### Что запрещено

```text
claiming Checked when only RuntimeChecked;
claiming StaticProof when only RuntimeWitness;
claiming Verified when Open;
claiming Conservative when not proven;
claiming clean when axiom/oracle/unsafe taint exists;
claiming optimized-verified when optimization is unchecked.
```

### Что разрешено

```text
Open;
Rejected;
Assumed;
RuntimeChecked;
Axiom;
Oracle;
Unsafe;
UncheckedOptimization;
PartialAudit;
OpenObligation.
```

### Проверка закона

Every report/status enum should have non-perfect states and use them honestly.

### Влияние

```text
all audits;
compiler;
runtime;
proof kernel;
trusted base;
production reliability.
```

### Независимые статусы свойств

`UnknownWithinBudget` не означает ложность утверждения. Открытое завершение не обнуляет доказанную безопасность памяти; доказанное завершение не закрывает открытое переполнение. Пользователь может постепенно усиливать контракты работающей программы.

Невозможно гарантировать полное автоматическое решение завершения и произвольных спецификаций. Допустимы решаемые фрагменты, пользовательские инварианты, runtime-проверки и явные предположения с сохранением их точного статуса.

---

## 26. Ownership / Alias / Lifetime — часть runtime-семантики

### Формулировка

Every runtime region, buffer, tensor, object handle or external resource must have explicit ownership, aliasing, mutation and lifetime semantics.

```text
Owned<T>
Borrowed<T>
Shared<T>
Mutable<T>
NoAlias<T>
ReadOnly<T>
GpuOwned<T>
RemoteOwned<T>
Checkpointed<T>
Expired<T>
```

### Зачем это нужно

DLM не сможет быть одновременно быстрым и безопасным без явных правил владения. Dense buffers, zero-copy, SIMD, GPU reuse, remote execution and checkpoint/restore требуют не только типа и паспорта, но и ответа на вопросы:

```text
кто владеет region;
кто может читать;
кто может писать;
есть ли alias;
жив ли handle;
можно ли переиспользовать buffer;
можно ли безопасно fused/parallel mutate;
можно ли передать ownership на GPU/remote worker.
```

### Что запрещено

```text
mutating shared region without exclusive capability;
zero-copy alias without alias contract;
GPU buffer reuse while CPU alias is mutable;
remote handle used after invalidation;
checkpoint restore over live mutable aliases;
NoAlias optimization without ownership proof;
FFI pointer escaping without lifetime boundary;
use-after-free hidden behind runtime descriptor.
```

### Что разрешено

```text
RegionOwnership;
BorrowScope;
AliasPolicy;
LifetimeToken;
NoAliasCapability;
ExclusiveMutationCapability;
ReadOnlySharedCapability;
MoveToGpuBridge;
MoveToRemoteBridge;
CheckpointOwnershipTransfer;
explicit invalidation event in history.
```

### Проверка закона

Every runtime region and external handle must answer:

```text
who owns it?
who may read it?
who may mutate it?
can it be aliased?
when does it expire?
which bridge changes ownership?
which audit event records the change?
```

### Влияние

```text
runtime safety;
zero-copy execution;
SIMD/vectorization;
GPU buffer reuse;
remote execution;
checkpoint/restore;
FFI safety;
native backend optimization.
```

### Проверяй владение по всем путям управления

Для региона `r` действующее исключительное право изменения исключает несовместимые активные чтения и записи других заимствований. Непересекающиеся диапазоны могут разделяться только при обоснованной непересекаемости.

Анализ охватывает создание, move, заимствование, частичное перемещение полей, захваты замыканий, возврат и освобождение по графу управления, включая ранние выходы, ошибки и отмену. Возвращаемое замыкание владеет захватами либо имеет допустимый внешний lifetime.

Владелец, разделяемое чтение, исключительное изменение, разделяемое долговременное владение и внешний handle — разные категории. `Shared` само по себе не разрешает mutation. Reference counting не освобождает циклы; для них требуется явная политика, например арена с индексами или отдельно определённый механизм.

Освобождение памяти и завершение внешней операции различаются. Дескриптор освобождается на всех предусмотренных путях, а ошибки `flush`/явного завершения доступны прикладному коду. Поведение при abort и исчерпании физических ресурсов задаётся runtime-политикой, без необоснованного обещания очистки после аварии ОС.

Регионы вызовов и продвижение возвращаемых графов не заменяют статическую модель владения. Их накладные обходы измеряются отдельно.

---

## 27. Stable ABI / Layout contract

### Формулировка

Low-level runtime representation must have explicit, versioned and auditable layout contracts.

```text
Logical type is not physical layout.
Physical layout must be declared.
Layout changes must be versioned.
Boundary layouts must be stable or explicitly migrated.
```

### Зачем это нужно

DLM может иметь богатую верхнюю семантику, но native backend, FFI, GPU kernels, serialization, compiled artifact cache and cross-version packages требуют точного физического представления.

Без layout law невозможно надёжно делать:

```text
LLVM/MLIR lowering;
versioned external ABI profiles (C, supported C++ boundaries, GPU);
serialized proof/runtime artifacts;
binary cache;
zero-copy mapped buffers;
columnar execution;
checkpoint portability;
package compatibility.
```

### Что запрещено

```text
implicit layout for boundary types;
ABI determined by compiler accident;
serialized artifact without layout version;
GPU kernel assuming undocumented stride/alignment;
FFI using semantic type without physical representation;
cache reuse after layout-affecting change;
checkpoint restore across incompatible layout silently.
```

### Что разрешено

```text
LayoutId;
LayoutVersion;
AbiProfile;
AlignmentContract;
StrideContract;
EndiannessContract;
SerializationSchema;
MigrationBridge;
LayoutFingerprint;
layout-aware cache key.
```

### Проверка закона

Every object crossing native, GPU, FFI, serialization, package or checkpoint boundary must declare:

```text
dtype;
shape;
layout;
alignment;
stride/order;
endianness if relevant;
version;
compatibility or migration rule;
fingerprint.
```

### Влияние

```text
native compilation;
FFI;
GPU kernels;
serialized artifacts;
package manager;
checkpoint/restore;
binary cache;
reproducible builds.
```

### Ограничь область стабильности

Стабильность требуется на объявленных внешних границах. Внутреннее представление можно менять, если оно не утекло в ABI и изменение инвалидирует зависимые артефакты. Нельзя предполагать стабильный Rust ABI или переносимость C++ ABI без конкретного поддерживаемого профиля.

Для FFI дополнительно фиксируются размер, nullable, кодировка, владельцы выделения/освобождения и срок действия указателей. Неиспользуемые к конкретному объекту свойства, например shape скалярного handle, помечаются неприменимыми.

---

## 28. Build Modes / Assurance Levels

### Формулировка

Every DLM build, run, check, explain, audit and compile mode must explicitly declare its assurance profile: what is checked, what is erased, what is retained, what is trusted and what remains open.

Гарантии многомерны:

\[
A=(A_{\mathrm{types}},A_{\mathrm{memory}},A_{\mathrm{arith}},
   A_{\mathrm{termination}},A_{\mathrm{spec}},A_{\mathrm{backend}}).
\]

Дополнительно отражаются эффекты и ресурсы. Для каждого свойства указываются область действия, статическое доказательство, runtime-проверка, предположение, неизвестность либо неприменимость. Это не единая линейная шкала: `BuildMode` выбирает политики, но не доказывает свойства.

### Целевые режимы и политики

```text
research
  Allows OpenObligation and experimental laws.
  Must report incomplete enforcement.

debug-audit
  Keeps full proofs, passports, spans, history and pass reports.
  Retention policy is explicit; dynamic history may be streamed or bounded.

release-safe
  Erases hot-path proofs but keeps audit fingerprints, trusted-base fingerprints and reproducibility metadata.
  Intended production policy; availability requires separate implementation evidence.

release-fast
  May erase more metadata or use more aggressive optimizations under explicit contracts.
  Cannot silently remove required safety checks or change arithmetic semantics.
  Unchecked transforms remain visible in assurance/trusted-base reports.

certified
  Declares the exact certified properties, artifact boundary and trusted base.
  Obligations inside that scope are closed by checked evidence or explicit policy-permitted assumptions.
  Source certification does not imply machine-code certification.
```

### Зачем это нужно

DLM должен быть честным не только по объектам, но и по режимам. Один и тот же проект может проверяться для исследования, аудита, production, high-performance execution or certified release. Эти режимы не должны смешиваться.

### Что запрещено

```text
release-fast claims certified guarantees;
unchecked optimization hidden in release-safe;
debug-only proof data required for production correctness;
research OpenObligation exported as verified theorem;
mode-specific erased metadata not recorded;
cache shared across incompatible assurance modes.
```

### Что разрешено

```text
AssuranceProfile;
BuildMode;
ErasurePolicy;
RetainedMetadataPolicy;
OptimizationAssurance;
ModeFingerprint;
Mode-specific audit report;
status downgrade by mode.
```

### Проверка закона

Every command and compiled artifact must be able to report:

```text
mode;
per-property assurance profile and certification scope;
trusted base;
erasure policy;
optimization policy;
open obligations;
retained audit links;
cache compatibility.
```

### Влияние

```text
CLI;
compiler backend;
CI/release process;
package publishing;
production deployments;
research experiments;
audit reproducibility.
```

### Условие применения названия режима

Это целевые политики, а не перечень уже доступных CLI-флагов. Неопределённая область сертификации, неучтённый внешний компилятор или непроверенный проход не позволяют объявить весь native-артефакт `certified`. Переключение режима не меняет наблюдаемую семантику без отдельного явно выбранного числового или unsafe-контракта.

---

## 29. Bounded Normalization / Termination Budget

### Формулировка

Normalization, symbolic evaluation, proof search, tactic execution, rewrite, desugaring, optimization and reflection expansion must be bounded, staged and auditable.

```text
No unbounded normalization without explicit budget.
No proof search without fuel/strategy report.
No rewrite loop without termination guard.
No reflection expansion without depth/level boundary.
```

### Зачем это нужно

A formally rich language can be logically correct and still unusable if checker, normalizer or proof search can diverge. DLM must distinguish:

```text
success;
rejected;
unknown within budget;
timeout;
fuel exhausted;
requires stronger proof;
requires explicit axiom/assumption;
requires manual theorem.
```

### Что запрещено

```text
unbounded normalization by default;
rewrite rules without loop guard;
recursive macro/desugaring expansion without depth limit;
proof search that silently times out as failure;
reflection expansion crossing meta-level without budget;
optimizer pass that may not terminate without report;
normalization result depending on nondeterministic rule order.
```

### Что разрешено

```text
NormalizationFuel;
ProofSearchBudget;
RewriteStrategy;
TerminationGuard;
DepthLimit;
StepLimit;
TimeoutStatus;
UnknownWithinBudget;
NormalizationReport;
stable deterministic rule order.
```

### Проверка закона

Every potentially expanding/checking/searching pass must declare:

```text
budget type;
default limit;
what happens when budget is exhausted;
whether result is Rejected, Unknown, OpenObligation or Partial;
how the report appears in explain/audit;
how deterministic replay is performed.
```

### Влияние

```text
checker reliability;
proof assistant layer;
rewrite engine;
macro/desugaring system;
reflection guard;
optimizer;
CI stability;
AI-agent patch safety.
```

### Не смешивай бюджет проверки с завершением программы

Fuel checker, proof-search и нормализатора ограничивает работу инструментов; он не доказывает завершение пользовательской программы. Бюджет применяется и к проверке предъявленного proof DAG, специализации обобщений и расширению IR.

Некорректный или чрезмерно глубокий пользовательский ввод обрабатывается диагностикой и контролируемым отказом, без panic или неконтролируемого переполнения стека. Используются итеративные обходы, depth/size limits и отмена. Большой резервируемый стек не считается окончательным решением.

Детерминированное воспроизведение опирается на версии правил, стратегию и счётчики шагов. Wall-clock timeout фиксируется отдельно: его срабатывание зависит от среды и не является воспроизводимым логическим результатом.

---

## 30. Операционная семантика едина для всех исполнителей

### Формулировка

Checker, интерпретатор, Typed Core и backend должны соответствовать одной нормативной семантике. Для каждой конструкции фиксируются типизация, порядок вычисления, эффекты, ошибки, поведение ресурсов и lowering в IR.

### Зачем это нужно

Одинаковый успешный результат не означает эквивалентность, если различаются вывод, ошибка, достижимость ветви или момент освобождения ресурса.

### Что запрещено

```text
неявно менять порядок вычисления аргументов;
вычислять невыбранную условную ветвь с наблюдаемой ошибкой или эффектом;
считать crash runtime допустимой ошибкой пользовательской программы;
скрывать различия backend за одинаковым типом результата.
```

### Что разрешено

```text
единая модель состояния, ресурсов и следа событий;
аргументы слева направо;
ленивые условные ветви;
явные исходы успеха, ошибки и незавершения;
раздельные политики физических ограничений и логических обязательств.
```

### Проверка закона

Наблюдаемое поведение включает результат, внешние действия, определённые ошибки, незавершение и предусмотренные события ресурсов. Ожидаемый отказ операции представляется типизированным `Result`, отсутствие значения — `Option`; нарушение проверяемого условия, арифметическая ошибка и исчерпание физических ресурсов имеют определённые политики. Дефект реализации выделяется отдельно.

Для каждой новой конструкции сопоставляются checker, интерпретатор и компилятор; поддержка только частью backend явно ограничивает доступные цели. Дифференциальные тесты сравнивают также ошибку, порядок эффектов и прекращение исполнения.

### Влияние

```text
language specification;
interpreter/compiler parity;
error model;
IR transforms;
resource semantics.
```

---

## 31. Чистота, завершение и машинная определённость независимы

### Формулировка

Контракт функции задаёт типы входа/выхода, эффекты, предусловие, постусловие, статус завершения и неуспешные исходы:

\[
C_f=(\tau_{\mathrm{in}},\tau_{\mathrm{out}},\varepsilon,P,Q,T,F).
\]

`Pure`, структурное завершение, отсутствие машинных ошибок и доказанная спецификация не взаимозаменяемы. Термин `total` допустим только с явно определённым смыслом; структурное уменьшение аргумента само по себе не доказывает успешность машинного исполнения.

### Зачем это нужно

Разбор может всегда завершаться с `Result`; чистое сложение может переполниться; безопасный сервер может намеренно не завершаться. Универсальный язык должен выражать все эти случаи.

### Что запрещено

```text
Pure => NoOverflow;
TerminationProof => достаточность физического стека или памяти;
total => доказано произвольное постусловие;
запрещать все долгоживущие процессы ради завершения proof core;
считать timeout исполнения доказательством незавершения.
```

### Что разрешено

Конечные обходы с границами, порядком, аккумулятором, ранним выходом, ошибками, инвариантом и мерой уменьшения; потенциально бесконечные процессы с явным контрактом; проверка инвариантов и безопасности каждого конечного префикса; доказательные вычисления в подходящем завершающемся фрагменте.

### Проверка закона

Завершение и машинная определённость образуют отдельные обязательства. Для серверов проверяются допустимость переходов, каждый шаг и очистка при отмене, без требования завершения всей службы.

Для поддерживаемых прямых и взаимных хвостовых вызовов задаётся гарантия устранения стека; подходящие рекурсивные обходы преобразуются в циклы. Если гарантия неприменима, указываются границы и определённое поведение при исчерпании стека. Большой резерв стека не заменяет эту модель.

### Влияние

```text
function contracts;
termination checker;
event loops/services;
tail calls;
runtime failure policy.
```

---

## 32. Математическая и машинная арифметика имеют разные контракты

### Формулировка

Математический домен, представление и семантика операций задаются отдельно. Согласно исходному документу, исполняемые `Nat`/`Int` после 0.86 ограничены `u128`/`i128`; символическое тождество не доказывает допустимость промежуточных операций в этих диапазонах.

\[
P(x)\land\operatorname{Defined}_{128}(e,x)
\Rightarrow Q(x,\operatorname{eval}_{128}(e,x)).
\]

### Зачем это нужно

Проверенная математическая формула должна сохранять смысл при реальном исполнении. Отдельно устанавливаются диапазоны результата и всех промежуточных значений.

### Что запрещено

```text
подменять математические целые ограниченным типом без обязательств;
скрыто переключать checked/wrapping/saturating арифметику;
менять Nat/Int на автоматический BigInt без решения о совместимости;
объявлять ограниченную дробь неограниченной рациональной арифметикой;
перегруппировывать Float или включать fast-math без подходящего контракта.
```

### Что разрешено

Проверяемые, модульные и насыщаемые операции с различимыми контрактами; анализ диапазонов и отношений между переменными; предусловия и инварианты; большие целые; ограниченная рациональная модель с ошибками либо рациональные числа на больших целых.

Для канонической рациональной формы `p/d` требуется `d > 0` и `gcd(abs(p),d) = 1`. Для Float задаются формат, округление, `NaN`, бесконечности, знаковый ноль, FMA, преобразования и воспроизводимость. Гарантия границы численной ошибки отличается от точного равенства.

### Проверка закона

Проверяются ноль, границы, переполнения промежуточных сумм и произведений, отрицание минимума знакового типа, `MIN / -1`, деление на ноль, величины сдвига, сужение разрядности и `Nat -> Int`. Для ширины `w` результат signed-операции лежит в `[-2^(w-1), 2^(w-1)-1]`, unsigned — в `[0, 2^w-1]`; правила вычитания, включая `monus`, сохраняют объявленный смысл.

Используются исчерпывающая уменьшенная 8-битная модель, структурированные и случайные тесты больших разрядностей, независимая модель на больших целых и сравнение backend. Удаление runtime-проверки требует доказательства именно соответствующего машинного условия.

### Влияние

```text
arithmetic semantics;
refinement/range analysis;
numeric lowering;
proof transfer;
optimization correctness.
```

---

## 33. Обобщения сохраняют типы, законы и стоимость специализации

### Формулировка

Обобщённые алгоритмы и пользовательские параметризованные типы должны иметь однозначные ограничения операций, правила владения и физического представления. Наличие операции, заявленные алгебраические законы и доказательства этих законов — разные сведения.

### Зачем это нужно

Проверенная сортировка не может предполагать корректный порядок только потому, что доступна функция сравнения. Быстрый числовой алгоритм не должен требовать неограниченной шаблонной метасистемы.

### Что запрещено

```text
неоднозначное разрешение реализаций;
неявно принимать произвольное сравнение за доказанный порядок;
терять эффекты переданной функции;
бесконечно порождать специализации;
скрывать перемещения полей в pattern matching.
```

### Что разрешено

Параметризованные записи, именованные варианты, рекурсивные типы, проверка полноты и недостижимых ветвей, явные copy/move; ограничения равенства, порядка, hash, копирования и сериализации; параметризация высших порядков по эффектам; специализация горячего кода и разделяемая реализация там, где она выгоднее.

### Проверка закона

Один алгоритм работает на нескольких пользовательских типах. Числовая специализация избегает ненужного динамического вызова внутри цикла. Учитываются число экземпляров, рекурсивное расширение, время компиляции и размер кода; исчерпание бюджета диагностируется отдельно от ошибки типов. Предположения о законах операций остаются в контракте и trusted base результата.

### Влияние

```text
generic algorithms;
algebraic data types;
effect polymorphism;
specialization;
code-size control.
```

---

## 34. Коллекции, текст, байты и пути имеют точные модели

### Формулировка

Логическое назначение, представление, единицы индексации и стоимость операций должны быть явно разделены. Региональные метаданные плотных данных масштабируются по числу регионов, а не дублируются на каждом элементе.

### Зачем это нужно

Массив указателей не равен плотному массиву чисел; копирующее добавление может сделать построение квадратичным. Системный путь и двоичные данные не обязаны быть корректным UTF-8-текстом.

### Что запрещено

```text
обещать O(1) или zero-copy только на основании названия типа;
выделять отдельный heap-объект на каждую операцию плотного числового прохода;
считать байт, Unicode-скаляр и графему одним индексом;
терять исходные байты пути из-за неявной перекодировки;
использовать повторную конкатенацию как единственный способ строить большой текст.
```

### Что разрешено

| Структура | Обязательный смысл контракта |
|---|---|
| Персистентный список | Добавление в начало, хвост и стоимость разделения структуры |
| Плотный вектор | Значения в буфере, индексация, изменение размера |
| Срез | Заимствованный диапазон без копирования payload, lifetime владельца |
| Строитель буфера/текста | Политика роста и амортизированная стоимость |
| Хеш-таблица | Условия среднего случая и поведение коллизий |
| Упорядоченное дерево | Закон порядка и стоимость операций |
| Арена графа | Владелец, lifetime и валидность индексов/handle |
| Байты | Произвольные двоичные данные |
| Текст | Кодировка, границы и единицы индексации |
| Системный путь | Сохранение платформенного представления |
| ABI-строка | Кодировка, длина/терминатор, null и lifetime |

### Проверка закона

Построение вектора последовательными добавлениями имеет амортизированную линейную стоимость. Срез не копирует payload; числовой буфер содержит значения. Для `n` фиксированных машинных элементов и `m` регионов payload — `O(n)`, региональные метаданные — `O(m)`; это не оценка размера полного audit trail.

Проверяются escape analysis, перенос вместо копирования и стоимость продвижения возвращаемых графов. Длительная работа с постоянными сохраняемыми данными не накапливает временные объекты; измеряются живые объекты наряду с RSS. Асимптотика подтверждается масштабированием.

Преобразование байтов в текст явно выбирает отказ, замену некорректных последовательностей или отдельное сохранение исходных байтов. Подстроки и поиск могут использовать заимствованные представления. Пустые данные, неверная кодировка и границы Unicode проверяются отдельно.

### Влияние

```text
stdlib containers;
numeric performance;
text processing;
filesystem paths;
memory accounting.
```

---

## 35. FFI — отдельная проверяемая граница доверия и ресурсов

### Формулировка

Сырой внешний импорт описывает фактический контракт. Безопасная обёртка проверяет аргументы, создаёт ABI-представления, удерживает ресурсы, переводит ошибки и возвращает значения с точными гарантиями. Внешняя реализация остаётся видимой доверенной зависимостью.

### Зачем это нужно

FFI необходим для универсальности, но переносит в DLM внешние правила памяти, потоков, ошибок и повторного входа.

### Что запрещено

```text
неописанные ABI, размер, выравнивание или nullable;
указатель, переживший владельца, без разрешённого lifetime;
освобождение чужим allocator или повторное освобождение;
callback после окончания его состояния;
выдавать статические capabilities за sandbox внешнего машинного кода.
```

### Что разрешено

Контракт ABI, физических типов, размера, выравнивания, кодировки, null, владения, allocator/free, допустимости сохранения указателя, эффектов, ошибок и потоковых ограничений. Для callback определяются регистрация/отмена, lifetime, владелец захватов, поток вызова, reentrancy и распространение ошибок.

### Проверка закона

Тестовая внешняя библиотека принимает/возвращает числа, получает буфер, возвращает выделенную память с обязательным free, регистрирует callback и возвращает ошибки. Проверяются нормальный выход, ранний отказ, отмена регистрации и окончание lifetime. Для недоверенных расширений, требующих изоляции, применяется отдельно определённая граница исполнения.

### Влияние

```text
foreign libraries;
native ABI;
safe handles;
callback lifecycle;
trusted-base reports.
```

---

## 36. Стандартная библиотека использует общие ошибки и ресурсы

### Формулировка

Библиотечные модули развиваются в порядке зависимостей и используют согласованные типы результатов, владение, отмену, кодировки, ограничения размеров и порядок элементов. Библиотека служит проверкой выразительности языка на каждом этапе.

### Зачем это нужно

Добавление отдельных удобных функций не создаёт системный слой, если ошибки, lifetime и форматы несовместимы между модулями.

### Что запрещено

```text
скрывать ожидаемые ошибки IO как внутреннюю ошибку компилятора;
утечки на раннем выходе;
проверенная сортировка без учёта потери или дублирования элементов;
объявлять системный сценарий завершённым по наличию только console IO.
```

### Что разрешено

```text
основание: option/result, int, text, bytes, сравнение, порядок, обходы;
контейнеры: вектор, срез, builder, сортировка, поиск, словари, множества;
системный слой: файлы, каталоги, buffered IO, время, процессы, сеть;
прикладной слой: разбор форматов и сериализация с явными ограничениями.
```

### Проверка закона

Для каждого API проверяются пустой ввод, `null` там, где он допустим, границы размера, порядок, ошибки и освобождение ресурсов. Для сортировки независимо устанавливаются `Sorted(sort(xs))` и `Permutation(xs,sort(xs))` при заданном контракте сравнения.

Первый контрольный сценарий: каталог → перечисление файлов → чтение → разбор → фильтрация и сортировка → запись → отчёт об ошибках с освобождением ресурсов на всех путях. Исключения из архитектуры для каждой операции означают незрелость общего интерфейса.

### Влияние

```text
stdlib design;
application APIs;
resource composition;
data processing;
incremental assurance.
```

---

## 37. Параллелизм структурирован по времени жизни задач

### Формулировка

Задачи принадлежат области, определяющей ожидание, отмену, ошибки, lifetime заимствований и очистку. Параллелизм вводится на основе явных эффектов и владения.

### Зачем это нужно

Задача не должна переживать используемые данные или оставлять незавершённую внешнюю операцию после отмены.

### Что запрещено

```text
фоновая задача с истёкшим заимствованием;
несовместимые обращения к памяти без синхронизации;
молчаливое изменение порядка результатов и выбора ошибки;
параллельная редукция только на основании математической ассоциативности;
переиспользование async/GPU-буфера до фактического завершения операции.
```

### Что разрешено

Передача владения, совместное неизменяемое чтение, сообщения и синхронизированное изменение. Для приостановленного вычисления определяются состояние, владелец данных, завершение внешнего запроса, частичный IO и очистка при отмене.

### Проверка закона

Автораспараллеливание требует независимых обращений к памяти, совместимых эффектов, определённого порядка результатов, политики нескольких ошибок и подходящей арифметики. Для checked integer редукции и Float смена группировки проверяется отдельно.

Отмена проверяется до старта, во время ожидания, после частичного IO и одновременно с завершением. Scope не закрывается с забытыми задачами или ресурсами. Долгоживущая служба проходит проверки нескольких клиентов, лимитов, тайм-аутов и корректного завершения.

### Влияние

```text
structured concurrency;
async IO;
CPU/GPU scheduling;
service reliability;
cancellation safety.
```

---

## 38. Пакеты и инструменты используют общую семантическую инфраструктуру

### Формулировка

Экспортируемые интерфейсы включают типы, сигнатуры, эффекты, владение, предусловия/постусловия, статусы завершения и ошибок, доказательные зависимости, аксиомы и ABI при внешней границе. Зависимости фиксируются lock-файлом; сборка сохраняет toolchain и профиль гарантий.

### Зачем это нужно

Большой проект требует устойчивой идентичности объявлений, раздельной компиляции и диагностики, согласованной с фактическим checker.

### Что запрещено

```text
принимать несовместимый импорт до обнаружения проблемы в runtime;
подменять ожидаемый артефакт другим числовым или assurance-профилем;
делать отдельную приближённую систему типов для редактора;
путать подпись автора с математической корректностью;
объявлять платформу поддержанной без нативного запуска.
```

### Что разрешено

Версионированные интерфейсы и dependency graph, семантические ключи кэша, единые ID/Span/контракты для compiler, LSP и debugger; переходы к определениям, поиск использований, просмотр эффектов и открытых обязательств, профилирование функций и выделений.

### Проверка закона

Несовместимость интерфейса обнаруживается до исполнения. Изменения значимых зависимостей инвалидируют кэш. Различаются происхождение, целостность, корректность proof и доверие к машинному коду.

Диагностика сообщает нарушенное правило, известные факты, недостающее условие, источник зависимости и допустимое исправление. Для владения показываются конфликтующие использования, для эффектов — цепочка вызовов, для proof — само обязательство.

Первоначальные цели полной поддержки — Linux x86-64 и Windows x86-64 с отдельными нативными сборками и запусками. По исходному документу Windows-ветка 0.86 ещё требует подтверждения на Windows. Другие цели принимаются после проверок ABI, арифметики, выравнивания, кодировок и ресурсов.

### Влияние

```text
package interfaces;
reproducible builds;
LSP/debugger;
cross-platform support;
semantic diagnostics.
```

---

## 39. Корректность и производительность подтверждаются воспроизводимыми проверками

### Формулировка

Примеры, число тестов и один benchmark не доказывают корректность или быстродействие языка. Каждое существенное утверждение имеет область действия, метод проверки и воспроизводимое свидетельство.

### Зачем это нужно

Общий frontend может одинаково ошибаться у разных backend. RSS может расти из-за удержания страниц allocator; разная числовая семантика искажает сравнение скоростей.

### Что запрещено

```text
выдавать проходящие тесты за доказательство всего компилятора;
мерить накладные расходы при разных арифметике и правилах ошибок;
объявлять утечку или её отсутствие по одному RSS;
оценивать готовность произвольными процентами;
называть архитектурное описание реализованной оптимизацией.
```

### Что разрешено

Генераторы корректных и некорректных программ; сравнение интерпретатора, backend и уровней оптимизации; сверка с нормативными правилами; арифметические граничные тесты; мутация proof; ошибки, отмена, move, замыкания и длительная работа; проверка сериализованных интерфейсов и кэша.

### Проверка закона

Измеряются числовые циклы, массивы, сортировка/поиск, построение текста, разбор данных, файлы, долгоживущая служба и обобщённая библиотека. Фиксируются задача, числовая семантика, ошибки, представление входа, toolchain, флаги, target/оборудование, размеры, прогрев и метод учёта памяти.

Метрики: время, throughput, число/объём выделений, пиковая и сохраняемая память, живые объекты, размер кода, время компиляции и распределение задержек служб. Короткие тесты повторяются с оценкой разброса; длительные оцениваются в установившемся режиме. Пороги регрессии назначаются после устойчивого baseline по классам задач.

Три обязательных контрольных проекта: обработчик файловых данных, долгоживущая сетевая служба и вычислительная библиотека над плотными массивами. Каждый содержит обычный прикладной код, участки усиленных гарантий и отчёт trusted base.

### Влияние

```text
correctness validation;
performance baselines;
memory/resource checks;
platform acceptance;
release evidence.
```

---

## 40. Математическая автоматизация расширяется через проверяемые результаты

### Формулировка

Refinement types, зависимые размеры, SMT, e-graphs, численные методы и синтез — направления развития, а не обещание поддержки текущей версией. Они опираются на Typed Core, числовую семантику, владение, бюджет и независимое ядро проверки.

### Зачем это нужно

Математические свойства могут обосновывать удаление проверок, специализацию и выбор алгоритма, если связаны с реальным машинным исполнением. Сложность поиска не должна расширять доверенную базу незаметно.

### Что запрещено

```text
SMT answer => StaticProof без проверки;
e-graph equality => машинная эквивалентность без предпосылок;
зависимый размер => отсутствие всех runtime-ошибок;
abstract_steps => точное время в миллисекундах на любом устройстве;
синтезированная программа => корректная программа без проверки;
обещать полное автоматическое решение произвольной спецификации.
```

### Что разрешено

| Направление | Обязательная граница гарантии |
|---|---|
| Refinement types | Проверяемое условие значения, явный статус и runtime-проверка при необходимости |
| Зависимые размеры, тензоры | Совместимость shape, бюджет вычисления типов, отдельные layout/lifetime |
| SMT и символьная алгебра | Проверяемое свидетельство, явные теории и аксиомы |
| E-graphs | Типизированные правила с учётом эффектов, ошибок, переполнения и округления |
| Линейность и уникальность | Правила move/borrow, включая незавершённые GPU/async-операции |
| Точная рациональная арифметика | Размер числителя/знаменателя, канонизация и политика роста |
| Интервальная арифметика | Корректное направленное округление и охват истинного значения |
| Автодифференцирование | Область дифференцируемости, правила ветвлений, числовая модель и проверка преобразования |
| Размерностные типы | Проверенные единицы и явные преобразования |
| Доказательство ресурсов | Модель абстрактной стоимости, предусловия, оценки памяти/стека/шагов |
| Темпоральная логика | Модель событий, безопасность; отдельные предположения для liveness |
| Вероятностные вычисления | Семантика распределений, явный источник случайности и статус статистического вывода |
| Синтез программ | Ограниченный поиск, бюджет, проверка реализации и её спецификации |

### Проверка закона

Автоматизация выдаёт проверяемый объект или честный статус неизвестности. Выбор самой дешёвой формы по cost model не является доказательством семантики. Границы ресурса относятся к объявленной модели, а wall-clock гарантии требуют отдельной модели среды.

Первыми развиваются машинная арифметика и refinement-условия, владение, проверяемые сертификаты и семантически корректные преобразования. Зависимые типы, тензоры, точная/интервальная арифметика, AD и синтез добавляются по готовности зависимостей. Предварительный список идей не отменяет порядок архитектурных этапов раздела 42.

### Влияние

```text
proof-guided optimization;
scientific computing;
resource contracts;
bounded synthesis;
mathematical roadmap.
```

---

## 41. Как законы применяются при проверке патчей

Патч описывает конкретное изменение поведения, затронутые законы, сохраняемые контракты и критерии готовности. Проверяются применимые пункты; неприменимость объясняется кратко. Расширение документа не требует механического изменения кода всех подсистем.

```text
Сохранены разделение слоёв и паспорта/capabilities/trust?
Проверенные типы, эффекты, ID и Span доходят до backend?
Сохранены порядок аргументов, ленивость ветвей, эффекты и ошибки?
Разделены чистота, завершение, машинная определённость и спецификация?
Доказательство проверяется независимо; аксиомы и зависимости видимы?
Proof/history не раздувают горячий код и плотные данные?
Bridge/materialization сохраняют объявленные свойства и происхождение?
Ownership/alias/lifetime соблюдаются на ошибках и отмене?
Layout/ABI/FFI и политика callback определены?
Оптимизация сохраняет машинную арифметику и наблюдаемое поведение?
Отчёт отличает proof от preservation contract и доверенного прохода?
Кэш учитывает зависимости, toolchain, target и профиль гарантий?
Нормализация, proof checking/search и специализация имеют бюджеты?
Коллекции/текст/пути сохраняют представление и контракт стоимости?
Параллельные задачи, ошибки и ресурсы ограничены scope?
Неизвестность и неполнота отражены без повышения статуса?
Есть подходящие отрицательные, граничные и платформенные проверки?
Измерения соответствуют заявлению о производительности?
Новые возможности отделены от реализованных и проверенных?
```

Минимальный отчёт: что изменилось, какой инвариант затронут, чем проверено, какие критерии закрыты, какие обязательства остаются. Открытый долг содержит границу гарантии и условие закрытия. Проходящие тесты не подменяют доказательство, а декларация долга не разрешает скрывать нарушение.

---

## 42. Готовность определяется результатами и зависимостями этапов

Изменение этих законов само по себе не повышает готовность кода и не доказывает soundness. Процентные прибавки и формула «100/100» без измеримой модели исключаются. Для подсистемы фиксируются требование, статус реализации, свидетельства проверки и открытые ограничения.

### Порядок реализации

| Этап | Конкретный результат | Условие перехода дальше |
|---|---|---|
| A. Семантическая фиксация | Правила исполнения, ошибок, арифметики и профилей гарантий | Поведение существующих конструкций описано однозначно |
| B. Typed Core | Проверенные типы, эффекты, ID и исходные позиции доходят до backend | Исходная семантика не восстанавливается эвристически |
| C. Доказательные объекты | Структурированные термы, независимый checker, явные аксиомы | Проверка без повторения поиска; мутации отвергаются |
| D. Владение и ресурсы | Move/borrow, handles, очистка ошибок и отмены | Пройдены отрицательные сценарии lifetime |
| E. Плотные данные и обобщения | Векторы, срезы, builders, параметризованные типы | Эффективные обобщённые алгоритмы и проверенная стоимость |
| F. Системный слой | Файлы, каталоги, время, процессы, FFI | Полноценный преобразователь файлов |
| G. Сеть и задачи | Службы, отмена, передача владения | Длительная работа без накопления временных ресурсов |
| H. Зрелый toolchain | Пакеты, раздельная компиляция, LSP, отладка, профилирование | Поддерживается внешний многомодульный проект |

Этапы задают зависимости, а не размер одного патча. Стандартная библиотека развивается на каждом этапе. Ближайшее основание соединяет разделение завершения и машинной определённости, Typed Core, общие ID/Span и согласованную семантику интерпретатора и компилятора. Одного расширения `std.int`/`std.text` для этого недостаточно.

### Что считается свидетельством

```text
Designed: есть нормативный контракт и критерии приёмки.
Implemented: механизм существует в конкретной версии и указан его объём.
Validated: есть воспроизводимые проверки в указанной конфигурации.
Proven: конкретное свойство доказано в объявленной модели и trusted base.
Open: зафиксированы незакрытые обязательства или неподдерживаемые случаи.
```

Это разные сведения, а не автоматическая лестница доказательности. Статусы относятся к конкретному свойству, платформе и артефакту. Результаты прежнего тестирования не объявляются результатами новой проверки.

### Историческое уточнение о v0.58

Прежний документ отмечал первый кодовый enforcement контрактов функций в v0.58.0. Эта историческая заметка не означает полного исполнения законов 5, 6, 7, 18, 25, 28 и 29. В актуальной модели `pure`, структурное завершение, машинная определённость и функциональная корректность учитываются отдельно; oracle/unsafe-зависимости сохраняются. Полнота enforcement подтверждается по каждому критерию и версии отдельно.

---

## 43. Итоговый принцип

DLM должен соединять математическую ясность с практической универсальностью: работающая программа получает предсказуемое исполнение, а её гарантии можно усиливать постепенно.

```text
Passports guide checking and scheduling; dense buffers carry data.
Proof terms justify claims; hashes identify artifacts.
History explains provenance within an explicit retention policy.
Trust records dependencies; checking does not erase assumptions.
Ownership controls lifetimes; cleanup includes errors and cancellation.
Layout defines boundaries; logical types do not imply an ABI.
Termination is separate from purity, machine safety and specification.
Optimization preserves observable behavior under stated assumptions.
Build modes select policies; assurance is reported per property.
Automation has budgets; unknown is not false and not proven.
Readiness and performance require reproducible evidence.
```

Целевой результат: согласованная семантика → проверенный Typed Core → обоснованные преобразования → безопасные ресурсы и явные ABI → эффективное исполнение с точным отчётом о гарантиях и доверенных компонентах.
