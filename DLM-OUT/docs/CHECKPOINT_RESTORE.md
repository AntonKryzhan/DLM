# Checkpoint / Restore MVP v0.14

`v0.14` adds the first checkpoint/restore layer for distributed memory regions.

This is not full live process migration yet. It is the minimal passport-aware
state primitive needed before live migration:

```dlm
let mem = distributed_memory(pool, 4096)
let snap = checkpoint_memory(mem)
let restored = restore_checkpoint(snap)
```

## Core law

A checkpoint is not the original memory region. It is a restorable state object.

```text
DistributedMemory<M> --checkpoint_memory--> MemoryCheckpoint<M>
MemoryCheckpoint<M> --restore_checkpoint--> DistributedMemory<M>
```

The checker must preserve the history chain:

```text
... -> memory:distributed_region:4096MiB -> checkpoint:memory -> checkpoint:restore_memory
```

## Capabilities

`DistributedMemory` has:

```text
can_use_distributed_memory
can_checkpoint_memory
```

`MemoryCheckpoint` has:

```text
can_symbolic_print
can_restore_checkpoint
```

A checkpoint intentionally does not have `can_print_decimal` and is not usable as
local `Nat` or as a live memory region until restored.

## MVP builtins

```text
checkpoint_memory(region)
checkpoint(region)
checkpoint_region(region)

restore_checkpoint(snapshot)
restore_memory(snapshot)
restore(snapshot)
```

## Runtime representation

`dlm run` represents checkpoints symbolically:

```text
memory_checkpoint<memory_mib=4096>
```

and restores them back to:

```text
distributed_memory<memory_mib=4096>
```

## Future extension

Later versions should extend checkpoints with:

- node membership snapshot;
- page/segment metadata;
- epoch/expiry;
- cryptographic hash;
- node trust policy;
- live migration restore target;
- checkpoint diff / incremental checkpoint.
