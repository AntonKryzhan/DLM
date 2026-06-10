# DLM/ЯРД v0.21 — GPU Memory Checkpoint / Restore

This layer adds passport-aware checkpointing for GPU memory regions.

## New types

```text
GpuMemoryCheckpoint<memory_mib>
```

A `GpuMemoryCheckpoint` is not live GPU memory. It is a restorable snapshot of a
`DistributedGpuMemory` region.

## New functions

```dlm
checkpoint_gpu_memory(region)
gpu_checkpoint_memory(region)
checkpoint_vram(region)

restore_gpu_memory(snapshot)
restore_gpu_checkpoint(snapshot)
restore_vram(snapshot)
```

## Rules

```text
checkpoint_gpu_memory(x) requires x : DistributedGpuMemory and can_checkpoint_gpu_memory.
restore_gpu_memory(x) requires x : GpuMemoryCheckpoint and can_restore_gpu_memory_checkpoint.
GpuMemoryCheckpoint cannot be used as live VRAM until restored.
GpuMemoryCheckpoint cannot be printed as decimal.
```

## Passport law

GPU memory checkpoints preserve their history:

```text
gpu_memory:distributed_region:8192MiB -> checkpoint:gpu_memory -> checkpoint:restore_gpu_memory
```

This makes GPU checkpoint / restore compatible with later live migration and
failure recovery layers.
