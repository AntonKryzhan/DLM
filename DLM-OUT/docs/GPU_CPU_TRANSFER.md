# GPU ↔ CPU Transfer Bridge v0.19

DLM/ЯРД v0.19 adds the first explicit CPU↔GPU transfer layer.

The rule is deliberately conservative:

```text
CPU RAM / Nat / local values are not GPU values.
GPU values are not local CPU values.
A transfer must be explicit and passport-checked.
```

## New concepts

```text
GpuValue<T>
```

A `GpuValue<T>` represents a value that has been copied into a `DistributedGpuMemory` region.
It may be symbolically printed, transferred back to CPU, and later used by GPU-kernel layers.
It is not a local CPU value.

## New builtins

```dlm
copy_to_gpu(value, distributed_gpu_memory)
gpu_upload(value, distributed_gpu_memory)
upload_to_gpu(value, distributed_gpu_memory)

copy_from_gpu(gpu_value)
gpu_download(gpu_value)
download_from_gpu(gpu_value)
```

## Capabilities

`copy_to_gpu` requires:

```text
source: can_serialize_for_migration
region: DistributedGpuMemory + can_use_gpu_memory + can_copy_cpu_to_gpu
```

`copy_from_gpu` requires:

```text
value: GpuValue<T> + can_copy_gpu_to_cpu
```

## Example

```dlm
module examples.gpu_cpu_transfer

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0)
    let vram = distributed_gpu_memory(gpool, 8192)

    let payload = 55
    let on_gpu = copy_to_gpu(payload, vram)
    let back = copy_from_gpu(on_gpu)

    print_symbolic(on_gpu)
    print_decimal(back)
}
```

Expected output:

```text
gpu_value<memory_mib=8192>(55)
55
```

## Law

```text
A GPU-resident value must not silently behave like a CPU-local value.
It must be explicitly downloaded/materialized through copy_from_gpu.
```
