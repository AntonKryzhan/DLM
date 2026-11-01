# v0.72 — Example Corpus Integrity / GPU Checkpoint Restore Completion

`v0.72.0` closes gaps found by checking the whole example corpus instead of individual examples.

## Problems found in v0.71

```text
examples/valid/gpu_checkpoint_restore_memory.dlm  rejected: checkpoint_gpu_memory / restore_gpu_memory not implemented
examples/valid/infinity_modes.dlm                 rejected: bridge Core_quote targets theory Meta, which was missing
examples/invalid/checkpoint_gpu_requires_gpu_memory.dlm  rejected for the wrong reason (unknown builtin)
examples/invalid/print_decimal_gpu_checkpoint.dlm        rejected for the wrong reason (unknown builtin)
examples/invalid/restore_gpu_requires_checkpoint.dlm     rejected for the wrong reason (unknown builtin)
dlm --version                                     printed 0.33.0
docs/MANIFEST.json                                4 entries with a docs/ prefix, 3 documents missing
```

The three invalid examples looked like passing negative tests, but they only failed because the checker did not know the
functions. The documented v0.21 rules (`docs/GPU_CHECKPOINT_RESTORE.md`) were never enforced.

## GPU memory checkpoint / restore

New type and capability:

```text
GpuMemoryCheckpoint<memory_mib>
can_restore_gpu_memory_checkpoint
```

Functions (checker and runtime):

```text
checkpoint_gpu_memory(region) | gpu_checkpoint_memory(region) | checkpoint_vram(region)
restore_gpu_memory(snapshot)  | restore_gpu_checkpoint(snapshot) | restore_vram(snapshot)
```

Rules:

```text
checkpoint_gpu_memory(x) requires x : DistributedGpuMemory and can_checkpoint_gpu_memory.
restore_gpu_memory(x) requires x : GpuMemoryCheckpoint and can_restore_gpu_memory_checkpoint.
A CPU MemoryCheckpoint cannot be restored into GPU memory.
GpuMemoryCheckpoint carries no use/launch/copy/peer capabilities: it is not live VRAM.
GpuMemoryCheckpoint has no can_print_decimal.
Trust, provenance and validation are copied from the source region; taint is never cleaned.
```

Passport history:

```text
gpu_memory:distributed_region:<N>MiB -> checkpoint:gpu_memory -> checkpoint:restore_gpu_memory
```

Runtime values:

```text
gpu_memory_checkpoint<memory_mib=N>
distributed_gpu_memory<memory_mib=N>   (after restore)
```

## Corpus integrity test

`crates/dlm_core/tests/example_corpus.rs` enforces:

```text
every examples/valid/*.dlm parses and checks under the research policy;
every valid example runs, except the explicit CHECK_ONLY_VALID list;
  every listed check-only example must still fail at runtime, so the list cannot go stale;
every examples/invalid/*.dlm is rejected in its intended mode:
  *_rejected_by_trusted_only -> passes research, rejected by trusted_only;
  RUNTIME_FAILURE_INVALID   -> passes check, fails at runtime;
  everything else           -> rejected by the checker;
no invalid example may be rejected only by "unknown builtin or function" / "unknown name";
docs/MANIFEST.json lists every docs/*.md by bare file name.
```

Adding a new example therefore requires no test edits unless it is check-only or a runtime-only negative case; those must be
added to the explicit lists.

## Commands

```text
cargo test -p dlm_core --test gpu_checkpoint_restore
cargo test -p dlm_core --test example_corpus
cargo test -p dlm_cli --test cli_version
```

## Law

```text
A negative example must fail for the documented reason, not because a feature is missing.
```
