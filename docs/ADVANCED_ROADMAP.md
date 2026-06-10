# DLM/ЯРД Advanced Roadmap

This document records advanced features discussed after the MVP compiler reached typed infinity and explicit equality modes.

These features are not part of the current `v0.x` MVP unless explicitly stated.

## v0.9 — Passport HistoryChain

Implemented first.

Purpose:

```text
remember important value transitions even when the current local passport looks safe
```

Events include creation, derivation, bridge crossing, runtime input, runtime witness, axiom and unsafe sources.

## v1.1 — RuntimeState model

Required before live migration or verified hot-swap.

A future runtime state should contain:

```text
module id
current theory
runtime scopes
call stack
heap/store
runtime witnesses
active IO handles
history chains
trust policy
```

## v1.2 — Serialization-safe RuntimeValue

Before migration, values must declare whether they can be serialized without losing required passport guarantees.

Potential capability:

```text
can_serialize_runtime
can_restore_runtime
```

## v1.3 — MigrationBridge

A migration is not ordinary IO. It is a theory/runtime bridge:

```text
MigrationBridge<NodeA, NodeB>
```

It must check:

```text
compatible std_core version
compatible theory versions
accepted trust policy
accepted axiom/oracle/unsafe history
serializable runtime values
restorable RuntimeWitness state
resource capability compatibility
```

## v1.4 — Epoch / Proof Expiry

Static mathematical proof does not expire.

But these may expire:

```text
RuntimeWitness
OracleProof
ExternalProof
NetworkCapabilityProof
NodeTrustProof
```

Future passport axis:

```text
ValidityWindow { epoch, ttl, expires_at }
```

## v2.0 — ReflectionBridge

Allows a program to inspect code as syntax safely.

Important distinction:

```text
quote gives Term
reflection gives controlled access to program/module/theory AST
```

Reflection must never imply truth or executable replacement by itself.

## v2.1 — MutationBridge

Self-modification should not be raw self-modifying code.

Correct model:

```text
current code -> quoted/reflected AST -> mutation candidate -> re-check -> activate or rollback
```

Mutation result must carry:

```text
Mutation<origin>
HistoryChain event
Trust taint if verification is incomplete
rollback metadata
```

## v2.3 — Verified Hot-Swap

A checked mutation can be activated without stopping the runtime if:

```text
old RuntimeState can be transformed to new RuntimeState
all required witnesses are preserved or re-validated
new code passes passport/type/theory checking
rollback remains possible
```

## v3.x — Proof Market / Distributed Proof Services

A network of nodes may advertise proof, bridge or oracle services.

This requires:

```text
node identity
capability advertisement
proof verification
trust policy negotiation
reputation/slashing or local trust policy
history-aware acceptance
```

This is intentionally outside MVP.

## v0.10 implemented seed: MigrationBridge

Implemented MVP surface:

```text
node_x86()
node_arm()
migrate(node, Source.value)
bridge Source_to_Target : Source -> Target { kind = migration }
```

This is not yet live migration. It is the first checker/runtime representation of node-aware remote values.


## v0.11 VirtualResourcePool

Added `VirtualCluster`, resource-aware node constructors, `virtual_pool(...)`, `pool_cores(...)` and `pool_memory_mib(...)`. This is the first MVP step toward the planned unified logical computer over many x86_64/aarch64 nodes while keeping node passports explicit. See `docs/VIRTUAL_RESOURCE_POOL.md`.


## v0.12 Scheduler seed

Implemented first explicit scheduler primitive:

```dlm
schedule_on(pool, node, value)
```

This is the bridge between `VirtualResourcePool` and real distributed runtime planning.
The next layers can build on it:

- scheduler policies;
- resource reservations;
- checkpoint/restore;
- remote materialization;
- live migration;
- node trust policies;
- cluster membership proofs.

## v0.13 completed foundation: DistributedMemoryRegion

The roadmap now includes an implemented MVP layer for logical distributed memory allocation:

```text
VirtualCluster -> DistributedMemory<MiB>
```

Next possible steps after v0.13:

```text
v0.14 — memory placement policies: pinned/spread/replicated
v0.15 — checkpoint/restore for DistributedMemory
v0.16 — materialize(remote) through an explicit bridge
v1.x  — consistency modes and actual distributed runtime backend
```


## v0.15 completed: RemoteCheckpoint / Live Migration Primitive

The roadmap now includes a concrete MVP primitive for remote job checkpoint/restore and live migration. This is not full OS process migration; it is the passport-level contract that preserves remote/local distinction and records history.


## v0.16 Remote materialization

Remote values now expose `can_materialize_remote`. Materialization is explicit and produces a local value while preserving construction/cost/trust/provenance/validation and appending `remote:materialize:<bridge>` to HistoryChain. Cross-theory materialization requires `BridgeKind::Materialize`.


## v0.17 Portable Code Deploy

Introduces `PortableCode<T>` and the deployment path `compile_portable(value) -> deploy_on(pool, node, code) -> Remote<T@arch>`. The checker keeps architecture/location differences visible even when the programmer treats the cluster as one logical computer.

## v0.18 GPU Virtual Memory

Adds `GpuDevice`, `GpuPool`, and `DistributedGpuMemory` as a separate passport axis from CPU RAM. This is the foundation for later GPU kernels, CPU↔GPU transfer bridges and heterogeneous accelerator scheduling.


## v0.20 completed: GPU kernels

`GpuKernel<T>` and `launch_kernel(...)` form the first accelerator execution slice. Future work: real backend selection, kernel arguments, memory coherence policy and GPU checkpointing.


## v0.22 Universe Levels

Added the first mathematical universe hierarchy layer:

- explicit `U0()`, `U1()`, `U2()` constructors;
- `Set<U n -> U n+1>` as a level-raising object;
- `Class<U n>` as a meta-level view;
- `UniverseLevelError` for bare universes and set-of-all-sets style mistakes;
- `HistoryChain` events for universe, set and class formation.

See `UNIVERSE_HIERARCHY.md`.
