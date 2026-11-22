# DLM/ЯРД v0.15 — Remote Checkpoint / Restore / Live Migration

Status: MVP layer.

This layer adds the first verified building blocks for moving already scheduled
remote runtime objects between nodes without treating them as local values.

## New types

```text
Remote<T@arch>
RemoteCheckpoint<T@arch>
```

`Remote<T@arch>` is a value that lives on a remote node architecture. It is not a
local `T` and it intentionally does not have local capabilities such as
`can_print_decimal`.

`RemoteCheckpoint<T@arch>` is a restorable snapshot of a remote value. It can be
printed symbolically and restored to a target node, but it is not executable as a
local value.

## New builtins

```dlm
checkpoint_remote(remote)
checkpoint_job(remote)
checkpoint_remote_job(remote)

restore_remote(node, checkpoint)
restore_job(node, checkpoint)
restore_remote_checkpoint(node, checkpoint)

live_migrate(node, remote)
live_migrate_remote(node, remote)
```

## Passport laws

`checkpoint_remote(remote)` requires:

```text
remote : Remote<T@arch>
remote has can_checkpoint_remote
```

It returns:

```text
RemoteCheckpoint<T@arch>
capabilities = { can_symbolic_print, can_restore_remote_checkpoint, can_cross_arch_portable }
history += checkpoint:remote
```

`restore_remote(node, checkpoint)` requires:

```text
node has can_accept_migration
checkpoint has can_restore_remote_checkpoint
checkpoint : RemoteCheckpoint<T@arch>
```

It returns:

```text
Remote<T@node_arch>
history += checkpoint:restore_remote:to:<node_arch>
```

`live_migrate(node, remote)` requires:

```text
node has can_accept_migration
remote has can_live_migrate_remote
remote : Remote<T@arch>
```

It returns:

```text
Remote<T@node_arch>
history += migration:live_remote:to:<node_arch>
```

## Important non-goals

v0.15 does not implement true OS-level process migration. It implements the
passport and runtime model that prevents a remote value, checkpoint or migrated
job from being confused with a local value.

The next distributed layers may add:

- RuntimeState capsules;
- node membership checks for restore targets;
- checkpoint serialization;
- epoch/expiry for runtime and node claims;
- actual network transport.


## v0.16 Remote materialization

Remote values now expose `can_materialize_remote`. Materialization is explicit and produces a local value while preserving construction/cost/trust/provenance/validation and appending `remote:materialize:<bridge>` to HistoryChain. Cross-theory materialization requires `BridgeKind::Materialize`.
