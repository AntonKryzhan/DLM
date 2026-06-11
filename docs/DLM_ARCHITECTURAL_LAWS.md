# DLM Architectural Laws — Конституция архитектуры ЯРД

## 0. Назначение документа

Этот документ фиксирует обязательные архитектурные законы DLM / ЯРД.

Это не список пожеланий и не набор красивых принципов. Это набор ограничений, которые должны удерживать язык от трёх опасных деградаций:

```text
1. Математически красиво, но soundness-размыто.
2. Формально богато, но аппаратно медленно.
3. Быстро патчится AI-агентами, но архитектурно расползается.
```

Главная формула DLM:

```text
Смысл должен быть голографическим.
Исполнение должно быть плотным.
Аудит должен объяснять результат назад.
```

Или в технической форме:

```text
Meaning-rich above.
Execution-dense below.
Audit-complete backward.
```

Эти законы применяются ко всем будущим слоям:

```text
Source / mathematical layer
IR / compiler layer
Runtime control layer
Hardware execution layer
Proof / audit layer
High-performance native compilation layer
AI-agent development workflow
```

Если новый патч временно нарушает один из законов ради MVP, это должно быть явно записано как:

```text
Technical Debt
Open Obligation
Known Incomplete Law Enforcement
```

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

Pure core языка должен быть детерминированным: одинаковый checked input даёт одинаковый result, passport summary and audit fingerprint.

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

Все effectful operations должны оставлять след в passport/history/audit.

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
NoAlloc + Pure + NoAlias => vectorization candidate;
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
trust join = worse/max trust;
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

---

## 10. History полная в audit, компактная в runtime

### Формулировка

Full HistoryChain должен сохраняться в audit/proof/explain layer. Runtime может использовать compact history descriptor or fingerprint.

### Зачем это нужно

Full history нужна для объяснимости. Но runtime не должен таскать большие цепочки в hot path.

### Что запрещено

```text
терять audit history;
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
 -> Parser
 -> RawAST
 -> HIR
 -> ResolvedHIR
 -> TypedIR
 -> ProofIR
 -> PassportIR
 -> Bridge/Policy Audit
 -> CheckedModule
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

### Зачем это нужно

C++/ASM-level speed requires dense buffers, predictable layout and no hidden allocation.

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
optimization that changes semantics without certificate;
proof/passport erasure without erasure report;
rewrite optimization without rewrite certificate;
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

### Влияение

```text
proof trust;
stdlib;
compiler;
runtime unsafe zones;
external review.
```

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
what readiness changed;
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

---

## 26. How these laws affect future patch review

Every future DLM patch should be checked against this short checklist:

```text
Does it preserve semantic layer separation?
Does it use passport/capability/trust instead of raw type-only checks?
Does it avoid carrying full proof/history into hot runtime?
Does it keep trust monotonic?
Does it preserve bridge contracts?
Does it keep effects explicit?
Does it avoid String references after resolution?
Does it preserve Span/source origin?
Does it keep runtime representation dense?
Does it keep GPU execution batch-first?
Does it make materialization explicit?
Does it produce audit/explain information?
Does it remain AI-agent-friendly?
Does it honestly downgrade status when full proof is absent?
```

---

## 27. Readiness impact

Adding these laws primarily increases architectural and fundamental readiness, not local code readiness.

```text
Metamathematical foundation:
  Local readiness:         no direct code change
  Architectural readiness: +3–5%
  Fundamental readiness:   +3–5%

Runtime / production execution:
  Local readiness:         no direct code change
  Architectural readiness: +5–8%
  Fundamental readiness:   +4–6%

High-performance native compilation:
  Local readiness:         no direct code change
  Architectural readiness: +6–10%
  Fundamental readiness:   +5–8%

AI-agent maintainability:
  Architectural readiness: +8–12%
```

---

## 28. Final principle

DLM must not become a slow language with beautiful metadata.

DLM must become a language that uses metadata to decide how to compute correctly and efficiently.

```text
Passports are not runtime weight.
Passports are semantic control surfaces.

Proof is not hot-loop payload.
Proof is compile-time justification.

History is not per-byte baggage.
History is audit-level reconstruction data.

Trust is not decoration.
Trust is a monotonic safety label.
```

The final target is:

```text
high-level mathematical clarity
+ explicit proof/trust/audit
+ compact runtime control
+ dense hardware execution
+ explainable results
```

