# Runtime / Hardware Layering Principle — DLM / ЯРД

## 1. Назначение

Этот документ фиксирует глобальный архитектурный принцип DLM:

```text
Смысл должен быть голографическим.
Исполнение должно быть плотным.
```

DLM должен хранить богатый математический смысл на верхних слоях: proof, passport, theory, trust, provenance, history, audit. Но при реальном исполнении на CPU/GPU/PCIe/remote runtime этот смысл не должен превращаться в аппаратный груз.

Правильная формула:

```text
High-level DLM:
  rich semantics, passports, proofs, theory context, history, audit.

Low-level runtime:
  compact descriptors, dense buffers, deterministic kernels, minimal transfers.
```

Паспорт — это не груз для GPU. Паспорт — это инструкция для CPU/compiler/runtime scheduler, как правильно использовать GPU, CPU, память, remote node или kernel.

---

## 2. Четыре слоя DLM

DLM должен явно разделять четыре слоя.

### 2.1. Source / Mathematical Layer

Полный смысл программы:

```text
source syntax
mathematical objects
proofs
proof terms
passports
theory context
trust levels
provenance
history chains
axiom registry
audit reports
```

Этот слой отвечает на вопросы:

```text
что это за объект;
из какой он теории;
чем он доказан;
какие assumptions использованы;
какой trust-level;
какая история вывода;
какие операции разрешены.
```

Здесь допустим богатый семантический вес, потому что этот слой нужен для проверки, объяснения, аудита и проектирования.

### 2.2. IR / Compiler Layer

Слой проверки и преобразования:

```text
HIR
ResolvedHIR
TypedIR
ProofIR
PassportIR
CoreIR
FastIR
OptimizationContractIR
```

Он отвечает за:

```text
name/theory resolution;
type checking;
proof checking;
passport inference;
bridge policy;
trust policy;
audit construction;
proof erasure;
passport erasure;
layout decisions;
optimization contracts;
backend lowering.
```

Главное правило:

```text
IR / compiler layer uses rich passports to decide what can be erased, moved, fused, batched, vectorized, compiled, scheduled, or rejected.
```

### 2.3. Runtime Control Layer

Слой управления исполнением:

```text
compact passport descriptors
capabilities
location state
buffer handles
kernel handles
scheduler metadata
device placement
batch descriptors
transfer plans
runtime witnesses
```

Этот слой не должен таскать полный математический паспорт на каждый элемент данных. Он хранит только то, что нужно для безопасного и эффективного исполнения.

Примеры compact metadata:

```text
buffer is on GPU;
buffer has shape [N, M];
buffer is aligned;
operation is pure;
operation is noalloc;
operation is vectorizable;
result can remain on device;
runtime witness is not static proof;
unsafe taint cannot be hidden.
```

### 2.4. Hardware Execution Layer

Минимальный аппаратный слой:

```text
raw memory
dense buffers
SIMD vectors
SIMT kernels
CPU loops
GPU kernels
DMA transfers
cache lines
registers
stack frames
native instructions
```

Здесь не должно быть full passport, proof checking, theorem logic или dynamic trust branching внутри hot path.

Правило:

```text
Hardware execution layer executes dense computation, not high-level metatheory.
```

---

## 3. Где начнутся проблемы

Проблемы начнутся, если смешать уровни:

```text
proof checking inside GPU kernel;
history chain per array element;
full passports across PCIe;
dynamic dispatch inside every GPU thread;
branching by trust-level inside SIMD/SIMT lanes;
small GPU tasks instead of batch kernels;
frequent CPU <-> GPU round trips;
reflection/proof construction in hot runtime loops;
heap allocation in every mathematical primitive;
full audit report attached to every scalar value;
opaque runtime objects where dense buffers are needed.
```

Это сделает язык красивым математически, но тяжёлым аппаратно.

Нельзя допустить, чтобы DLM стал языком, где математическая честность оплачивается постоянным runtime overhead.

---

## 4. Где будет сила

Сила DLM должна быть в другом.

DLM заранее понимает:

```text
this operation can safely run on GPU;
this operation cannot run on GPU;
this buffer is already on GPU;
this result does not need to return to CPU;
this proof is compile-time only;
this passport can be erased after verification;
this runtime witness cannot become StaticProof;
this unsafe taint cannot be hidden;
this tensor can be fused with another tensor;
this loop can be vectorized;
this batch can be scheduled as one kernel;
this value requires materialization before printing;
this bridge does not preserve truth;
this location transition must be explicit.
```

То есть passport model becomes a planning system:

```text
passport => compiler decision
passport => scheduler decision
passport => device placement decision
passport => erasure decision
passport => transfer decision
passport => audit decision
```

Паспорта должны стать не тормозом, а планировщиком вычислений.

---

## 5. Главный закон

```text
Rich semantics above.
Dense execution below.
```

Или точнее:

```text
Source / Mathematical Layer:
  full semantic truth, proof, passport, theory, trust, history.

IR / Compiler Layer:
  verification, erasure, optimization, bridge policy, audit.

Runtime Control Layer:
  compact descriptors, capabilities, locations, scheduling, witnesses.

Hardware Execution Layer:
  raw memory, dense buffers, kernels, minimal metadata.
```

---

## 6. Связь с proof/passport erasure

Этот принцип напрямую связан с high-performance native compilation track.

DLM должен уметь:

```text
use proof/passport/history at compile time;
verify safety and trust boundaries;
construct audit artifacts;
erase proof objects from hot runtime code;
erase full passports into compact descriptors;
compile pure kernels into dense native code;
keep audit outside the hot loop;
keep runtime witnesses separate from static proofs.
```

Правильная runtime-модель:

```text
proof-carrying compile time;
proof-erased runtime;
passport-guided scheduling;
compact runtime descriptors;
dense hardware execution.
```

---

## 7. GPU / CPU / PCIe rule

GPU/CPU execution must follow this rule:

```text
Do not move meaning through PCIe when only data is needed.
Move compact descriptors and dense buffers.
Keep full meaning in compiler/audit layer.
```

Bad design:

```text
each tensor element carries full passport;
each GPU thread branches on trust;
each kernel verifies proof obligations;
each operation copies data back to CPU for audit.
```

Good design:

```text
compiler verifies passport once;
runtime schedules batch;
GPU receives dense buffers;
kernel executes deterministic computation;
compact descriptor records location/result shape;
audit references kernel plan and passport erasure proof.
```

---

## 8. Design consequences

Future runtime/compiler patches must respect these consequences:

```text
No full Passport per scalar in hot arrays.
No HistoryChain per tensor element.
No ProofTerm checking inside kernels.
No reflection in runtime kernels.
No hidden CPU/GPU transfer.
No implicit materialization.
No trust downgrade during device transfer.
No runtime witness becoming StaticProof.
No device placement without passport/capability evidence.
```

Allowed and desired:

```text
compact runtime passport descriptor;
buffer-level passport summary;
shape-level proof;
location capability;
compile-time proof erasure;
kernel-level audit certificate;
batch/fusion plan;
explicit materialize bridge;
explicit transfer bridge;
explicit runtime witness boundary.
```

---

## 9. Integration points in roadmap

This principle must guide these future tracks:

```text
Runtime / production execution
High-performance native compilation
GPU backend prototype
Distributed execution
Remote checkpoint/restore
Proof/passport erasure
FastIR / CoreIR
MLIR / LLVM backend
Scheduler
Resource logic
```

It also affects current and future passport capabilities:

```text
can_compile_gpu_kernel
can_serialize_for_migration
can_materialize
can_copy_to_gpu
can_copy_from_gpu
can_schedule_remote
can_run_dense_kernel
can_erase_proof_runtime
can_use_compact_descriptor
```

---

## 10. Final formulation

The final architectural formula:

```text
DLM should be semantically holographic at the source level
and physically dense at the execution level.
```

More operationally:

```text
Meaning is global and explicit.
Execution is compact and local.
```

This is the only way to keep both goals:

```text
programs remain understandable;
errors remain explainable;
proof/trust remains auditable;
GPU/CPU/PCIe are not overloaded;
performance remains high;
DLM can understand better than ordinary languages where and how to compute.
```
