# DLM/ЯРД Distributed Virtual Cluster — MVP seed

Status: v0.10 code-level seed.

This document defines the first implementation slice of the future distributed runtime layer.
The long-term goal is a single logical DLM computer spanning many machines, CPU cores and memory pools, while the checker still preserves node, architecture, trust, provenance and migration distinctions.

## Core law

The programmer may see one virtual computer.
The checker must not forget that execution is distributed.

Therefore a remote value is not the same thing as a local value:

```dlm
let remote = migrate(node_arm(), Local.n)
```

`remote` is `Remote<Nat@aarch64>`, not `Nat`.
It can be printed symbolically, traced and migrated further in future versions, but it is not decimal-printable as a local `Nat` unless a future materialization bridge proves that this is safe.

## MVP v0.10 implemented surface

```dlm
node_x86() / node_x86_64()   -> Node<x86_64>
node_arm() / node_aarch64()  -> Node<aarch64>
migrate(node, Source.value)  -> Remote<T@arch>
```

Migration requires an explicit bridge:

```dlm
bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}
```

Example:

```dlm
module examples.migration_bridge

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

Runtime output:

```text
remote[aarch64](7)
```

## Passport effects

A node carries:

```text
TypeKind::Node { arch }
capabilities:
  can_host_runtime
  can_accept_migration
  can_symbolic_print
  can_cross_arch_portable
location=node<arch>
history=node:<arch>
```

A migrated value carries:

```text
TypeKind::Remote { inner, target_arch }
capabilities:
  can_symbolic_print
  can_remote_symbolic_print
location=remote<arch>
history += migration:<bridge>:to:<arch>
```

A migrated value intentionally loses direct local capabilities such as `can_print_decimal`.
This prevents the checker from pretending that remote memory is local memory.

## Rejected examples

No migration bridge:

```dlm
let remote = migrate(node_arm(), Local.n)
```

without:

```dlm
bridge Local_to_Cluster : Local -> Cluster { kind = migration }
```

must produce `MigrationBridgeError`.

Migration target is not a node:

```dlm
let remote = migrate(1, Local.n)
```

must fail.

Decimal print of remote value:

```dlm
print_decimal(remote)
```

must fail unless a later materialization bridge is introduced.

## Future layers

v0.10 is only a static/runtime seed. Full distributed execution needs:

- RuntimeState serialization;
- heap and call-stack checkpoints;
- NodePassport and HostPassport metadata;
- architecture-specific executable backends;
- portable DLM bytecode / portable IR;
- consistency modes for distributed memory;
- migration rollback;
- node trust policy;
- proof and witness mobility;
- Epoch / Proof Expiry;
- cluster scheduler.



## v0.11 VirtualResourcePool

Added `VirtualCluster`, resource-aware node constructors, `virtual_pool(...)`, `pool_cores(...)` and `pool_memory_mib(...)`. This is the first MVP step toward the planned unified logical computer over many x86_64/aarch64 nodes while keeping node passports explicit. See `docs/VIRTUAL_RESOURCE_POOL.md`.



## v0.17 Portable Code Deploy

Introduces `PortableCode<T>` and the deployment path `compile_portable(value) -> deploy_on(pool, node, code) -> Remote<T@arch>`. The checker keeps architecture/location differences visible even when the programmer treats the cluster as one logical computer.
