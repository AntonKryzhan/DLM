# GPU Kernels / Accelerator Execution

DLM/ЯРД v0.20 introduces the first passport-aware GPU kernel layer.

## Core law

GPU memory and GPU execution remain separate from CPU values:

```text
Nat -> compile_gpu_kernel(...) -> GpuKernel<Nat>
GpuKernel<Nat> + DistributedGpuMemory -> launch_kernel(...) -> GpuValue<Nat>
GpuValue<Nat> -> copy_from_gpu(...) -> Nat
```

A `GpuKernel<T>` is not a CPU `T` and cannot be printed as a decimal value.
A `GpuValue<T>` is GPU-resident and must be explicitly copied back before CPU-side value operations.

## MVP functions

```text
compile_gpu_kernel(value)
gpu_kernel(value)
make_gpu_kernel(value)

launch_kernel(gpu_memory, kernel)
launch_gpu_kernel(gpu_memory, kernel)
gpu_launch(gpu_memory, kernel)
```

## Capabilities

```text
can_compile_gpu_kernel
can_launch_gpu_kernel
can_use_gpu_memory
can_copy_gpu_to_cpu
```

## History events

```text
gpu_kernel:compile
gpu_kernel:launch
copy:gpu_to_cpu
```

## Restrictions

- `compile_gpu_kernel(...)` requires a serializable/compilable source value.
- `launch_kernel(...)` requires `DistributedGpuMemory` as the first argument.
- `launch_kernel(...)` requires `GpuKernel<T>` as the second argument.
- `print_decimal(GpuKernel<T>)` is forbidden.
- `print_decimal(GpuValue<T>)` is forbidden.
