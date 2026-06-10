# DLM / ЯРД MVP Compiler Scaffold v0.29.0

## v0.29.0 provability/truth boundary

- Added first-class `Prop<...>` and `Provable<Theory.Proposition>` passports.
- Added `prop_true()`, `prop_gt(...)`, `provable_of(...)`.
- Added `truth_from_provable(...)` as an explicit rejection boundary: provability is not truth.
- Added `truth_from_provable_axiom(...)` as an explicit Axiom-tainted lift for research mode.
- Added `TruthBoundaryError[E0905]`.
- `dlm explain` now counts propositions, provability claims and axiom truth lifts.

Example:

```powershell
cargo run -p dlm_cli -- check examples\valid\provability_truth_boundary.dlm
cargo run -p dlm_cli -- run examples\valid\provability_truth_boundary.dlm
```


## v0.27.1.0 bridge soundness classification

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


## v0.26.0 mathematical foundations: passport soundness explain

- Added `dlm explain <file.dlm>` to summarize passport soundness facts after a successful check.
- Added `SoundnessSummary` in `dlm_core`.
- Added `docs/PASSPORT_SOUNDNESS.md` and `docs/FORMAL_METATHEORY.md`.
- The summary reports kernel-checked proofs, proof terms, runtime witnesses, Axiom/Oracle/Unsafe taint, bridge events, migration/materialization and GPU history events.
- This is a formalization layer: it does not add new object-language syntax, but states and checks key invariants of the current passport model.

Example:

```powershell
cargo run -p dlm_cli -- explain examples\valid\minimal_proof_kernel.dlm
cargo run -p dlm_cli -- explain examples\valid\soundness_summary_axiom.dlm
```


## v0.20.0 hotfix

- Fixed GPU round-trip capability preservation: `copy_to_gpu` no longer degrades the inner value construction class.
- `copy_from_gpu(GpuValue<Nat>)` can restore `can_print_decimal` for exact small literal `Nat` values while still keeping GPU transfer history.


## v0.22.0 mathematical foundations: universe hierarchy

- Added first-class universe levels `U0()`, `U1()`, `U2()`.
- Added `Set<U n -> U n+1>` and `Class<U n>` as separate passported mathematical objects.
- Added `set_of(...)`, `class_of(...)`, `universe_succ(...)`, `set_lives_in(...)`, and `class_level(...)`.
- Added `UniverseLevelError` to reject bare universes, set-of-self style mistakes, and `set_of_all_sets()`.
- This patch resumes the mathematical track and intentionally does not expand CPU/GPU/cluster runtime features.


Это стартовая реализация MVP-компилятора/проверяльщика для языка DLM/ЯРД.

Текущая цель версии `v0.29.0`: команда `dlm check <file.dlm>` проверяет синтаксис, theory scope, паспорта и capabilities, а команда `dlm run <file.dlm>` умеет выполнять маленькое безопасное подмножество языка на exact `u128` runtime.

## Быстрый запуск на Windows

```powershell
cd D:\JARD\dlm_yard
cargo check
cargo test
```

Запуск проверки валидного примера:

```powershell
cargo run -p dlm_cli -- check examples\valid\simple_nat.dlm
```

Проверка запрещённой операции:

```powershell
cargo run -p dlm_cli -- check examples\invalid\print_busy_beaver.dlm
```

Ожидаемый результат: компилятор должен отклонить `print_decimal(BB(1000))`, потому что `BB(1000)` имеет паспорт `definable_noncomputable` и не имеет capability `can_print_decimal`.

## Запуск exact runtime

```powershell
cargo run -p dlm_cli -- run examples\valid\run_small.dlm
```

Ожидаемый вывод:

```text
program output:
30
```

## Runtime input и RuntimeWitness

`v0.9` добавляет первый контролируемый мост к runtime-вводу:

```dlm
module examples.runtime_witness_input

theory Core {
    let n = read_nat()
    let positive = require(n > 0)
    print_decimal(n)
}
```

Запуск:

```powershell
cargo run -p dlm_cli -- run examples\valid\runtime_witness_input.dlm --stdin 42
```

Ожидаемый вывод:

```text
program output:
42
```

Если передать `0`, `require(n > 0)` упадёт на runtime:

```powershell
cargo run -p dlm_cli -- run examples\valid\runtime_witness_input.dlm --stdin 0
```

## Что уже есть

- CLI `dlm check <file.dlm>`.
- CLI `dlm run <file.dlm> [--stdin <text>]`.
- Минимальный line-oriented parser для MVP-синтаксиса.
- AST для `module`, `theory`, `bridge`, `let`, expression statement.
- Product Passport model: construction, capabilities, cost, trust, provenance, validation, theory.
- Capability checker: операция разрешена только если capability есть в паспорте.
- Trust / provenance / validation propagation на базовом уровне.
- TheoryBridge skeleton: `quote` между теориями.
- `read_nat()` как runtime input Nat.
- `require(condition)` как RuntimeWitness.
- `prove(condition)` как StaticProof только для static-safe условий.
- `RuntimeStaticMismatch` для попытки сделать StaticProof из runtime input.
- Диагностика: `ParseError`, `AccessError`, `TheoryBridgeError`, `NameError`, `RuntimeStaticMismatch`, `TrustTaintError`, `RuntimeError`.
- Примеры valid/invalid.

## Что намеренно не реализовано в v0.9

- Настоящий proof kernel.
- Полная EBNF-грамматика.
- SMT / monotonicity checker для пользовательских passport transformers.
- Полная семантика `transport`, `reflection`, `soundness bridge`.
- Полноценный Result/Option pipeline для IO.
- Генерация exe / байткод.
- LSP/IDE.

## Главный закон MVP

```text
Операция разрешена не потому, что тип подходит,
а потому что паспорт значения содержит нужную capability,
а значение не пересекает theory/static/runtime/trust границы без явного разрешения.
```

## Trust policy modes v0.9

`v0.9` adds the first trust policy layer:

```powershell
cargo run -p dlm_cli -- check examples\valid\axiom_research.dlm
cargo run -p dlm_cli -- check --trusted-only examples\valid\axiom_research.dlm
cargo run -p dlm_cli -- check --allow-unsafe examples\invalid\unsafe_nat_requires_flag.dlm
```

Default `check` allows `Checked`, `Builtin` and `Axiom` trust for research-mode experiments, but rejects `Unsafe` taint. `--trusted-only` rejects `Axiom`, `Oracle` and `Unsafe`. `--allow-unsafe` accepts the full trust lattice for prototypes, while preserving the taint in inferred passports.


## Passport HistoryChain v0.9

`v0.9` adds the first append-only `HistoryChain` to every inferred passport. The chain records important creation, derivation and bridge events:

- `created:literal_nat` / `created:compressed_nat`;
- `runtime_input:read_nat` / `runtime_witness:require`;
- `bridge:quote:<name>`;
- `bridge:transport:<name>`;
- `bridge:soundness:<name>` plus `axiom:soundness_assumption`;
- `equality:value`, `equality:syntax`, `equality:proof`;
- output/inspection operations such as `output:print_decimal` and `inspect:ast`.

This is the first implementation of the rule: a value must remember the important transitions that shaped its passport. Later versions can replace the current string-based MVP chain with typed events, hashes, epochs and node IDs.

Example:

```powershell
cargo run -p dlm_cli -- check examples\valid\history_chain.dlm
```

The inferred passports now include `history=[...]`, so trust and bridge provenance are visible in diagnostics and normal `check` output.

## v0.13 distributed seed

The MVP now has the first node-aware migration surface:

```dlm
bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
    print_symbolic(remote)
}
```

Run:

```powershell
cargo run -p dlm_cli -- run examples\valid\migration_bridge.dlm
```

Expected output:

```text
remote[aarch64](7)
```


## v0.11 VirtualResourcePool

Added `VirtualCluster`, resource-aware node constructors, `virtual_pool(...)`, `pool_cores(...)` and `pool_memory_mib(...)`. This is the first MVP step toward the planned unified logical computer over many x86_64/aarch64 nodes while keeping node passports explicit. See `docs/VIRTUAL_RESOURCE_POOL.md`.


## v0.13 Scheduler seed

v0.13 adds the first passport-aware scheduler primitive:

```dlm
let job = schedule_on(pool, node, value)
```

It requires a `VirtualCluster`, a target `Node<arch>`, and a serializable source value.
Cross-theory scheduling requires an explicit `kind = migration` bridge.
The result is `Remote<T@arch>` and keeps only symbolic remote capabilities.

Examples:

```powershell
cargo run -p dlm_cli -- check examples\valid\schedule_on_virtual_pool.dlm
cargo run -p dlm_cli -- run examples\valid\schedule_on_virtual_pool.dlm
```

Expected output:

```text
remote[aarch64](9)
```

### v0.13 distributed memory quick check

```powershell
cargo run -p dlm_cli -- check examples\valid\distributed_memory_region.dlm
cargo run -p dlm_cli -- run examples\valid\distributed_memory_region.dlm
```

Expected runtime output:

```text
49152
distributed_memory<memory_mib=49152>
```

Negative checks:

```powershell
cargo run -p dlm_cli -- check examples\invalid\distributed_memory_requires_cluster.dlm
cargo run -p dlm_cli -- check examples\invalid\distributed_memory_zero.dlm
cargo run -p dlm_cli -- check examples\invalid\distributed_memory_exceeds_pool.dlm
cargo run -p dlm_cli -- check examples\invalid\memory_region_mib_requires_region.dlm
```


## Checkpoint / Restore v0.22

`v0.22` adds the first memory checkpoint primitive:

```powershell
cargo run -p dlm_cli -- check examples\valid\checkpoint_restore_memory.dlm
cargo run -p dlm_cli -- run examples\valid\checkpoint_restore_memory.dlm
```

Expected runtime output:

```text
4096
memory_checkpoint<memory_mib=4096>
distributed_memory<memory_mib=4096>
```

A checkpoint is not a live memory region: it carries `can_restore_checkpoint`, not
`can_use_distributed_memory`, until `restore_checkpoint(...)` is applied.


## v0.22 — Remote Checkpoint / Restore / Live Migration

Added first-class `RemoteCheckpoint<T@arch>` and runtime operations `checkpoint_remote(...)`, `restore_remote(node, checkpoint)` and `live_migrate(node, remote)`. This is a passport-safe foundation for future live migration: remote values can be checkpointed and restored or moved between x86_64/aarch64 nodes, but they remain `Remote<T@arch>` and never regain local capabilities such as `can_print_decimal` without an explicit future materialization bridge.


## v0.17 Remote materialization

Added `materialize_remote(...)` / `materialize(...)` / `fetch_remote(...)` / `collect_remote(...)`. A `Remote<T@arch>` can now be explicitly converted back into a local `T`, but only through a same-theory operation or an explicit cross-theory `bridge ... { kind = materialize }`. The operation preserves taint/history and records `remote:materialize:*` in `HistoryChain`.


## v0.17 Portable Code Deploy

Adds `compile_portable(...)`, `deploy_portable(node, code)` and `deploy_on(pool, node, code)`. This models cross-architecture portable code as a first-class passported value: `PortableCode<T>` can be deployed to x86_64/aarch64 nodes as `Remote<T@arch>`, while preserving HistoryChain and preventing ordinary local operations on code packages.

## v0.18 — GPU virtual memory layer

v0.18 adds the first GPU resource model:

```text
GpuDevice<backend>
GpuPool
DistributedGpuMemory<MiB>
```

The programmer can model a single virtual GPU memory pool, but the checker keeps GPU VRAM/HBM separate from CPU `DistributedMemory`.

Example:

```powershell
cargo run -p dlm_cli -- check examples\valid\gpu_memory_pool.dlm
cargo run -p dlm_cli -- run examples\valid\gpu_memory_pool.dlm
```

### v0.19 GPU ↔ CPU transfer

```powershell
cargo run -p dlm_cli -- check examples\valid\gpu_cpu_transfer.dlm
cargo run -p dlm_cli -- run examples\valid\gpu_cpu_transfer.dlm
```

Expected output:

```text
gpu_value<memory_mib=8192>(55)
55
```

The important rule: `GpuValue<T>` is not local `T`. Use `copy_from_gpu(...)` to materialize it back on CPU.


## v0.20 — GPU kernel launch layer

Added `GpuKernel<T>`, `compile_gpu_kernel(...)` and `launch_kernel(gpu_memory, kernel)`.
This is the first accelerator execution layer: CPU values can be compiled into GPU kernels, launched into `DistributedGpuMemory`, then returned as `GpuValue<T>` and copied back via `copy_from_gpu(...)`.

New law: GPU kernels and GPU-resident values are not CPU values. They require explicit launch/copy transitions and preserve history events `gpu_kernel:compile` and `gpu_kernel:launch`.

## v0.25 — Definability Passport

Adds explicit definability objects:

```dlm
let lang = language_L0()
let enc = encoding_godel()
let meta = meta_level(1)
let d = definable_nat(lang, enc, 20, meta)
```

Bare Berry-style forms such as `berry_number(20)` and incomplete
`definable_nat(20)` are rejected with `DefinabilityError[E0902]`.


## v0.25 BigNumber Hierarchy

Added explicit huge-number passports for `Graham()`, `TREE(n)`, `BB(n)` and `fast_growing(level)`. Bare huge numbers are rejected; huge finite numbers can be symbolically printed/proof-compared but are not decimal-printable unless a future checked evaluator provides that capability.


## v0.25 — Minimal Proof Kernel

Added `ProofTerm<rule>`, `proof_true()`, `proof_gt(a,b)`, `check_proof(term)`, `can_proof_kernel_check`, and `ProofKernelError`. This is the first layer where `StaticProof` can be produced from a checked proof term rather than only from the legacy `prove(...)` helper.


## v0.27.1 — Soundness Inherited History Hotfix

`HistoryChain` is inherited by derived values. A value such as `Text` produced by `inspect_ast(quote(...))` legitimately contains a prior `bridge:quote:*` event, but it was not directly produced by the quote bridge. The soundness invariant now checks the direct producer event, so quote-derived values no longer create false invariant issues.

## v0.28 — Extended Infinity Mathematics

Adds a stricter first layer for the remaining infinity modes:

```dlm
let c = aleph0()
let o = omega()
let lim = limit_omega()
let pot = potential_infinity()
let ci = class_infinity(class_of(U0()))
let ui = universe_infinity(U0())
```

New arithmetic/transition forms:

```dlm
cardinal_add(c1, c2)
ordinal_add(o1, o2)
potential_step(potential)
```

The checker keeps the modes separate: cardinal arithmetic cannot consume ordinal infinities, `class_infinity(...)` requires an explicit `Class<U n>`, and `universe_infinity(...)` requires an explicit `Universe<U n>`.
