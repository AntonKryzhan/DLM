# DLM/ЯРД v0.12 — Passport-aware scheduler seed

Status: implemented as an MVP seed in v0.12.

This layer makes `VirtualCluster` useful as more than an aggregate resource counter.
It introduces explicit scheduling onto a selected node inside a virtual resource pool.

## Surface

```dlm
let job = schedule_on(pool, node, value)
```

Alias:

```dlm
let job = schedule(pool, node, value)
```

The result is a remote value:

```text
Remote<T@arch>
```

For example:

```dlm
module examples.schedule_on_virtual_pool

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let payload = 9
}

theory Cluster {
    let x86 = node_x86_64_with(4, 8192)
    let arm = node_aarch64_with(8, 16384)
    let pool = virtual_pool(x86, arm)

    let job = schedule_on(pool, arm, Local.payload)
    print_symbolic(job)
}
```

Runtime output:

```text
remote[aarch64](9)
```

## Static requirements

`pool` must be a `VirtualCluster` with:

```text
can_schedule_runtime
```

`node` must be a `Node<arch>` with:

```text
can_accept_migration
```

`value` must have:

```text
can_serialize_for_migration
```

If `value` comes from a different theory, a migration bridge must be in scope:

```dlm
bridge Source_to_Target : Source -> Target {
    kind = migration
}
```

Same-theory scheduling uses the internal bridge name:

```text
local_schedule
```

## Runtime membership check

The static checker verifies the shape and capabilities of the pool and target node.
The runtime additionally verifies that the selected node is actually a member of the runtime `VirtualCluster`.

This is intentional: v0.12 does not yet encode cluster membership into dependent types.
That belongs to a later `ClusterProof` / `ResourceProof` layer.

## Capabilities of scheduled values

A scheduled value is remote. It keeps symbolic visibility but loses local capabilities:

```text
Remote<T@arch>:
  can_symbolic_print
  can_remote_symbolic_print
```

It does not keep:

```text
can_print_decimal
can_add_as_nat
can_compare_direct
```

Those require future materialization/remoting bridges.

## HistoryChain

Scheduling appends an explicit history event:

```text
cluster:schedule:<bridge>:to:<arch>
```

The scheduled passport merges history from:

```text
pool + target node + source value
```

This prevents scheduled values from forgetting their resource and migration origin.
