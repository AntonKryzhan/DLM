# DLM/ЯРД v0.11 — VirtualResourcePool

This document defines the first executable MVP layer for treating many hosts as one logical computer without hiding node-level distinctions from the checker.

## Law

A DLM program may view a cluster as a single virtual resource pool, but the compiler must preserve node architecture, trust, location, memory, core count and migration history.

```text
programmer view: one virtual computer
checker view: passported distributed cluster
```

## MVP constructors

```dlm
let x86 = node_x86_64_with(8, 32768)
let arm = node_aarch64_with(16, 65536)
let pool = virtual_pool(x86, arm)
```

`node_x86_64_with(cores, memory_mib)` and `node_aarch64_with(cores, memory_mib)` require literal positive resources in v0.11.

## MVP resource queries

```dlm
let cores = pool_cores(pool)
let memory = pool_memory_mib(pool)
```

Both return local `Nat` values with `can_print_decimal` because they are finite metadata computed by the runtime from the cluster passport, not remote heap data.

## Capabilities

A `VirtualCluster` receives:

```text
can_host_runtime
can_symbolic_print
can_virtualize_cores
can_virtualize_memory
can_schedule_runtime
```

A plain `Node` is not a pool. A remote value is not local memory. A future materialization bridge will be required before using remote state as a local value.

## HistoryChain

`virtual_pool(...)` appends:

```text
cluster:virtual_pool
```

Node constructors append:

```text
node:<arch>
node_resource:cores:<n>
node_resource:memory_mib:<m>
```

## MVP boundaries

v0.11 does not implement real distributed execution. It introduces the static and runtime model needed for later scheduling, checkpoint/restore and live migration.
