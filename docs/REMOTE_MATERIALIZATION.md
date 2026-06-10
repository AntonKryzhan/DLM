# Remote Materialization

DLM/ЯРД v0.16 introduces explicit materialization of remote values.

A `Remote<T@arch>` is not a local `T`. It may be printed symbolically, checkpointed or live-migrated, but it cannot be used as a local value until an explicit materialization step is performed.

## Builtins

```dlm
materialize_remote(remote)
materialize(remote)
fetch_remote(remote)
collect_remote(remote)
```

All aliases perform the same MVP operation.

## Same-theory materialization

If the remote value is already inside the ambient theory, no cross-theory bridge is required:

```dlm
theory Cluster {
    let node = node_x86_64_with(4, 8192)
    let pool = virtual_pool(node)
    let payload = 17
    let job = schedule_on(pool, node, payload)
    let back = materialize_remote(job)

    print_symbolic(job)  // remote[x86_64](17)
    print_decimal(back)  // 17
}
```

## Cross-theory materialization

If the remote value lives in another theory, a dedicated bridge is required:

```dlm
bridge Cluster_to_Return : Cluster -> Return {
    kind = materialize
}

theory Return {
    let back = materialize_remote(Cluster.job)
}
```

This is intentionally separate from `migration`, `transport` and `soundness`.

## Passport law

Materialization appends a history event:

```text
remote:materialize:<bridge>
```

The resulting value becomes local, but it keeps the original construction, cost, trust, provenance and validation taint.

For example, a remote literal `Nat` may become local and decimal-printable again, while a remote noncomputable `Nat` must remain non-decimal-printable after materialization.

## Safety law

Remote materialization is not implicit.

```dlm
print_decimal(remote)
```

remains invalid. The programmer must write:

```dlm
let local = materialize_remote(remote)
print_decimal(local)
```

and, across theory boundaries, must provide a `materialize` bridge.
