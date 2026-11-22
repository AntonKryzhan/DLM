# DLM/ЯРД v0.18 — GPU Virtual Memory

## Purpose

`DistributedMemory` models cluster RAM. GPU memory must not be collapsed into the same type because VRAM/HBM has a different access model, backend, locality and synchronization cost.

v0.18 introduces a separate passport layer:

```text
GpuDevice<backend>
GpuPool
DistributedGpuMemory<memory_mib>
```

This keeps the programmer-facing model close to “one virtual computer”, while the checker keeps the differences between CPU RAM and GPU memory explicit.

## New constructors

```dlm
gpu_cuda()
gpu_cuda_with(memory_mib)
gpu_rocm()
gpu_rocm_with(memory_mib)

gpu_pool(gpu0, gpu1, ...)
distributed_gpu_memory(gpu_pool, memory_mib)
allocate_gpu_memory(gpu_pool, memory_mib)
gpu_memory_region(gpu_pool, memory_mib)

gpu_memory_mib(region)
distributed_gpu_memory_mib(region)
```

## New types

```text
GpuDevice<cuda>
GpuDevice<rocm>
GpuPool
DistributedGpuMemory<MiB>
```

## Capabilities

```text
can_host_gpu_runtime
can_allocate_gpu_memory
can_use_gpu_memory
can_checkpoint_gpu_memory
can_launch_gpu_kernel
can_copy_cpu_to_gpu
can_copy_gpu_to_cpu
can_gpu_peer_transfer
can_gpu_unified_addressing
```

In v0.18 only the allocation/query subset is executable. Kernel launch and CPU/GPU copy capabilities are reserved for later versions.

## Safety rule

```text
DistributedMemory != DistributedGpuMemory
```

CPU distributed memory and GPU distributed memory are separate passport types. A CPU memory operation must not accept a GPU region, and a GPU memory operation must not accept a CPU region unless a future explicit transfer bridge is supplied.

## Example

```dlm
module examples.gpu_memory_pool

theory Cluster {
    let gpu0 = gpu_cuda_with(24576)
    let gpu1 = gpu_cuda_with(24576)
    let gpool = gpu_pool(gpu0, gpu1)
    let vram = distributed_gpu_memory(gpool, 32768)
    let cap = gpu_memory_mib(vram)

    print_decimal(cap)
    print_symbolic(gpool)
    print_symbolic(vram)
}
```

Runtime output:

```text
32768
gpu_pool<devices=2, memory_mib=49152>
distributed_gpu_memory<memory_mib=32768>
```

## MVP limitations

v0.18 does not execute real GPU kernels. It only creates checked GPU resource passports and enforces that GPU memory is not silently treated as CPU RAM.

Future layers:

```text
v0.19 — CPU↔GPU transfer bridge
v0.20 — GpuKernel / launch_kernel
v0.21 — GPU checkpoint / peer migration
v0.22 — backend-specific capabilities: CUDA / ROCm / Vulkan / Metal
```
