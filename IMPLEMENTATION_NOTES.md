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


## v0.39.0 — Proof Context Foundation

This patch deliberately keeps proof contexts as an API-level foundation. It does not add tactic syntax to the parser and does not route existing checker behavior through ProofIR yet.

The strict closure helper checks that the open goal, theorem statement and static proof all describe the same proposition. This prevents the statement layer from becoming a loose wrapper around arbitrary `StaticProof` passports.


# Implementation Notes v0.40

## v0.40.0 Tactic Script Foundation

- Added `crates/dlm_core/src/tactic.rs`.
- Added `TacticScript`, `TacticScriptStep`, `TacticStepIndex`, `TacticCommand`, `TacticScriptReport`, and `TacticScriptStatus`.
- Added `execute_tactic_script(...)` as a typed executor over `ProofContext`.
- Added `TacticScriptError [E0911]` for tactic-script structural diagnostics.
- Preserved the existing proof-context closure law: `Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>`.
- Added tests in `crates/dlm_core/tests/tactic_script.rs`.
- Added `docs/TACTIC_SCRIPT.md`.
- No new public `.dlm` syntax or checker semantics were introduced.

Example:

```powershell
cargo test -p dlm_core --test tactic_script
```

# Implementation Notes v0.41

## v0.41.0 Proof Certificate Foundation

- Added `crates/dlm_core/src/certificate.rs`.
- Added `ProofCertificate` and `ProofCertificateStatus`.
- Added `certificate_from_closure(...)` for closed `ProofClosure` values.
- Added `certificate_from_tactic_report(...)` for closed tactic reports.
- Added `verify_certificate_against_theorem(...)`.
- Added deterministic local certificate fingerprinting with `dlm-cert-v1-*` prefix.
- Added `ProofCertificateError [E0912]`.
- Added tests in `crates/dlm_core/tests/proof_certificate.rs`.
- Added `docs/PROOF_CERTIFICATE.md`.
- No public `.dlm` syntax or checker semantics were changed.

The certificate layer is intentionally not a proof constructor. It is an audit artifact over a closed theorem.


## v0.42.0 Proof Certificate Audit / Export Foundation

The proof-certificate layer now has a canonical text export and a structured audit report.

Design constraints:

- no external serialization dependency;
- stable line-oriented format;
- fingerprint validation before checked export;
- separate unchecked export for forensic display only;
- audit does not create or strengthen proofs.


## v0.43.0 Equality Proof / Rewrite Foundation

Implemented a separate equality/rewrite layer in `equality.rs`.

The implementation keeps equality proof evidence distinct from boolean equality checks. `EqProof` is a proof artifact, `RewriteRule` is an applicative rule derived from proof evidence, and `RewriteCertificate` records the final from/to pair after an ordered trace.

The patch intentionally avoids parser and checker integration. It is a core semantic layer for later HIR rewriting and tactic automation.

Important invariant: rewrite construction never lowers trust. If a rewrite rule comes from `axiom_eq_proof`, the resulting trace and certificate remain `trust=Axiom`.


## v0.44 implementation notes

Rewrite normalization intentionally remains a core-only API. It repeatedly applies the first forward rewrite rule matching the current term and requires an explicit `max_steps` bound. This prevents cyclic rewrite systems from becoming non-terminating hidden compiler work.

Normalization certificates reuse the existing `RewriteCertificate` passport; the new report/audit layer validates that the certificate endpoints, trust and provenance match the trace.


## v0.45 implementation notes

`v0.45.0` adds a dedicated Nat induction proof layer. The implementation is intentionally outside parser/checker/runtime integration. It provides typed passport-level constructors and validators for:

- `InductionScheme<Nat,P>`;
- `BaseCase<P(0)>`;
- `StepCase<forall n:Nat. P(n) -> P(succ(n))>`;
- `InductionProof<forall n:Nat. P(n)>`;
- explicit theorem construction from an induction proof and matching statement.

The important safety boundary is that induction cases are static proof obligations. Runtime witnesses, raw proof terms and mismatched proposition families are rejected. Trust and provenance are joined monotonically, so axiom-tainted cases stay visible in the final proof/theorem.


## v0.46 implementation notes

The module/import system is deliberately a core semantic layer and not parser syntax. It validates duplicate imports, duplicate aliases, duplicate exports, missing import targets, and cyclic import graphs. Public/private exports are visibility metadata only and do not create theorem/proof/truth claims or reduce trust taint.


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


## v0.48.0 implementation note

Metatheory dependency auditing is intentionally separate from proof checking. A verified dependency audit is closure evidence for later kernel layers, not a theorem. Axiom-tainted and unsafe-tainted dependencies remain visible in the resulting passport.

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

## v0.50.0 implementation notes

`conservative_extension.rs` introduces a metatheoretic audit object for checking whether an extension preserves old theorem identities while making new assumptions explicit.

The implementation is intentionally conservative:

- old theorem name must match exactly;
- old theorem proposition must match exactly;
- base closure must be closed;
- rejected extension closure rejects the audit;
- open extension closure keeps the audit open;
- empty preservation evidence is rejected;
- duplicate preserved theorem names are rejected;
- new assumptions are carried as dependency entries and preserve trust/provenance taint.

No runtime behavior and no `.dlm` syntax are changed.

## v0.51.0 — Theorem Dependency Graph / Global Metatheory Inventory

Added `theorem_dependency.rs` as the global metatheory inventory layer above dependency audits, closure reports, module-interface audits and conservative-extension audits.

New objects:

- `TheoremDependencyNodeKind`
- `TheoremDependencyNode`
- `TheoremDependencyEdge`
- `GlobalMetatheoryInventoryReport`
- `MetatheoryInventoryStatus`

New passport type:

- `TypeKind::GlobalMetatheoryInventory { subject, status }`

New diagnostic:

- `TheoremDependencyError[E0922]`

The implementation rejects mislabeled nodes, duplicate nodes, duplicate fingerprints, unknown edge endpoints and self-edges. It preserves max trust and axiom/oracle/unsafe taint across all inventory evidence.

## v0.52 — Soundness Boundary Ledger

Added `bridge_assumption.rs` with `BoundaryAssumptionEntry`, `SoundnessBoundaryLedgerReport`, `SoundnessBoundaryStatus`, passport construction, rendering/export helpers, and regression tests.

## Strategic note — High-performance compilation track

This notes file now records a future high-performance compilation track.

No Rust code is changed by this documentation patch. The track is intentionally placed after the current staged plan, because performance compilation depends on stable IR, proof erasure, passport erasure, effect inference, and a trustworthy proof/audit foundation.

Key future compiler concepts:

```text
OptimizationContractIR
FastIR
ProofErasure
PassportErasure
Effect/capability inference
Region/ownership memory model
LLVM/MLIR/Cranelift backend ladder
PGO/LTO/SIMD build modes
```

Main implementation constraint:

```text
A future release-fast path may erase runtime metadata only after the corresponding proof/passport evidence has been validated and recorded in an erasure report.
```


## v0.53.0 — Trusted Base Closure / Final Metatheory Foundation Gate

Added `trusted_base.rs` as the final gate for the current metatheory-foundation track. The new report aggregates axiom registry, dependency audit, closure report, global inventory and soundness-boundary ledger evidence. It preserves max trust and axiom/oracle/unsafe taint, rejects duplicate or missing required evidence, and emits `TrustedBaseClosure<subject:status>` passports.

## v0.54.0 — Metatheory Foundation Exit

Added `metatheory_foundation.rs`, `MetatheoryExitCriterion`, `MetatheoryFoundationExitReport`, and the `MetatheoryFoundationExit` passport type. The report is ready only when all required metatheory exit criteria are satisfied; open/missing criteria keep it incomplete, and failed/duplicate criteria reject it.


## Documentation Control Patch — Stage Readiness Model

Added the project-level readiness triad used for all future stage transitions:

```text
Local readiness
Architectural readiness
Fundamental readiness
```

This clarifies that closing a stage as an MVP gate is not the same as declaring it 100% complete. Future patches should include a readiness delta when they affect a major roadmap stage.

New document:

- `docs/STAGE_READINESS_MODEL.md`

The current interpretation is:

```text
Stage 1 / Metamathematical foundation:
  local readiness high after full v0.54 validation;
  architectural readiness strong enough to move into ordinary mathematics;
  fundamental readiness still below finality and expected to be revisited under quantifier/function/dependent-type pressure.
```


## v0.55.0 — Logic / Quantifier foundation

This patch starts ordinary mathematics after the metatheory foundation gate. It intentionally adds formula objects, not theorem/proof introduction rules.

Important distinction:

```text
LogicalFormula != Theorem
LogicalFormula != StaticProof
QuantifiedFormula != Theorem
QuantifiedFormula != StaticProof
```

The implementation keeps the layer conservative: connectives enforce arity; quantifiers require explicit binder/domain; proof/truth/runtime/provability/reflection/consistency objects are rejected as formula operands unless a later explicit extraction/proof rule is added.


## v0.56.0 — Substitution / Alpha-Equivalence Foundation

This patch adds the first explicit variable-scope layer for the ordinary mathematics track. It intentionally keeps substitution as an auditable report object rather than a proof-producing operation.

Implementation points:

- `VariableScopeReport` extracts free and bound identifiers from the restricted textual formula identity used by the current logic MVP.
- `AlphaEquivalenceReport` canonicalizes binders to `$0`, `$1`, ... and compares canonical forms.
- `SubstitutionReport` rewrites identifier tokens only, blocks substitutions under a same-name binder, and rejects obvious capture risk when replacement free variables collide with source binders.
- The layer preserves Axiom/Oracle/Unsafe taint and does not turn formula manipulation into theorem/proof/truth evidence.

Known limitation: this is still a textual formula MVP. Full capture-avoiding substitution over a real term AST should replace it after function/lambda/dependent-term syntax lands.

<!-- RUNTIME_HARDWARE_LAYERING_PRINCIPLE_BLOCK -->
## Runtime / Hardware Layering implementation note

Future runtime/compiler patches must preserve the four-layer separation:

```text
source/mathematical -> IR/compiler -> runtime control -> hardware execution
```

Proof checking, full history and full passports belong high in the stack. Dense buffers, compact descriptors and deterministic kernels belong low in the stack.

<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->
## DLM Architectural Laws enforcement note

Every future patch should be reviewed against `docs/DLM_ARCHITECTURAL_LAWS.md`: semantic layer separation, passport-governed operations, proof erasure, dense runtime data, explicit effects, bridge contracts, trust monotonicity, ID-based resolution, Span preservation, visible trusted base and honest downgrade.

<!-- V0_57_FUNCTION_LAMBDA_APPLICATION_BLOCK -->
## v0.57.0 — Function Type / Lambda / Application Foundation

DLM now has an ordinary function foundation for Stage 2 — ordinary mathematics of the language.

Added:

```text
FunctionType
LambdaTerm
ApplicationTerm
ApplicationStatus
```

Main law:

```text
Function application is not theorem proving.
LambdaTerm is not StaticProof.
ApplicationTerm is not TruthClaim.
```

Readiness delta:

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         18–24% -> 24–32%
Architectural readiness: 40–50% -> 44–56%
Fundamental readiness:   25–35% -> 28–40%
```


<!-- V0_58_FUNCTION_CONTRACTS_BLOCK -->
## v0.58.0 — Function Contract / Purity / Totality Boundary

Adds `docs/FUNCTION_CONTRACTS.md` and the first contract layer above ordinary functions. Function contracts record purity, explicit effect boundaries, totality status, static evidence, open obligations and honest downgrade status.

Main law:

```text
FunctionContract is not theorem/proof/truth.
It is an audit/control object for future optimization, scheduling and assurance modes.
```


## v0.59.0 — Product / Sum / Record Type Foundation

Added `docs/STRUCTURAL_TYPES.md` and the core structural type layer: ProductType/ProductTerm, SumType/SumInjection and RecordType/RecordTerm/RecordProjection. This layer preserves trust taint, rejects proof/truth/runtime objects as ordinary structural values, and prepares future layout/ABI-aware records.


## v0.60.0 — Structural Elimination / Pattern Boundary

Adds explicit product elimination, sum case elimination and record pattern reports. Structural elimination is value-level and does not become proof, theorem or truth. Taint is preserved; proof/truth/theorem/runtime smuggling is rejected. See `docs/STRUCTURAL_ELIMINATION.md`.


## v0.61.0 — Option / Result / Partiality Type Foundation

Adds explicit `Option<T>`, `Result<T,E>` and `PartialityReport` objects for ordinary mathematical partiality. Partial functions now have a typed target representation instead of hidden null/exception semantics. Readiness delta: Local 42–50% -> 48–56%, Architectural 60–72% -> 64–76%, Fundamental 42–54% -> 46–58%.

## v0.62 — Sequence/List foundation

Introduces `sequence.rs`, `sequence_types` tests, `TypeKind::{ListType,ListValue,SequenceType,SequenceValue,SequenceIndex}` and `E0933 SequenceTypeError`. Indexing reports `Option<T>` boundary instead of throwing runtime exceptions.

## v0.63 — Traversal boundary

Implemented finite `map`/`fold` traversal reports over `ListValue` and `SequenceValue`. Traversals require exact function-contract domain/codomain matching, explicit fuel, and preserve Axiom/Oracle/Unsafe taint. `VerifiedBounded` is only produced for pure, total, verified, clean contracts with enough fuel.

## v0.64 — Recursion / Well-Founded Fuel Boundary

Introduced `recursion.rs` with `RecursionSchemeReport`, `RecursiveCallReport`, explicit measure kinds, fuel checks, taint preservation, and `E0935 RecursionBoundaryError`.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.


## v0.66 implementation note

`standard_prelude` introduces `PreludeOperationKind`, canonical signature generation, `StandardPreludeContractReport`, passport export and audit rendering for checked prelude operations. Signature mismatches and missing budgets are explicit statuses rather than implicit coercions.


## v0.67 implementation note — Prelude Evaluation

`prelude_eval` introduces an explicit small-step semantics layer above `standard_prelude`. It accepts only `verified_checked` prelude contracts, rejects proof/theorem/truth/runtime evidence as ordinary values, consumes explicit fuel for collection traversal, preserves taint, and exports stable evaluation reports.

## v0.71.0 — Dense Runtime Descriptor Boundary

Implemented `dense_runtime.rs` with explicit dense runtime descriptors and reports. Runtime representations now distinguish scalar values, tagged values, dense vectors, slice views, GPU regions, remote regions, and audit-only descriptors. Ownership is explicit and target-aware. The layer rejects mismatched layout containers, scalar boxing of dense data, invalid stride/alignment, zero-sized payloads, and hot payloads that try to carry full passports or per-element passport metadata.

## v0.72.0 — Example Corpus Integrity / GPU Checkpoint Restore Completion

GPU checkpoint/restore mirrors the CPU `MemoryCheckpoint` layer. The checker validates the argument type before the capability so that wrong-type arguments report `DistributedResourceError` with the value passport. Restored regions receive the same capability set as a freshly allocated `DistributedGpuMemory` region; checkpoints receive only `can_symbolic_print` and `can_restore_gpu_memory_checkpoint`. `example_corpus.rs` keeps two explicit lists (`CHECK_ONLY_VALID`, `RUNTIME_FAILURE_INVALID`) and verifies both directions so they cannot go stale.

## v0.73.0 — Ordinary Mathematics Surface Wiring

The checker submodule `checker/ordinary_surface.rs` is dispatched before the legacy builtin match. User functions are checked in the legacy fallback arm, so builtins always win and a `fn` may not shadow one. Parameters live in a `RefCell` frame stack because `infer_expr` takes `&self`. `true`/`false` parse as zero-argument calls, so no new `ExprKind` variant is needed and existing AST walkers are unaffected. Function totality evidence (`StaticProof<total_structural(f)>`) is produced only after the structural non-recursion check. The runtime mirrors the dispatch and evaluates `if_then_else`, `and`, `or` lazily.

Found and not changed: `crates/dlm_core/src/reflection.rs` (v0.31 module-level reflection guard) is not declared in `lib.rs` and is therefore not compiled; the reflection rules that are enforced live inline in `checker.rs`.

## v0.74.0 — Well-Founded Recursion, Records and Sums

The well-foundedness check (`MeasureCtx` in `checker/ordinary_surface.rs`) is purely syntactic and runs before the body is typed. During body typing the declaration is kept in `pending_fn`, so recursive calls are typed against the declared signature. The derived `StaticProof<well_founded(...)>` feeds both the function contract (when the body is total) and `recursion::recursion_scheme`.

Stack: profiling showed about 80 KB per recursion level in debug builds. Most of it came from the huge legacy `eval_call` frame and from one big ordinary dispatch function. Ordinary/user-function dispatch moved into `eval_expr` ahead of the legacy table, ordinary groups became separate `#[inline(never)]` functions, and the `eval_expr` arm bodies were extracted. The result is about 10 KB per level. The runtime runs on its own 1 GiB virtual stack; `check_stack_budget` compares the current stack address with the thread base and fails before the last 64 MiB.

## v0.75.0 — Lambdas and Function Values

`ExprKind::Lambda` is the only new expression variant. `reflection.rs` is still not compiled (see v0.73 note) and was not updated.

Purity/totality of lambdas uses the same `effects_of` as `fn` declarations. Because function values are pure and total by construction, a call of a function-typed binding contributes no effect, which keeps higher-order functions verified without effect polymorphism.

`function_arg` gives `map`/`fold`/`case` one entry point. A bare declared-fn name keeps the fn's real contract; any other expression gets a synthesized pure/total contract carrying the value's taint, so tainted function values are rejected by traversals exactly like tainted declared fns.

The runtime pushes a `<lambda>` entry onto `call_names` for closure calls, so the direct-recursion test used by the measure re-check stays correct. Closures capture the top local frame by value. Theory-level values are looked up at call time, which is equivalent because theory scopes only grow and checked code cannot reference later bindings.

## v0.76.0 — Statements, Theorems and Proofs

`checker/statements.rs` builds the core objects in order: `statement_passport` → `goal_passport` → `open_proof_context` → `close_proof_with_static_proof` / `close_proof_by_axiom` → `certificate_from_closure` → `verify_certificate_against_theorem`. The static proof's evidence passport carries the body's taint and a `proof:<method>:cases=N` event. The proposition text comes from `ordinary::statement_proposition`, shared with the runtime, so checker and runtime agree on the proposition.

Evaluation uses a fresh `Runtime` that sees only the theory's functions and records (not its values) and runs on the runtime thread (`on_runtime_thread`, shared with `run_module`).

The core certificate trace (`open/exact/close`) does not include the evidence method. `dlm certify` prints it from the theorem passport history rather than changing the certificate format.

## v0.77.0 — Symbolic Kernel

`symbolic.rs` is independent of the checker: it takes `FnDecl`s, lemma rules and an expression. Atom keys are built only from normalized arguments, so syntactically different but equal arguments share an atom. Undecided `if_then_else` branches are normalized with recursive unfolding disabled, which keeps normal forms small and normalization finite; the step budget is a second guard. The induction hypothesis is normalized before it is installed as a ground rule, so the rule cannot rewrite itself.

`checker/statements.rs` keeps a per-theory lemma registry. It is filled by closed theorems, symbolic theorems and axioms; bounded theorems and conjectures are excluded by construction. The induction proof passes through the core layer with a synthetic proposition family `P_name`, and the family instantiation is recorded in history before the `StaticProof` of the readable statement is formed.

## v0.78.0 — Facts, Lists, Cross-Check

`Kernel::fact` maps `nat_eq`, `>` and `not(>)` to `Fact::Eq` / `Fact::Ge` over normalized polynomials. `nonnegative_combination` tries the multipliers `0`, `1` and every ratio of matching coefficients; this is a finite candidate set, so the check is cheap and incomplete. Equational residuals keep the v0.77 `lhs = rhs` form.

A symbolic list is `Sym::List { items, rest }`: a known prefix and an optional opaque tail key. Operations reduce on the prefix and leave atoms keyed by the tail (`length(t)`, `map(t,f)`, `fold(t,acc,f)`), so the induction hypothesis about `t` matches them as a ground rule.

The cross-check reuses `Checker::evaluate` with a fixed bound and is recorded after the kernel evidence in the `StaticProof` history; it never contributes to trust. List induction uses separate core objects rather than reusing the Nat scheme, so a Nat scheme cannot close a list proof.

## v0.79.0 — Step / Generalized Induction

`prove_by_induction_with` builds the induction values once (bases, hypotheses, step) and runs every Bool case with
its own kernels. For each hypothesis, an equation installs a ground rule. With `generalizing`, it also installs a
local `LemmaRule` whose `fixed` bindings carry the induction value and the non-generalized variables. Local rules
share `rewrite_with_lemmas` with `using` lemmas. The active-rewrite stack (rule name plus argument keys) blocks
re-entry, which cuts cycles without making the kernel less sound: the call is just left unrewritten.

Bool hypotheses become `truths`: an `Opaque` result of `normalize` whose key is a truth becomes `true`. Keys are
structural, so a key determines its type.

`prove_by_normalization` / `prove_by_induction_with` run on a scoped thread with `KERNEL_STACK_BYTES`. Direct
`Kernel::normalize` calls run on the caller's stack. A debug-build test thread (2 MiB) does not fit
`MAX_SYMBOLIC_DEPTH`, so the depth test gives it the kernel stack.

## v0.80.0 — Programs

`check_module(module)` is `check_program(Program::single(module))`. Imported modules are checked by the same
`Checker`, so their tables stay available for `extends`. Their `inferred` and `certificates` are cleared, and their
diagnostics are moved into the root report with a prefix.

Inheritance copies tables (`functions`, `records`, statement passports from the scope, `lemmas`) and records the
inherited names. Privacy is a pre-scan of the derived theory's own syntax (free identifiers, called names, type
names, `using` lemmas) against the `hidden` map. Hidden definitions stay in the tables, so inherited bodies, the
symbolic kernel and the runtime can still use them. Redeclaring any inherited name, hidden ones included, is an
error, because inherited bodies resolve names in the derived theory.

The syntactic lemma matcher runs in `normalize_call` before argument normalization, for declared functions only. It
is limited to lemmas with a nested call that contains a pattern variable. Everything else keeps the v0.77 semantic
matcher.

## v0.81.0 — Audit / Mutual Groups

`audit_check_report` works on `CheckReport.inferred` only. Statements are recognised by passport type (`Theorem`
named like the entry, `Goal` with a `conjecture:<name>:tested:` event). Methods come from evidence events with
the `:theorem=<own name>` suffix, so merged lemma histories do not leak their methods. Lemma and axiom events are
deliberately taken from the whole history, so they are transitive.

A mutual group is checked at its first member. `pending_group` types calls to members that are not checked yet.
`check_well_founded_in` extends the v0.74 measure walk with the other members' measured positions. Calls to
not-yet-checked members are dropped from the effect analysis and verified after the group, and a failing group is
removed from the function table. Forward references outside a group are detected syntactically before checking.

## v0.82.0 — Logical Lines, Let, Match

`logical_lines` runs before the line-based module parser. A joined line keeps its first line number, and a
single-line logical line keeps its indentation, so columns are unchanged for single-line code. A closing `}` never
continues a line whose brackets are balanced. This preserves the v0.32 diagnostic for `let n =` followed by `}`.

`match` needs no new AST node: the parser desugars it. `ExprKind::Let` is the only new expression form. The runtime
adds a `let` binding to the current frame instead of pushing a frame, because frames are parallel to `call_names`
for the recursion checks. At theory level it pushes a `<let>` frame. The recursion walk keeps a stack of `let`
aliases with the measured position they decrease.

Case splitting reads the residual text of a failed obligation (`nat_eq(n,c)`, `gt(c + n,d)`, ...) and re-runs the
obligation with fresh kernels per case. Hypotheses are not involved, so it is used by `by normalize` only.

## v0.83.0 — Text Literals

The parser stays line-based. `mask_text_literals` runs on every physical line before logical-line joining and
replaces each literal with `__text_N`. The decoded texts sit in a thread-local table that is reset by `parse_module`
and `parse_expr`, so every splitting helper sees no quotes, commas or `//` from inside literals. The kernel keys a
literal by its Rust debug form and decodes it back for folding (`text_literal` accepts only keys that round-trip).
The runtime keeps a stdin cursor for `read_line` / `read_all`, so `read_nat` keeps its v0.13 whole-input meaning.

## v0.84.0 — Compiler

The IR is untyped: every value is a uniform heap object in C, so lowering only resolves names and makes order and
captures explicit. Lowering runs the checker first and refuses unchecked programs. Pass 1 assigns function ids
per theory, with inheritance, and collects statements and records. Pass 2 lowers root theory items into
synthetic `<initN>` functions, so they get frames for `let` and closures. Pass 3 lowers function bodies.

C functions all have the signature `DV *f(DV **env, DV **args)`. Every call site, builtin and binary operator
evaluates its operands into fresh per-site temporaries with the comma operator, which gives left-to-right order. A
frame owns `s[]` (slots) and `t[]` (temporaries). Record field-name arrays have static storage, because records
outlive frames.

The differential tests need a C compiler. The checker now always runs on its own large-stack thread, because a
deeply nested generated program overflowed the 2 MiB test thread inside `infer_expr`.

## v0.85.0 — Int

`a - b` and `-a` are parsed into the calls `__sub` / `__neg`, so every later stage handles them as builtins. The
parser finds the last top-level `+` or `-` that is binary (a `-` after an operator, a comma, an opening parenthesis
or at the start is unary), which gives left associativity for the shared level.

Literals stay Nat; only a negated literal is an Int constant. This keeps every existing program and proof
unchanged and makes all Int/Nat conversions visible (`int(n)`, `abs(i)`).

In the symbolic kernel `Sym::Int(Poly)` reuses the polynomial normal form, but none of the Nat positivity
reasoning: an Int comparison is decided only when the difference is a constant, and Int sides produce no
hypothesis facts. The second hazard is untyped irreducible terms (`Opaque`), which the Nat path silently treats as
non-negative atoms; `strict_opaque` turns this off whenever the functions, lemmas or goal mention Int. `abs` picks
the sign of its argument whose rendering is smaller, which identifies `abs(x)` with `abs(-x)` soundly.

The C runtime keeps Int as its own tag (`DV_INT`), so a Nat and an Int with the same magnitude never compare
equal by accident, and uses the GCC/Clang overflow builtins for exact arithmetic.

## v0.86.0 — Compiled runtime

Scalar functions: types are reconstructed by unification over the IR; a function is emitted with native
`nat_t`/`int_t`/`int` parameters only when every slot and expression has a known numeric or Bool kind and the
body uses only scalar operations and calls to other scalar functions. Any conflict disables the specialization
for the whole program. A boxed wrapper `f{id}` keeps the uniform ABI for function values.

Tagged numbers: the two low bits of a value word mark small Nat (01) and Int (10, 11 for negatives); heap objects
are 16-byte aligned. `dv_tag`, `dv_nat_value` and `dv_int_value` hide the encoding.

Regions: every generated function and initializer enters a region; allocations record their owner. The overflow
report (`machine_arithmetic.rs`) is an interval analysis over the IR with guards `x > c` as refinements; it is
independent of the proof kernel.

## v0.86.1 — Survivor groups

The v0.86.0 collector moved survivors to the caller block by block and traced them again at the caller's return.
Ownership is now an allocation serial number: a region owns every block with serial `>= start` (process-lifetime
allocations have serial 0), so promotion needs no per-block update. The survivors of one collection form a
group (serial interval, member list, roots, `ext` = references to older blocks found while tracing). Values are
immutable and acyclic, so if all roots of a group are reached again the members are exactly as reachable as
before: the group is spliced in O(1), and `ext` is processed so that older blocks referenced from it survive.
Reaching only some roots or an inner block forces a precise member-by-member trace of that group. Groups whose
roots are not all reached are expanded only after all whole groups have been processed. Marks hold the epoch of
the last collection, so whole groups are never walked to clear marks. References to older blocks are
deduplicated per collection by the same mark.

## v0.87.0 — Typed Core

Expression identity is a process-wide counter (`ExprId::fresh`); clones keep the id, so all copies of a node
refer to one checked fact, and equality of `Expr` ignores ids and spans to keep existing structural comparisons.
Spans: the parser already tracked columns of the logical (joined, literal-masked) line; `logical_lines` now
records where each physical line starts in the logical line, and `mask_text_literals_mapped` records each
placeholder, so `span_of` maps a logical column back to the physical line and byte column. A thread-local span
context holds the mapping while a module is parsed.

The checker records types in `infer_expr`, which every sub-expression passes through; the recorded type is the
surface type of the inferred passport. Named functions passed as values and lambdas that a builtin infers
through a dedicated path may have no recorded type; lowering then uses the function's checked signature or the
lambda's declared parameters and lowered body, which are themselves checked facts. `verify_ir` would reject any
node still untyped.

The IR verifier is deliberately structural: it does not compare the IR with the source semantics. The
differential parity tests and the specification are the evidence for semantic agreement.

Stack guard: the frame address is used instead of the address of a local because AddressSanitizer's fake stack
places locals on the heap. GCC may turn an accumulating recursion such as `depth(k) + 1` into a loop at `-O2`;
the guard then never triggers and the exact result is printed, which is the same observable result without the
resource failure.
## v0.88 — Checked record identities and independent equational replay

`RecordFacts` crosses the checker/lowering boundary. Schemas resolve in declaration order to local `RecordId`s;
source `Record(name)` types become compiler-only `RecordRef(id)` recursively. Inheritance reuses IDs, while
different declarations with the same display name remain distinct. Backend projections consume schema IDs and
field indices. `IrProgram::source_type` is a display adapter, not a semantic fallback.

`proof_kernel.rs` checks flat backward DAGs iteratively and hash-conses structural terms for equality. The small
producer and text codec live separately. Kernel replay never calls the symbolic prover. Axiom declarations own
their binders/terms outside the proof object; the standalone CLI and certificate attachment use an empty context.
All encountered axiom steps remain dependencies, including unused proof nodes. Size/work/depth/text limits and
numeric capacity return `UnknownWithinBudget`, separately from an invalid rule or scope.

Source theorem acceptance still runs the existing symbolic pipeline and runtime cross-check. A supported Nat
artifact is then independently checked and matched to the full proposition. Legacy proofs are not relabeled.
The audit reports both the existing trusted path and the new replay component. Full logical/inductive proof
elaboration, source/IR preservation and native-code certification remain open.

## v0.100.1 — Compatible cumulative Q(i) integration

The separate ComplexRational implementation retains private canonical rational
components and checked intermediate operations. Its source BigComplex/bigc_*
API is added to StdComplexQ alongside the existing ComplexQ/complexq_* API;
none of the existing declarations or display contracts is replaced. New CLI
cases have their own target so the prior CLI regressions remain active.

The separator scanner works in 16-character blocks. Its outer 8192-call fuel
covers the 65544-character complex input bound at 16384-bit precision, while
each component retains std.exact parsing and work rules. Regression cases
parse the full bound and distinguish valid 1025-bit precision with too little
work from invalid 16385-bit precision. The host and source work counters keep
their existing distinct units. New results add no proof or optimizer authority.

## v0.101 — Canonical kernel artifacts; rational intervals

`KernelProof::from_text` keeps its line parser and then requires
`to_text(proof) == text`. This is simpler and stricter than validating each
spelling rule separately: every accepted artifact is the serializer's output, so
a proof has exactly one text and one hash. `proof-check` keeps the structured
`KernelError` instead of a string so the exit code can distinguish Invalid from
UnknownWithinBudget.

`IntervalQ` uses only checked `Rational` operations, so its precision rule is
the one of the components: the operands and each listed intermediate value. The
square root works on integers only: for `x = p/q` it takes `isqrt(p·q·4^k)` and
divides by `q·2^k`, which avoids rational root approximations and makes the
exact case (perfect squares at the resolution) fall out of the remainder. Host
and source compute the same intermediate integers, so the minimal admissible
precision is shared and tested on both layers. Source text scanning uses the
16-character block scanner of `std.complex_q` for the long-input profile.

## v0.102 — Ownership pass for ByteBuf; computable reals

The ownership pass works on the AST after typing, because the surface language
is a small expression tree (`Ident`, `Call`, `Let`, `Lambda`, operators) and
`match` is already desugared into `let`/`if_then_else`. A buffer-producing
expression is recognised syntactically (buffer builtins, functions returning
`ByteBuf`, owners, conditionals and lets of those); this is complete because a
buffer can never be stored in data, captured or passed to a function value, so
no other expression can yield one. Moves into a call are marked pending until
all arguments are walked, which models the left-to-right evaluation followed by
the transfer of ownership at entry. Instead of rejecting conditionals that
consume an owner on one branch only, the pass releases it at the end of the
other branch; recursive functions with a base case that does not consume their
buffer are therefore written naturally. The plan is recorded in Typed Core by
`ExprId` and parameter name, so lowering does not rerun the analysis and the IR
verifier can check every release.

The C runtime keeps buffer storage outside the call regions: the region object
is a fixed header pointing to `malloc`ed storage, so region survivors stay
immutable while bytes change in place, and the GC needs no new edges. Live
storage is linked into a list that the existing `atexit` cleanup frees, which
covers termination by a runtime error. Releases always follow the evaluation of
the scope's value, so a call they wrap is never a tail call and region reuse of
tail calls cannot drop a live owner's header.

Computable reals reuse `IntervalQ` operations unchanged; a refinement round is a
plain interval evaluation of the DAG at one square-root resolution, which makes
the host and the source library produce identical intervals and the minimal
precision of a request a computable property of the oracle. Retrying the whole
program at a doubled resolution keeps the schedule simple and deterministic; the
retry reasons (a divisor or radicand interval touching zero, a width above
`2^-n`) are exactly the cases where a finer resolution can help.

## v0.103 — Call borrows; complex programs

`&ByteBuf` is recorded in `FnDecl::borrowed` (the parameter type stays `ByteBuf`), so typing, Typed Core and IR
types are unchanged; only the ownership shape and `IrFunction::borrowed` differ. Complex operations are program
constructors (`CRealBuilder`), so N09 reuses the N08 schedule and error control instead of a second
approximation algorithm, and host/source equality is checked on program text.
