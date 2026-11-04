# Native application foundation (v0.133.0)

The explicit `native { ... }` execution domain provides machine values,
mutable control flow, dense owned storage, C ABI functions and user resources.
The [acceptance report](NATIVE_ACCEPTANCE_RU.md) maps the implementation and
executed checks to all eighteen supplied requirements.

The ordinary DLM surface, `Nat`, `Int`, `Float`, immutable `FloatVec`, `ByteBuf`,
`TextBuf`, `File`, proof checking and proof erasure retain their existing contracts.
Native functions have checked types and execution contracts; they are not
mathematical theorems and do not generate proof certificates.

## Build and run

The patch includes the updated Windows compiler in `DLM-OUT/bin/dlm.exe`.
The existing SDK C toolchain is used for application builds. For source builds,
use Cargo; `DLM-pre/Build-DLM.cmd` rebuilds the source and refreshes the Windows SDK.

```text
cargo build --release --workspace

dlm check examples/native/foundation.dlm
dlm run examples/native/foundation.dlm
dlm build examples/native/foundation.dlm -o foundation

dlm run examples/native/serialization.dlm --lib lib

dlm build examples/native/component.dlm --allow-unsafe --target sharedlib -o component.so
dlm build examples/native/component.dlm --allow-unsafe --target staticlib -o component.a
dlm build examples/native/component.dlm --allow-unsafe --target plugin --plugin-profile c-abi-v1 -o component.plugin
```

Use `.dll` for a Windows shared library. The compiler must be GCC-compatible
and support C11 and `__int128` (the same general toolchain requirement as the
ordinary backend). The native ABI profile requires x86-64. `CC` selects the C
compiler and `AR` selects the static archiver.

Windows executable output without an extension receives `.exe`; the build
result reports that actual path. Deploy the selected compiler's matching runtime
DLLs with native artifacts and include their directory in the host's DLL search
path. The DLM runtime itself is compiled into each native artifact.

Build options include `--cpu auto|scalar|sse2|avx2|avx512`, `--c-only`, `--emit-ir`,
`--library NAME`, `--library-dir DIR`, `--link-object FILE` and repeated
`--cflag FLAG`. Arguments are passed directly to the compiler without a shell.

Library builds also write a C header next to the artifact. `plugin` packages the
shared library, C source, header and a `c-abi-v1` manifest in a directory. This is
a generic C module profile, not an implementation of a particular host protocol.
The ordinary `--require-no-overflow` interval proof pass remains specific to
ordinary `Nat`/`Int`; native code uses the explicit machine arithmetic semantics
below and rejects that flag rather than silently claiming a range proof.

## Source and machine values

```dlm
module example
native {
    export fn affine(x: F64, scale: F64, bias: F64) -> F64
        guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        return x * scale + bias;
    }
    fn main() -> Unit {
        let mut sum: F64 = 0.0;
        for index in 0..100 { sum = sum + affine(cast<F64>(index), 2.0, 1.0); }
        print(sum);
    }
}
```

Types: `U8/U16/U32/U64`, `I8/I16/I32/I64`, `F32/F64`, `USize/ISize`, `Bool`,
`Unit` and immutable literal-backed `Utf8`. Pointer-sized integers are 64 bits
in this profile. Machine type names have meaning inside native declarations;
ordinary `Float` remains its existing independent surface type.

Integer `+ - * / %`, negation and shifts check their defined error conditions.
Overflow, division by zero, an invalid shift count and `MIN / -1` report errors.
`wrapping_add/sub/mul` implement arithmetic modulo the destination width;
`saturating_add/sub/mul` clamp to the destination range. Bitwise operators are
`& | ^ ~`; left shift discards shifted-out bits and signed right shift extends
the sign. Shift counts must lie in `0..width`.

`cast<T>(value)` and numeric constructors perform explicit conversions. Integer
narrowing is range checked; float-to-integer conversion requires a finite,
integral, in-range value. Use `trunc`, `floor` or `ceil` before conversion when
rounding is intended. Floating conversions round to the destination IEEE format.

`bits`, `from_bits`, `nan`, `infinity`, `is_nan`, `is_finite`, `sign_bit` and the
usual scalar math operations are available. `from_bits`/`bits` preserve raw
floating representations. Arithmetic NaN payload propagation is not an API
promise. F32 arithmetic rounds after each operation. F32 literals and integer
conversions round directly to binary32, without an intermediate binary64 rounding.
Contraction and fast-math are disabled after user C flags are applied.
ABI entries establish the default floating environment,
preserve subnormals and restore the caller's environment on return and error.
Transcendental operations use the target platform's math library.

`print` uses 17 significant digits for floating values, with a decimal point,
`nan`, `inf` and `-inf`. This is a native machine-value display contract; the
ordinary `Float` display contract is unchanged.

Native function names form one namespace across imported native blocks. An
imported module's native `main` is not selected as the application's entry.
Ordinary mathematical declarations may coexist with native declarations.
An explicit `ordinary "Theory.function" fn ...;` declaration converts compatible
values at the call boundary. The ordinary target and its initializers are checked
independently and must be pure; an ordinary `main` cannot coexist with a native
entry. Existing ordinary generics retain their behavior. Native functions have
concrete machine types.

```dlm
theory Math {
    fn twice(x: Float) -> Float = fl_mul(x, fl_of_nat(2))
}
native {
    ordinary "Math.twice" fn old_twice(x: F64) -> F64;
    fn main() -> Unit { print(old_twice(3.0)); }
}
```

Conversions cover unsigned scalars/Nat, signed scalars/Int, F32/F64/Float,
Bool, UTF-8/Text, float and unsigned dense vectors, and records with matching
field names and compatible types. Narrowing and invalid UTF-8 report errors.
Use shared references for owning input containers. Returned text/vectors own
their native copies. Ordinary globals are initialized in a call-local region;
bridge calls are serialized and conservatively allocate, may block/fail, and
have unbounded work. Numeric native functions continue to use unboxed values.
See `examples/native/ordinary_bridge.dlm` for scalar and aggregate conversion.

## Mutation, dense memory and lifetimes

Native statements include typed `let`, `let mut`, assignments to variables,
fields and elements, `if/else`, `while`, `for name in start..end`, nested blocks,
`return`, `break` and `continue`. A for bound is evaluated once; its index is an
immutable `USize`. Existing ordinary tail-recursive `loop` functions are unchanged.
Assignment evaluates its right-hand value before selecting the destination;
indexing evaluates the storage before the index and rejects an index expression
that could invalidate that storage.

| Type | Representation / ownership |
| --- | --- |
| `Array<T,N>` | Fixed, inline, contiguous elements |
| `Buffer<T>` | Owned pointer, length and capacity; contiguous elements |
| `Box<T>` | Owned allocation containing T |
| `Slice<T>` | Shared borrowed pointer and length |
| `MutSlice<T>` | Exclusive borrowed pointer and length |
| `Split<T>` | Two disjoint mutable slices from one checked split |
| `&T` / `&mut T` | Shared / exclusive reference |
| `BorrowedPtr<T>` | A pointer tied to an explicit reference lifetime |
| `ForeignBuffer<T>` | Exclusive borrowed external storage; never freed by DLM |
| `ThreadBound<T>` | Affine state with a checked owning thread |
| `Resource<T,finalizer>` | Owned T with a checked user finalizer |

Creation and access examples:

```dlm
let mut values: Buffer<F64> = buffer<F64>(64);
values[0] = 1.5;
resize(&mut values, 128);
let fixed: Array<F64,3> = array<Array<F64,3>>(1.0, 2.0, 3.0);
{
    let mut parts: Split<F64> = split_at_mut(&mut values, 32);
    parts.left[0] = 5.0;
    parts.right[0] = 7.0;
}
```

Numerical elements have no dynamic value header or passport. `push` grows a
buffer and transfers an element. `buffer_aligned<T>(count, alignment)` validates
and preserves the requested alignment through resize/growth. Array initializers
supply exactly N elements; N must be in `1..=268435456` and the complete layout
must fit the checked target size. `zero<T>()` and zero-initialized buffers require valid
zero representations. Types such as `NonNull`, function pointers and
`ThreadBound` cannot be fabricated by zero initialization. Create an empty
buffer and `push` initialized values when an element has no zero value.
Default resize cannot grow such an element type.

Owned values require explicit `move` at transfers. Records can own multiple
buffers/boxes and support partial field moves. All remaining owned fields are
dropped, including on early exits and native errors. Numeric buffer writes are
O(1); slices do not copy. Moving nested owning aggregates may traverse resource
allocations to update error-cleanup bookkeeping; moving a numeric buffer is O(1).

Borrows are checked conservatively at lexical scope boundaries. Shared aliases
cannot overlap an exclusive alias. Constant disjoint slice intervals and
`split_at_mut` establish disjointness. The checker rejects use after move,
incompatible reborrows, escaping a local view and assignment through a shared
view. A returned borrow must name its input lifetime:

```dlm
fn first(bytes: &Buffer<U8>) -> Slice<U8> borrows(bytes) {
    return slice(bytes, 0, 1);
}
```

Borrowed fields inside records, arbitrary lifetime parameters and a complete
non-lexical lifetime system are not implemented. Bind owning temporaries before
projecting/indexing them. Unsafe blocks do not establish arbitrary safe alias or
lifetime proofs.

A recognized `for i in ...len(storage)` traversal can carry a checked
`IndexProven` operation. The independent verifier reconstructs the bound; it does
not trust a caller-supplied opcode. Resizing, consuming or reborrowing the
storage in a way that invalidates that bound is rejected. Unsafe loops retain
checks. Compatible loops can also be optimized by the C compiler under the
selected strict numerical semantics.

## ABI, FFI and callbacks

```dlm
native {
    trust "fixture";
    record Packet repr(C, version=1) { tag: U32; value: F64; }
    extern "C" fn fixture_sum(packet: Packet, data: Ptr<F64>, count: USize) -> F64
        from "fixture" guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe);
}
```

External dependencies must be named in the trust list. FFI calls, raw memory,
external handles and explicit unsafe blocks require `--allow-unsafe`. Foreign
execution guarantees are trusted declarations, not proofs of foreign code.
Object/library files are linked with the explicit build options.

Imports use distinct C identifiers with the requested linker symbols, so a
system header's declaration does not conflict with the native declaration.
The author remains responsible for matching the foreign ABI, string encoding,
termination, nullability, aliasing and allocation/freeing conventions. C varargs,
C++ object ABI and arbitrary calling conventions are not supported.

`Ptr<T>`, `NonNull<T>` and `OpaqueHandle` have pointer representation.
`nonnull` checks a nullable pointer. `ptr_offset` checks address arithmetic;
physical allocation bounds and external validity remain unsafe preconditions.
`foreign(pointer, length)` creates a borrowed zero-copy external view. It never
adopts or frees the external allocation. `from_raw` requires unique ownership
of a compatible DLM allocation; it is not an adapter for arbitrary C malloc.
`into_raw` transfers ownership out of a box. `borrowed_ptr` creates a lifetime
tracked pointer, and `as_raw` explicitly crosses out of that lifetime guarantee.

Records have checked order, size, alignment and offsets. `repr(C, packed)` and
`repr(C, align=64)` are explicit. Unions require unsafe field access and cannot
hide owning resources without an active-variant model. C compiler static
assertions check generated layouts. The library exports size/alignment/offset
and version queries. Headers expose ABI value types and forward-declare opaque
record pointees. `Unit` results use C `void`; fixed-width integer types stay fixed.

`export fn` emits a fixed C symbol; `export "symbol" fn` chooses its name.
`FnPtr<P1,...,R>` and `fn_ptr(function)` support typed function-pointer tables
and external callbacks with explicit userdata. Each DLM callback has its own ABI
error boundary, including calls from foreign threads and nested entry.

An unknown indirect target has conservative effects. To invoke a dynamic host
callback under checked execution guarantees, use
`call_trusted(contract, pointer, arguments...)` in unsafe code. `contract` names
a trusted extern declaration with the exact C signature and explicit guarantees;
its symbol need not be linked when the declaration is used only as a contract.
The pointer must denote a live, non-null function obeying that contract. Known
native targets still contribute their actual effects, so a contradictory
`fn_ptr(allocating_function)` cannot satisfy `NoAlloc`. Ordinary `FnEffect`
continues to describe ordinary callbacks; C function pointers keep a distinct
ABI representation.


Native failures return a zero result (or return from a void function), and
`dlm_native_last_error()` reports the thread-local reason. Check it immediately;
the next top-level call clears it. Owned locals are cleaned on the error path.
Changes already committed to a borrowed/retained object remain committed; the
object stays valid. This is not rollback of application state.

A `Resource<T,finalizer>` automatically calls a native finalizer with signature
`(&mut T) -> Unit`, then drops all owned payload fields. The checker verifies
that the finalizer cannot raise a native error; its other effects propagate to
owners and callers. Finalization covers ordinary scope exit, early return,
replacement, moved temporaries and ABI error unwinding.

`resource(value)` creates an annotated resource. Its control allocation precedes
evaluation of the payload: `resource(acquire_foreign_handle())` cannot lose a
newly acquired handle because a later control allocation fails. Wrap each foreign
acquisition immediately; a raw handle inside a larger fallible expression does
not itself carry ownership. `resource_get(&owner)` borrows the payload.
`resource_get_mut(&mut owner)` is unsafe because the caller must preserve the
finalizer invariant. `resource_close(move owner)` closes early;
unsafe `resource_into_inner(move owner)` transfers the payload and cancels the
finalizer. The payload cannot hide borrowed lifetimes.

`examples/native/managed_callbacks.dlm` owns a registration token together with
its boxed context. Its finalizer calls the trusted host cancellation function,
which must stop future invocations and wait for all active calls before it
returns. Only then is the context released. No type checker can prove that an
arbitrary C library obeys its declared cancellation contract. The independent
C host exercises four callback threads, two contexts, failure cleanup and
shutdown/restart.

Use explicit owned user objects as library instances (`component.dlm`). Native
application state has no `G[]` global array. A versioned C-compatible function
table is exported by `component_api()`. `size_of<T>()`, `align_of<T>()` and
`abi_version<Record>()` expose the checked representation to native source.
The GUI registry is protected; each window retains its owner thread. Linux
shared modules bind their own exported functions even under `RTLD_GLOBAL`.
Independently emitted static runtimes still require distinct symbol namespaces
if linked into one executable.

`dlm_native_runtime_init()` is idempotent. `dlm_native_runtime_shutdown()` refuses
to close while exported calls or owned allocations remain; a failed attempt
leaves the runtime usable. Successful shutdown closes the renderer's module
resources and rejects subsequent ABI entries until initialization. Callers must
synchronize library unloading and stop/join foreign callback producers first.
`dlm_native_live_allocations()` counts native allocations across threads; the
allocation/free counters remain per thread.

## Execution contracts, SIMD and threads

Effects propagate to a fixed point through direct calls. Recursive call cycles
are not declared bounded merely because an annotation says so.

| Guarantee | Checked meaning |
| --- | --- |
| `NoAlloc` | No reachable native heap-allocation operation or untrusted allocating call |
| `NoBlocking` | No allocation/freeing or other known potentially blocking operation |
| `NoIO` | No IO operation or foreign call lacking the trusted contract |
| `NoUnwind` | No native error-raising operation in the analyzed call graph |
| `BoundedWork` | Finite for traversals and a non-recursive call graph in the operation-count model |
| `ThreadSafe` | No unproved ordinary raw-memory access or thread-affine operation; unsafe external preconditions still apply |
| `ThreadAffine` | Calls require a checked thread-bound operation/resource |

Analysis is conservative, including branches whose unreachability has not been
proved. These contracts concern the generated operations under valid input and
ownership contracts. They do not promise a millisecond deadline, immunity to OS
preemption/page faults or unlimited stack/resources.

Call `dlm_native_thread_init()` once on each host processing thread before its
hot calls. It prepares TLS, CPU dispatch and floating support outside those
calls. Native entry overhead is fixed; numeric internal calls and loops use
machine values without boxing. ELF shared libraries use eager symbol binding.

`AtomicBool`, `AtomicU32`, `AtomicU64` and `AtomicF32` provide load, store,
exchange, compare-exchange and lock-free queries. F32 atomics preserve the bit
representation through U32 atomics. Invalid load/store/CAS memory orders are
rejected; supported orders are `Relaxed/Acquire/Release/AcqRel/SeqCst`. The target
profile requires lock-free Boolean and 32/64-bit integer atomics.

`ThreadBound<T>` uses `thread_bind`, `thread_get` and `thread_get_mut`; access and
destruction check the owning thread. Windows also have checked thread ownership.

`Simd<F32,N>` / `Simd<F64,N>` support N = 2, 4, 8, 16, splat, add, multiply,
comparison and checked aligned/unaligned load/store. Comparison produces 1/0
numeric lanes. SSE2, AVX2 and AVX-512 paths dispatch by CPU support, with a scalar
fallback. AVX2/AVX-512 profiles fall back safely. Strict elementwise traversals
can auto-vectorize at `-O3`; the acceptance test checks packed binary64 machine
instructions without fast-math. Reductions preserve their defined evaluation
order. Integer SIMD and unrestricted symbolic cost proofs are outside this
floating-vector profile.

## GUI and formats

`window_open`, unsafe `window_child` and `window_attach`, `window_resize`,
`window_focus`, `window_dpi`, `window_poll`, `window_gpu` and `window_scene` use
the existing window/vector renderer. Scenes retain their existing command format
in a U64 array/buffer/slice. `window_handle` returns the borrowed native handle;
`window_read` copies GPU pixels into an owned buffer. Event kinds 1..8 retain
their meanings; kind 9 carries a DPI change.

`window_input(&window,kind,a,b,c)` forwards normalized host input. Attached X11
surfaces keep the host's exclusive input selection; Win32 attachment forwards
messages to the original host procedure. Detaching restores the procedure and
preserves the host window. CPU presentation uses the actual target visual/depth,
including ARGB32 surfaces. Native GPU operations restore the host's current
GLX/WGL context, GL state and floating environment on return and native error.
A borrowed parent/surface must remain alive, and window operations must use the
owning UI thread. `embedded_editor.dlm` and its independent C host exercise two
editors, CPU/GPU drawing, input, resize, attachment and independent destruction.
The existing Rust interpreter window backend remains Linux/X11-specific;
compiled native GUI uses X11/Win32.

`read_le/read_be/write_le/write_be` provide checked binary scalar access.
`std.native_binary` supplies the DLMB versioned TLV envelope, preservation of
unknown fields/flags/order, v1-to-v2 migration and strict UTF-8 scalar decoding.
`versioned_state.dlm` implements a typed settings codec with defaults for fields
absent in old versions, duplicate/size/UTF-8 validation and unknown-field
preservation. Schemas are explicit DLM code.

`std.native_xml` implements UTF-8 XML 1.0 without DTDs or external entities.
Its owned token document preserves unknown markup and source bytes. Nesting is
limited to 256 elements. It checks
nesting, Unicode names/characters, duplicate attributes, declarations, entities,
comments and CDATA; decoding normalizes XML line endings and attribute spaces.
`xml_replace_value` escapes a new text/attribute value and commits a reparsed
valid document. The parser rejects DTDs rather than resolving external data.
The standalone `native_hosts/interop.py` checks XML against Python ElementTree
and binary records against an independent Python struct codec.

The reference VM independently executes native machine/control/memory algorithms.
FFI/host-address operations require compiled native execution and report an
explicit VM limitation. It has separate allocation, call-depth and work budgets;
these are interpreter resource limits, not proofs of target hardware capacity.

## Verification

Run `tools/test_native_matrix.sh` on Linux or
`powershell -ExecutionPolicy Bypass -File tools/test_native_matrix.ps1` on
Windows with the GNU Rust target, a GCC-compatible compiler and Python available.
The scripts run regression, host ABI/callback/instance tests, a live GUI test
and independent XML/binary interoperability checks. On Windows the optional
`-OpenGlDll path/to/opengl32.dll` selects a compatible software OpenGL driver
for the GUI test; an installed graphics driver is used by default.
