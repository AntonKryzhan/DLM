# Implementation Notes v0.36

## v0.36.0 Property-Based Invariant Tests

- Added `crates/dlm_core/tests/property_invariants.rs`.
- Added deterministic property-style checks over all current `TrustLevel` values.
- Added generated checks over all current bridge kinds, including `Unknown`.
- Added semilattice checks for `policy::join_trust` and `policy::join_many_trust`.
- Added prefix-closure checks for `CheckPolicy`.
- Added bridge law consistency checks between `bridge_law` and `BridgeProfile`.
- Added passport trust preservation checks for binary and source-derived constructors.
- Added history order/multiplicity checks to protect `HistoryChain` from set-like regressions.
- Added `docs/PROPERTY_INVARIANTS.md`.
- No new runtime semantics or public `.dlm` syntax were introduced.

Example:

```powershell
cargo test -p dlm_core --test property_invariants
```

# Implementation Notes v0.34

## v0.34.0 ID / Resolver Skeleton

- Added typed compiler/checker IDs: `FileId`, `ModuleId`, `TheoryId`, `ValueId`, `TypeId`, `BridgeId`, `ProofId`.
- Added `IdAllocator` with independent monotonic spaces for each ID kind.
- Added `resolve.rs` as the first name-resolution skeleton separate from `checker.rs`.
- Added `ResolvedModule`, `ResolvedTheory`, `ResolvedValue`, `ResolvedBridge` and `SymbolTable`.
- Resolver currently assigns IDs to module-local theories, let bindings and bridges.
- Resolver rejects duplicate theories, duplicate values inside one theory, duplicate bridges and unknown bridge endpoints.
- Checker behavior is not changed yet; this patch prepares the later AST -> HIR -> ResolvedHIR split.

Example:

```powershell
cargo test -p dlm_core --test resolver_ids
```


## v0.27.0 bridge soundness classification

- Added formal bridge taxonomy for `definitional`, `conservative`, `quote`, `transport`, `soundness`, `reflection`, `migration`, `materialize`, and `unsafe` bridges.
- `dlm explain` now reports bridge declarations and bridge soundness profiles.
- Each bridge profile states what it preserves: syntax, value, proof, truth.
- `soundness` bridges are explicitly Axiom-tainted truth bridges.
- `quote` bridges are syntax-only: they preserve syntax but not value/proof/truth.
- `unsafe` and unknown bridge declarations are explain-level invariant issues.
- Added `docs/BRIDGE_SOUNDNESS.md` and bridge-classification examples/tests.

Example:

```powershell
cargo run -p dlm_cli -- explain examples\valid\bridge_soundness_classification.dlm
```


## v0.26.0 passport soundness / formal metatheory layer

Добавлено:

- `SoundnessSummary` как отдельный анализ поверх успешного `CheckReport`;
- CLI-команда `dlm explain <file.dlm>`;
- подсчёт `ProofTerm`, `StaticProof`, `kernel-checked proofs`, `RuntimeWitness`;
- подсчёт `Axiom` / `Oracle` / `Unsafe` taint;
- подсчёт событий `quote`, `transport`, `soundness`, `migration`, `materialize`, GPU history;
- документы `PASSPORT_SOUNDNESS.md` и `FORMAL_METATHEORY.md`;
- regression-тесты для clean kernel proof и axiom-tainted soundness bridge.

`v0.26` не является полным академическим доказательством непротиворечивости ЯРД. Это первый формальный контракт того, что именно гарантирует текущий checker.


## v0.20.0 hotfix

- Fixed GPU round-trip capability preservation: `copy_to_gpu` no longer degrades the inner value construction class.
- `copy_from_gpu(GpuValue<Nat>)` can restore `can_print_decimal` for exact small literal `Nat` values while still keeping GPU transfer history.


## v0.22.0 mathematical foundations: universe hierarchy

- Added first-class universe levels `U0()`, `U1()`, `U2()`.
- Added `Set<U n -> U n+1>` and `Class<U n>` as separate passported mathematical objects.
- Added `set_of(...)`, `class_of(...)`, `universe_succ(...)`, `set_lives_in(...)`, and `class_level(...)`.
- Added `UniverseLevelError` to reject bare universes, set-of-self style mistakes, and `set_of_all_sets()`.
- This patch resumes the mathematical track and intentionally does not expand CPU/GPU/cluster runtime features.


Эта сборка — инженерный MVP-каркас языка DLM/ЯРД.

## Текущее состояние

- Реализован `dlm check`.
- Реализован консервативный `dlm run` для exact `u128` Nat-подмножества.
- Реализован минимальный parser без внешних зависимостей.
- Реализована базовая модель `Type + Passport`.
- Реализованы `CapabilitySet`, `Cost`, `Trust`, `Provenance`, `Validation`, `TheoryContext`.
- Реализованы diagnostics для MVP.
- Реализованы valid/invalid примеры и smoke-тесты.

## v0.2 runtime prototype

`dlm run <file.dlm>`:

- сначала выполняет `dlm check`;
- исполняет только exact `u128` Nat-значения;
- запускает `print_decimal(...)` только после успешной проверки capability;
- не превращает symbolic/compressed/noncomputable значения в runtime decimal.

## v0.3 runtime/static boundary

Добавлено:

- `read_nat()` — runtime Nat из внешнего stdin;
- `require(condition)` — создаёт `RuntimeWitness`, если runtime-проверка прошла;
- `prove(condition)` — создаёт `StaticProof` только для static-safe условий;
- `prove(read_nat() > 0)` отклоняется как `RuntimeStaticMismatch`;
- `dlm run <file.dlm> --stdin <text>` передаёт контролируемый stdin.

## v0.9 trust policy layer

Добавлено:

- `CheckPolicy::research()` — допускает `Checked`, `Builtin`, `Axiom`, но отклоняет `Unsafe`;
- `CheckPolicy::trusted_only()` — отклоняет всё сильнее `Builtin`;
- `CheckPolicy::allow_unsafe()` — допускает весь MVP trust lattice;
- CLI-флаги `--trusted-only`, `--allow-axioms`/`--research`, `--allow-unsafe` для `check` и `run`;
- builtins `axiom_true()`, `axiom_nat()`, `unsafe_nat()` / `unsafe_assume_nat()`;
- нарушения trust policy теперь дают `TrustTaintError`.

## Важное ограничение

Код в архиве подготовлен как изменённые файлы поверх v0.3. В текущей среде Rust toolchain недоступен, поэтому `cargo check` здесь не запускался. Проект по-прежнему не использует внешние crate-зависимости.

## Следующие патчи

1. Расширить parser до полноценной грамматики из `docs/GRAMMAR.md`.
2. Добавить более точные line/column diagnostics.
3. Добавить result-based IO pipeline: `read()` → `parse_nat()` → `require(...)`.
4. Уточнить `TheoryBridge` и начать `transport(...)` MVP.
5. Добавить `yard.toml` policy defaults.


## v0.9 TheoryBridge layer

`v0.9` adds the first executable and checkable semantic theory bridge layer:

- `transport(Source.value)` requires an explicit `bridge Source -> Target { kind = transport }`.
- `quote(Source.value)` remains syntax-level transfer and returns `Term<Source.Type>`.
- `soundness(Source.proof)` requires an explicit `kind = soundness` bridge and returns an Axiom-tainted `StaticProof`.
- `--trusted-only` rejects `soundness(...)` results because they carry `TrustLevel::Axiom`.
- `dlm run` can execute `transport(...)` for exact runtime values if the bridge exists; `soundness(...)` is static-only.

This keeps the core law intact: proofs transported as syntax prove provability, not truth, unless an explicit soundness bridge is supplied.


## v0.9

Added first-class syntax inspection for quoted terms:

- `inspect_ast(term)` requires `can_inspect_ast` and returns `Text`;
- `print_text(text)` prints `Text` runtime values;
- quoted `Term` values intentionally do not support Nat arithmetic capabilities.

This patch demonstrates the key TheoryBridge rule: `quote(...)` changes the role of a value from semantic object to syntactic term.


## v0.9 Passport HistoryChain

Added an append-only `HistoryChain` field to `Passport`.

Current MVP properties:

- history is propagated by derived operations;
- binary operations merge source histories conservatively;
- `quote`, `transport` and `soundness` append explicit bridge events;
- `soundness` also appends `axiom:soundness_assumption`;
- `RuntimeWitness` and `StaticProof` preserve source history and append their own event;
- inferred passport display now prints `history=[...]`.

This prepares the later distributed/reflection roadmap: `MigrationBridge`, `MutationBridge`, epoch-based proof expiry and node-aware trust can extend `HistoryChain` without rewriting the existing passport lattice.

## v0.13 dense patch — distributed virtual cluster seed

Added:

- `BridgeKind::Migration`;
- `NodeArch` and `LocationContext`;
- `Node<arch>` and `Remote<T@arch>` type roles;
- node constructors `node_x86()` / `node_arm()`;
- `migrate(node, Source.value)` checker and runtime path;
- `MigrationBridgeError`;
- remote values lose local decimal-print capability;
- migration history events in `Passport.history`.

This is not full live migration yet. It is the first safe, passport-aware representation of remote values and cross-architecture target nodes.


## v0.11 VirtualResourcePool

Added `VirtualCluster`, resource-aware node constructors, `virtual_pool(...)`, `pool_cores(...)` and `pool_memory_mib(...)`. This is the first MVP step toward the planned unified logical computer over many x86_64/aarch64 nodes while keeping node passports explicit. See `docs/VIRTUAL_RESOURCE_POOL.md`.


## v0.13 Scheduler seed

Implemented `schedule_on(pool, node, value)` / `schedule(pool, node, value)`.

Static checker rules:

- pool must be `VirtualCluster` and have `can_schedule_runtime`;
- target must be `Node<arch>` and have `can_accept_migration`;
- source must have `can_serialize_for_migration`;
- cross-theory scheduling requires a `kind = migration` bridge.

Runtime rules:

- target node must be an actual member of the runtime `VirtualCluster`;
- scheduled values are represented as `Remote<T@arch>`;
- remote values remain symbolic and are not locally decimal-printable.

This is a seed for later cluster scheduling, checkpoint/restore, remote materialization and live migration.

## v0.13 — DistributedMemoryRegion

Added the first virtualized memory layer:

- `TypeKind::DistributedMemory { memory_mib }`;
- `distributed_memory(pool, memory_mib)` / `allocate_memory(...)` / `memory_region(...)`;
- `memory_region_mib(region)` / `distributed_memory_mib(region)`;
- capabilities: `can_allocate_distributed_memory`, `can_use_distributed_memory`, `can_checkpoint_memory`;
- checker-side validation that a memory region is positive and does not exceed the known VirtualCluster memory;
- runtime-side validation of the same bounds;
- `HistoryChain` events for distributed memory allocation.

This layer intentionally does not expose local pointers or mutable shared memory. A `DistributedMemory` value is a passported symbolic region. Addressable distributed memory needs a later consistency model.

## v0.22 — Checkpoint / Restore seed

Added the first checkpoint layer for distributed memory:

- `checkpoint_memory(region)` / `checkpoint(region)` / `checkpoint_region(region)`;
- `restore_checkpoint(snapshot)` / `restore_memory(snapshot)` / `restore(snapshot)`;
- `TypeKind::MemoryCheckpoint`;
- `can_restore_checkpoint` capability;
- checkpoint and restore events in `HistoryChain`;
- runtime symbolic representation `memory_checkpoint<memory_mib=...>`.

This is the minimum primitive required before real live migration: a program can
now turn a distributed memory region into a restorable state object and restore
it under checker control.


## v0.22 — Remote Checkpoint / Restore / Live Migration

Added first-class `RemoteCheckpoint<T@arch>` and runtime operations `checkpoint_remote(...)`, `restore_remote(node, checkpoint)` and `live_migrate(node, remote)`. This is a passport-safe foundation for future live migration: remote values can be checkpointed and restored or moved between x86_64/aarch64 nodes, but they remain `Remote<T@arch>` and never regain local capabilities such as `can_print_decimal` without an explicit future materialization bridge.


## v0.17 Remote materialization

Added `materialize_remote(...)` / `materialize(...)` / `fetch_remote(...)` / `collect_remote(...)`. A `Remote<T@arch>` can now be explicitly converted back into a local `T`, but only through a same-theory operation or an explicit cross-theory `bridge ... { kind = materialize }`. The operation preserves taint/history and records `remote:materialize:*` in `HistoryChain`.


## v0.17 Portable Code Deploy

Adds `compile_portable(...)`, `deploy_portable(node, code)` and `deploy_on(pool, node, code)`. This models cross-architecture portable code as a first-class passported value: `PortableCode<T>` can be deployed to x86_64/aarch64 nodes as `Remote<T@arch>`, while preserving HistoryChain and preventing ordinary local operations on code packages.

## v0.18 GPU Virtual Memory

Added `GpuDevice`, `GpuPool`, and `DistributedGpuMemory` as a separate resource axis from CPU `DistributedMemory`.

The core invariant is:

```text
CPU RAM and GPU VRAM are not the same passport type.
```

`distributed_gpu_memory(...)` requires a GPU pool and produces `DistributedGpuMemory`, which can be queried with `gpu_memory_mib(...)` and printed symbolically, but cannot be printed as decimal or used as ordinary CPU memory.

## v0.18.1 hotfix — HistoryChain resource multiplicity

`HistoryChain` is now treated as an ordered provenance log rather than a deduplicated set.
This fixes pooled resource accounting when multiple nodes/devices have identical resource events,
for example two CUDA devices with the same `gpu_resource:memory_mib` value.

Changed behavior:

```text
gpu_pool(gpu_cuda_with(24576), gpu_cuda_with(24576))
```

now correctly reports total GPU memory as `49152 MiB` instead of collapsing repeated history events to `24576 MiB`.

## v0.19 — GPU ↔ CPU Transfer Bridge

v0.19 adds explicit transfer between CPU-local values and GPU-resident values:

- `copy_to_gpu(value, DistributedGpuMemory)` creates `GpuValue<T>`.
- `copy_from_gpu(GpuValue<T>)` returns the local materialized `T`.
- GPU-resident values deliberately do not get `can_print_decimal`; they can be symbolically printed or explicitly copied back.

This keeps CPU RAM and GPU VRAM/HBM separate in passports while allowing a programmer to use them as parts of one virtual cluster.


## v0.20 — GPU kernel launch layer

Added `GpuKernel<T>`, `compile_gpu_kernel(...)` and `launch_kernel(gpu_memory, kernel)`.
This is the first accelerator execution layer: CPU values can be compiled into GPU kernels, launched into `DistributedGpuMemory`, then returned as `GpuValue<T>` and copied back via `copy_from_gpu(...)`.

New law: GPU kernels and GPU-resident values are not CPU values. They require explicit launch/copy transitions and preserve history events `gpu_kernel:compile` and `gpu_kernel:launch`.

## v0.25 notes

- Added `Language`, `Encoding`, `MetaLevel` and `DefinableNat` type kinds.
- Added `DefinabilityError[E0902]`.
- Added `definable_nat(language, encoding, bound, meta_level)`.
- Added explicit rejection of Berry-style bare undefinability builtins.
- `DefinableNat` does not automatically acquire `can_print_decimal`; it exposes
  definability metadata through dedicated capabilities.


## v0.25 BigNumber Hierarchy

Added explicit huge-number passports for `Graham()`, `TREE(n)`, `BB(n)` and `fast_growing(level)`. Bare huge numbers are rejected; huge finite numbers can be symbolically printed/proof-compared but are not decimal-printable unless a future checked evaluator provides that capability.


## v0.25 — Minimal Proof Kernel

Added `ProofTerm<rule>`, `proof_true()`, `proof_gt(a,b)`, `check_proof(term)`, `can_proof_kernel_check`, and `ProofKernelError`. This is the first layer where `StaticProof` can be produced from a checked proof term rather than only from the legacy `prove(...)` helper.


## v0.27.1 — Soundness Inherited History Hotfix

`HistoryChain` is inherited by derived values. A value such as `Text` produced by `inspect_ast(quote(...))` legitimately contains a prior `bridge:quote:*` event, but it was not directly produced by the quote bridge. The soundness invariant now checks the direct producer event, so quote-derived values no longer create false invariant issues.

## v0.28 — Extended Infinity Mathematics

Implemented additional typed infinity modes beyond the original cardinal/ordinal MVP:

- `limit_omega()` / `infinity_limit()` / `limit_infinity()` → `Infinity<limit>`.
- `potential_infinity()` / `infinity_potential()` → `Infinity<potential>`.
- `class_infinity(class)` / `proper_class_infinity(class)` → `Infinity<class>`.
- `universe_infinity(universe)` / `infinity_universe(universe)` → `Infinity<universe>`.
- `cardinal_add(a,b)` and `ordinal_add(a,b)` preserve explicit arithmetic modes.
- `potential_step(p)` models one step of a potential infinite process.

The invariant is unchanged: there is still no untyped `infinity()`, and no arithmetic bridge can silently coerce cardinal/ordinal/limit/potential/class/universe infinities into one another.

## v0.30 — Consistency / Incompleteness Boundary

Implemented a first incompleteness guard:

- `TypeKind::ConsistencyClaim { theory }`.
- `consistency_claim()` / `consistency_of_current()` / `consistent_current()`.
- `prove_consistency(...)` rejected with `IncompletenessBoundaryError[E0906]`.
- `assume_consistency(...)` / `consistency_axiom(...)` creates `StaticProof<consistency_axiom:T>` with `TrustLevel::Axiom`.
- `SoundnessSummary` counts consistency claims and axiom consistency assumptions.

This keeps consistency claims separate from checked proofs. A future stronger meta-theory bridge may provide checked consistency proofs for weaker theories, but this must be explicit and passported.
## v0.31 — Reflection / Self-Reference Guard

Added an explicit boundary for reflective and self-referential forms. Dangerous self-reference functions are rejected with `ReflectionBoundaryError`. Safe MVP claim constructors are symbolic and do not produce checked truth/proof objects unless the user explicitly requests an axiom-tainted lift.

The implementation keeps the v0.31 layer conservative: no implicit theorem/proof synthesis is added. `reflection_claim(...)` requires `kind = reflection`; `self_reference(...)` and `godel_sentence()` create claim objects only; `reflection_axiom(...)` and `self_reference_axiom(...)` make axiom-taint visible in the passport/history path.
## v0.31 fix #3 — preserve static proof/runtime separation

The v0.31 reflection guard keeps `prove(...)` static-only. The test matrix now separates:

- `reflection_self_reference_guard.dlm` for `dlm check` / `dlm explain`;
- `reflection_runtime_symbolic_guard.dlm` for `dlm run`.

This avoids weakening the runtime merely to satisfy a smoke command and preserves the existing proof-kernel invariant.

## v0.35.0 — Checker orchestration skeleton

- Added `passes.rs` with `PassId`, `PassStatus`, `PassReport`, `PassPipelineReport` and `run_frontend_passes(...)`.
- `CheckReport` now carries `passes: PassPipelineReport`.
- `Checker::check_module(...)` runs the resolver frontend before the legacy semantic checker.
- If name resolution fails, the legacy checker is skipped and resolver diagnostics are returned as checker diagnostics.
- The old semantic implementation remains in place and is represented as `PassId::LegacyChecker`.
- New regression tests live in `crates/dlm_core/tests/checker_passes.rs`.

## v0.37.0 — Meta-Level Stratification foundation

This patch introduces the first explicit meta-level API without changing surface `.dlm` syntax.

New module:

```text
crates/dlm_core/src/meta_level.rs
```

Important implementation laws:

```text
M0 = object level
M1 = meta level
M2 = meta-meta level
```

Any operation that observes syntax, provability, truth or self-reference of level `N` must run at a strict observer level `> N`.

`meta_quote_passport(...)` intentionally returns `TypeKind::Term { ... }` only. It does not synthesize `TruthClaim`, `Provable` or `StaticProof`, and it preserves the source trust level.

This is a foundation layer for later HIR/TypedIR/ProofIR work. The legacy checker semantics remain unchanged.

## v0.38.0 — Statement / Theorem foundation

This patch introduces a declaration-layer vocabulary without changing surface `.dlm` syntax or legacy checker behavior.

New source module:

```text
crates/dlm_core/src/statement.rs
```

New type forms:

```text
Statement<P>
Theorem<name:P>
Goal<P>
Hypothesis<P>
```

Implementation rules:

```text
Statement is proposition carrier only.
Theorem is not StaticProof.
Theorem from proof requires StaticProof evidence.
Raw ProofTerm must be kernel-checked before theorem construction.
RuntimeWitness cannot close a theorem.
Axiom theorem construction records trust=Axiom and theorem:axiom history.
Hypothesis remains local assumption material, not an exported theorem.
```

This is a foundation for later `ProofContext`, `HypothesisSet`, `TacticStep` and `close_proof(...)` APIs. It deliberately avoids desugaring or syntax changes until HIR/ProofIR split is stronger.
