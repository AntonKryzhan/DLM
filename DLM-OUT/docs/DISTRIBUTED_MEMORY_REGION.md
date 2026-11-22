# DLM/ЯРД v0.13 — DistributedMemoryRegion

`DistributedMemoryRegion` is the first concrete step toward the long-term goal where thousands of machines appear as one logical computer while the compiler still preserves node, architecture, memory and trust distinctions.

## Core law

```text
A programmer may allocate a logical memory region from a VirtualCluster.
The checker must still remember that the region is distributed, bounded by cluster memory, and not equal to local RAM.
```

## New type

```text
DistributedMemory<MiB>
```

In MVP this is a symbolic runtime object. It is not raw addressable memory and it cannot be used as a local pointer.

## New builtins

```dlm
distributed_memory(pool, memory_mib)
allocate_memory(pool, memory_mib)
memory_region(pool, memory_mib)

memory_region_mib(region)
distributed_memory_mib(region)
```

`distributed_memory(pool, memory_mib)` requires:

```text
pool : VirtualCluster
pool.capabilities contains can_allocate_distributed_memory
memory_mib is a positive literal Nat in MVP
memory_mib <= total pool memory when the total is statically known
```

`memory_region_mib(region)` requires:

```text
region : DistributedMemory
region.capabilities contains can_use_distributed_memory
```

## Capabilities

New capabilities:

```text
can_allocate_distributed_memory
can_use_distributed_memory
can_checkpoint_memory
```

A `VirtualCluster` receives `can_allocate_distributed_memory`.
A `DistributedMemory` region receives `can_use_distributed_memory` and `can_checkpoint_memory`.

## HistoryChain events

```text
memory:distributed_region:<N>MiB
memory:region_mib
```

These events make it impossible for a memory region to forget the virtual pool and node resources that produced it.

## Runtime behavior

`dlm run` v0.13 still does not expose pointers or shared mutable memory. It only creates and prints a symbolic region:

```text
distributed_memory<memory_mib=49152>
```

This is intentional. Addressable distributed memory requires a future consistency model.

## Future layers

Future versions may add:

```text
DistributedMemoryConsistency: local | eventual | quorum | linearizable
MemoryPlacementPolicy: spread | pinned | replicated | erasure_coded
CheckpointBridge
RestoreBridge
Remote materialization bridge
Distributed heap / object store
```
