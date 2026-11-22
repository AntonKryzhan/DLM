# HIGH_PERFORMANCE_COMPILATION.md — DLM / ЯРД

## 1. Purpose

This document records the long-term high-performance compilation strategy for DLM / ЯРД.

The goal is not to claim that DLM will be faster than every language in every workload. That is not a meaningful compiler target: once two programs lower to the same machine instructions, performance is dominated by algorithms, memory layout, cache behavior, branch prediction, vectorization, I/O, profile data, and target hardware.

The realistic target is stronger and more precise:

```text
DLM should compile a restricted high-performance subset to native code at C/C++/Rust level,
and in domains with strong static invariants it should be able to outperform ordinary implementations
by giving the optimizer proof-derived facts that ordinary compilers usually do not know.
```

This is a late-stage engineering track. It must not be allowed to weaken the current metamathematical foundation, proof/truth boundary, passport model, or trusted-base accounting.

---

## 2. Strategic principle

DLM should not carry proof, passport, provenance, and audit metadata through every hot runtime instruction.

The intended model is:

```text
proof-carrying compile time
proof-erased runtime
```

Meaning:

```text
1. DLM uses proof/passport/history/trust data before code generation.
2. The compiler proves that a fast path is safe.
3. The proof and passport evidence are erased from hot runtime code.
4. The generated code contains only the minimal computation needed for the target.
```

A value such as:

```text
Amount<USD, minor_unit=cents, checked, non_negative>
```

may compile in audit/debug mode with full metadata, but in `release-fast` it should become a compact machine representation such as:

```text
i64
```

provided the erasure pass has a valid proof/audit certificate.

---

## 3. Required language split

High-performance compilation requires a split between the full mathematical language and a restricted fast subset.

```text
DLM-Full
  proof/passport/audit-heavy language;
  symbolic reasoning;
  reflection/metatheory;
  explainable computation.

DLM-Core
  resolved, typed, proof-aware core IR;
  suitable for transformations and checking.

DLM-Fast
  restricted high-performance subset;
  no hidden allocation;
  no runtime reflection;
  erased passports;
  native code generation.

DLM-Kernel
  ultra-small hot-kernel subset;
  direct SIMD/LLVM/ASM-oriented lowering.
```

DLM-Fast should reject or isolate constructs that cannot be compiled predictably:

```text
runtime reflection in hot paths;
dynamic proof construction inside loops;
heap allocation without explicit policy;
implicit boxing;
implicit dynamic dispatch;
arbitrary symbolic terms in numeric kernels;
unbounded BigNat in hot paths;
I/O inside pure kernels;
unchecked external input;
opaque unsafe transitions.
```

---

## 4. Compiler pipeline target

The long-term pipeline should look like this:

```text
source .dlm
  ↓
RawAST
  ↓
HIR
  ↓
ResolvedHIR
  ↓
TypedIR
  ↓
ProofIR
  ↓
PassportIR
  ↓
OptimizationContractIR
  ↓
FastIR
  ↓
ProofErasure
  ↓
PassportErasure
  ↓
Effect/Memory Optimization
  ↓
LLVM IR / MLIR / Cranelift IR / C/Rust backend
  ↓
Native object / executable / shared library
```

The key new layer is `OptimizationContractIR`:

```text
OptimizationContractIR records which mathematical facts may be used by the optimizer.
```

Examples:

```text
index is in bounds;
value is nonzero;
array is sorted;
matrix shape is fixed;
loop bound is static;
function is pure;
function allocates no memory;
arguments do not alias;
value is aligned to 32/64 bytes;
transaction batch is grouped by account;
query predicate is monotonic;
graph is acyclic.
```

---

## 5. Zero-cost passports

A zero-cost passport is a passport that affects compile-time checking and optimization, but is erased from the emitted hot code.

Example before erasure:

```text
Matrix<Float64, rows=4, cols=4, aligned=32, noalias>
```

Possible lowered representation:

```text
aligned [f64; 16]
```

Example before erasure:

```text
Index<Array<N>, proven_in_bounds>
```

Possible lowered representation:

```text
usize
```

with the bounds check removed only if the proof survives verification.

Main law:

```text
passport erasure is allowed only after the erased fact has been consumed by a verified optimization contract.
```

Erasure must itself be auditable:

```text
PassportIR + ErasureProof => FastIR
```

---

## 6. Proof-guided optimization

DLM can become faster than ordinary hand-written implementations in narrow domains when the compiler receives facts that a standard compiler cannot infer.

Potential proof-guided optimizations:

```text
BoundsProof       => remove bounds checks.
NonZeroProof      => remove divide-by-zero checks.
NoOverflowProof   => remove overflow guards.
SortedProof       => choose binary search / merge join.
AcyclicProof      => use linear topological traversal.
ShapeProof        => unroll/vectorize fixed-size tensor operations.
NoAliasProof      => enable aggressive vectorization.
AlignmentProof    => use aligned SIMD loads/stores.
NoAllocProof      => stack/arena-only lowering.
PurityProof       => common subexpression elimination and loop fusion.
AssociativityProof=> reassociation and parallel reduction.
CommutativityProof=> reordering and batch execution.
```

This is the main performance advantage of DLM:

```text
not a magical backend,
but a frontend that supplies stronger true facts to the optimizer.
```

---

## 7. Memory model requirements

Fast code requires a strong memory model.

DLM-Fast needs explicit support for:

```text
stack allocation;
arena allocation;
region allocation;
escape analysis;
move semantics;
copy elision;
packed structs;
aligned structs;
SoA/AoS layout choice;
cache-line awareness;
zero-copy slices;
no hidden allocation;
borrow/ownership-like restrictions;
noalias contracts;
NUMA placement policy;
SIMD-friendly alignment;
GPU-portable layout contracts.
```

Without this, DLM would remain a proof/audit language but would not reach C/C++ performance.

---

## 8. Effect and capability inference

The existing `CapabilitySet` should eventually feed an optimization-capability model.

Future performance capabilities:

```text
can_compile_fast;
can_erase_passport;
can_erase_proof;
can_use_stack_layout;
can_use_region_layout;
can_assume_noalias;
can_vectorize;
can_parallelize;
can_emit_simd;
can_emit_gpu_kernel;
can_emit_native_object;
can_emit_asm;
can_use_target_cpu_native;
```

Future effects:

```text
Pure;
NoAlloc;
NoPanic;
NoIO;
NoNetwork;
NoRuntimeProof;
NoReflection;
NoAlias;
ReadOnly;
Deterministic;
Vectorizable;
Parallelizable;
GpuPortable;
ConstantTime;
```

The optimizer should use these only if they are derived by checked passes, not by unchecked annotations.

---

## 9. Backend ladder

The backend should be developed in stages.

### Stage A — C/Rust backend

Purpose:

```text
get native execution quickly;
reuse existing compilers;
validate FastIR lowering;
compare behavior against interpreter/runtime.
```

### Stage B — LLVM backend

Purpose:

```text
native AOT compilation;
LTO;
PGO;
target-cpu=native;
assembly inspection;
object/library generation.
```

### Stage C — MLIR dialects

Purpose:

```text
multi-level optimization;
domain-specific lowering;
linear algebra;
database kernels;
financial batch execution;
GPU/heterogeneous targets.
```

### Stage D — Cranelift/JIT backend

Purpose:

```text
fast compilation for interactive workloads;
JIT kernel specialization;
AOT cache for generated kernels.
```

### Stage E — micro-ASM backend

Purpose:

```text
only for tiny hot kernels;
manual ABI contracts;
SIMD intrinsics;
cryptographic or numeric inner loops.
```

DLM should not start with a custom ASM backend. That is a late optimization, not a foundation.

---

## 10. Release modes

Future build modes:

```text
dlm build --debug
  preserves more runtime checks and metadata.

dlm build --audit
  emits audit artifacts and proof/passport reports.

dlm build --release
  optimized native build, safe checks retained where not proven away.

dlm build --release-fast
  proof-erased/passport-erased fast subset only.

dlm build --release-fast --verify-erasure
  requires erasure certificates.

dlm build --emit-llvm
  emits LLVM IR.

dlm build --emit-asm
  emits assembly.

dlm build --pgo-generate
  generates profile instrumentation.

dlm build --pgo-use
  consumes profile data.
```

---

## 11. Benchmarks and correctness gates

Performance claims must be benchmarked, not asserted.

Required benchmark tracks:

```text
integer kernels;
array kernels;
fixed-size matrix kernels;
ledger batch operations;
query/filter/project kernels;
serialization/deserialization;
crypto transaction validation;
policy engine evaluation;
proof-erased theorem kernels;
normalization/rewrite kernels.
```

Comparison targets:

```text
C++ -O3;
Rust --release;
Zig ReleaseFast;
C with clang/gcc -O3;
Fortran for numeric kernels where relevant.
```

Required correctness gates:

```text
differential testing against reference interpreter;
FastIR validation;
proof-erasure validation;
passport-erasure validation;
ASM inspection for hot kernels;
benchmark reproducibility metadata;
CPU feature reporting;
profile-data fingerprinting.
```

---

## 12. Best domains for DLM high-performance compilation

DLM-Fast is especially promising in domains where strong invariants remove runtime uncertainty:

```text
financial batch processing;
ledger reconciliation;
risk scoring;
verifiable database query kernels;
columnar processing;
fixed-size linear algebra;
compiler passes;
policy engines;
crypto transaction validation;
Web3 transaction builders;
industrial deterministic simulation;
HPC kernels with shape/bounds/noalias proofs;
proof-erased symbolic normalization.
```

Less promising domains for extreme speed:

```text
GUI-heavy applications;
network-I/O-bound services;
disk-bound workloads;
very dynamic scripting;
runtime-reflection-heavy systems;
code whose bottleneck is external APIs.
```

---

## 13. Relationship to current roadmap order

This track is strategic and late-stage.

It must not interrupt the current staged plan:

```text
1. Metamathematical foundation
2. Ordinary language mathematics
3. Proof/audit architecture
4. Full proof kernel
5. Standard library
6. Runtime/production execution
7. High-performance native compilation
```

High-performance compilation should start only after the language has enough stable IR, proof erasure, passport erasure, effect/capability inference, and standard core types to make optimization contracts meaningful.

---

## 14. Non-negotiable safety rules

```text
Fast mode must not silently weaken trust.
Fast mode must not erase taint from audit reports.
Fast mode must not turn RuntimeWitness into StaticProof.
Fast mode must not treat unchecked annotation as proof.
Fast mode must not hide unsafe/axiom/oracle dependencies.
Fast mode must not compile reflective proof construction into hot loops.
Fast mode must not allow erased facts without erasure certificates.
```

The central rule:

```text
fast code may be small,
but the reason it is safe must remain explainable.
```
