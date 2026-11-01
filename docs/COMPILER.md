# v0.87 — Compiler: IR and C Backend (`dlm build`)

```text
dlm build program.dlm [-o out] [--emit-ir] [--c-only] [--trusted-only] [--lib DIR]
          [--check-overflow] [--require-no-overflow]
```

`dlm build` checks the program (with its imports), lowers it to the DLM IR, generates one self-contained C file
and compiles it with the system C compiler (`$CC`, default `cc` on POSIX or `gcc` on Windows; 64-bit GCC/Clang
with `unsigned __int128`; MinGW-w64 on Windows). The result is a native executable:

```text
$ dlm build examples/valid/word_count.dlm -o wc
$ printf 'one two\nthree' | ./wc Anton
hello, Anton
lines: 2
words: 3
chars: 13
```

`fib(24)` takes about 0.006 s compiled, against 0.55 s for `dlm run`.

## Pipeline

```text
.dlm --parse--> AST --check (passports, proofs, effects)--> checked program
     --lower_program--> IR (compile.rs) --emit_c--> C (backend_c.rs + backend_c_runtime.c) --cc -O2--> executable
```

Lowering does four things:

| step | what happens |
|---|---|
| erase statements | theorems, axioms and conjectures are proved or admitted by the checker and are not executed; using one as a value is `E0950` |
| erase measures and fuel | termination is established by the checker's well-founded measures, so compiled code recurses natively (on a 1 GiB virtual stack) |
| resolve names | locals become frame slots, theory `let`s become globals, calls become direct calls, function-value applications or builtins, with the resolution order of `dlm run` |
| convert closures | lambdas become functions with an explicit list of captured slots |

`dlm build --emit-ir` prints the IR:

```text
fn f0 Patterns.fact(n) captures=0 slots=2
  if nat_eq(%0, 0) then 1 else let %1 = pred(%0) in mul(%0, f0(%1))
```

## Semantics preserved

The C runtime follows `dlm run`:

- exact u128 Nat with the same overflow errors;
- lazy `if_then_else`, `and`, `or`, `unwrap_or` and `case`;
- left-to-right evaluation, enforced with temporaries because C leaves argument order unspecified;
- the same display format for `print_symbolic`;
- the same stdin behaviour: `read_line` strips `\r\n`, `read_all`, and `read_nat` on the trimmed input;
- program arguments via `args()`;
- a Nat result of `main` as the exit status.

Runtime errors print `dlm runtime error: ...` and exit with status 1. Stdin is read only when the program first
reads it.

Compiled: Nat, Int, Bool, Text, Option, Result, List, Sequence, Product, Sum, records, functions, lambdas, mutual
recursion, `let`, `match`, theory-level values and effects, imported modules and `extends`.

Not compiled (`E0950 CompileError`, with a hint to use `dlm run`): bridges and the research/meta layer (universes,
provability, GPU/remote/checkpoint values, `axiom_true`, quote/transport ...). The checker still checks them.

Differences from `dlm run`, by design:

- **fuel**: `dlm run` stops recursion at its fuel (10000 by default); compiled code does not, because termination
  is proved;
- **memory**: immutable heap objects are reclaimed at function/initializer boundaries; escaping result graphs survive. Self tail calls reuse their frame and region.

## Verification

`tests/compile_c.rs` checks the compiler differentially against the interpreter:

- every valid example that runs under `dlm run` and is compilable (including the standard library,
  mutual recursion, records/sums, higher-order functions, text and I/O) is compiled, run, and its output and exit
  status compared;
- `word_count` and a line-reading program are run with several stdin texts (CRLF, empty, Unicode) and arguments;
- 12 generated programs, each with 22 functions over Nat, lists, options, text, lambdas, `let` and `match`, must
  print exactly what the interpreter prints;
- overflow errors, deep recursion beyond the interpreter's fuel, statement erasure, unsupported constructs and
  closure captures are tested.


## v0.87 typed IR, verifier, contracts and stack guard

* The IR is typed (`DLM-IR v2`): every node carries the checked type of its source expression, its exact span
  and the source `ExprId`; functions carry their signature, effects and termination status. `verify_ir` runs
  after lowering; a failure is an internal compiler error. See `TYPED_CORE.md`.
* The scalar backend and the machine-range analysis read the IR types; nothing is reconstructed.
* Self tail calls of scalar functions are compiled to loops too, so every self tail call runs in constant stack.
* Stack exhaustion is defined: every generated function checks the remaining stack (frame address against the
  end of the 1 GiB program stack, 8 MiB reserve) and fails with
  `dlm runtime error: stack exhausted: the recursion is deeper than the native stack allows`, exit status 1.
  If the program thread cannot be created and the program runs on the main thread, the check is disabled.
* `dlm contracts file.dlm` prints per-function contracts with independent statuses (`SEMANTICS.md` §9).
* The interpreter and compiled programs are compared on observable behaviour including runtime errors
  (`SEMANTICS.md` §8); `dlm run` now prints the output produced before a runtime error.

## v0.86 runtime: machine values and memory

Pure numeric/Boolean functions whose types can be reconstructed unambiguously are emitted with native `nat_t`/`int_t`/`int` parameters, locals and return values. Calls between these functions do not allocate or box values, including 128-bit values outside the tagged range. Unresolved types, aggregates, captured closures, indirect calls and I/O keep the uniform ABI. Both paths use the same checked arithmetic helpers and preserve left-to-right evaluation and lazy branches. Public IR structures and the closure ABI are unchanged.

On 64-bit targets, Nat values `0..2^62-1` and Int values `-2^62..2^62-1` are tagged machine words and require no heap allocation. Larger values use heap objects without narrowing the `u128`/`i128` domains. Every operation retains its overflow guard; arithmetic never wraps silently. The encoding is an internal GCC/Clang target convention, not an FFI pointer ABI.

Each generated call owns a region. At return, an iterative trace preserves the returned graph (text buffers, list elements, options, results, pairs, sums, record fields and captured closure environments), promotes it to the caller, and frees all other allocations in the region. Shared subobjects remain shared. Globals, argument values and the stdin buffer survive until process exit; normal exit and runtime-error exit release remaining allocations with `atexit`.

A `fold` discards unreachable old accumulators on each iteration. Direct self calls in tail position become a loop: new argument graphs survive, while old temporaries and argument objects are released before the next iteration. This includes calls under `let`, `match` and `if_then_else`.

This is region reclamation, not a tracing GC at every allocation: unreachable locals can live until their call returns; non-tail and mutual recursion still use the native stack. Since v0.86.1 a graph returned through many calls is not traced again at every level (see below). Retained globals and growing results necessarily consume memory.

## Mathematical correctness and machine arithmetic

The symbolic kernel proves mathematical identities. For example, `n + n = 2*n` does not establish that either intermediate expression fits 128 bits. Statements remain erased, and no theorem or axiom disables a runtime overflow guard.

`--check-overflow` additionally runs a conservative interval analysis over every compiled function, including imports, unused functions and lambdas. It reports each unresolved executable arithmetic obligation. `--require-no-overflow` refuses to emit C or an executable if any obligation is open:

```text
dlm build safe.dlm --trusted-only --require-no-overflow
```

For example, this function is certified for the full Nat input domain:

```dlm
module safe
theory Safe {
    fn next(n: Nat) -> Nat = if_then_else(n > 1000, n, n + 1)
    theorem add_zero(n: Nat): nat_eq(n + 0, n) by normalize
    fn main() -> Nat = next(3)
}
```

The true branch returns `n` unchanged; the false branch restricts `n` to `0..1000`, so `n+1` fits. An unguarded `fn next(n: Nat) -> Nat = n + 1` leaves an obligation open, even if one caller supplies `3`.

The pass checks addition, multiplication, signed subtraction/negation, Nat-to-Int conversion, powers (including the runtime exponent limit), and signed division/remainder at `Int::MIN / -1`. It checks intermediate expressions, rather than simplifying away their possible failures. Nat division/remainder keep total zero-divisor semantics. Unknown bounds remain open. Function results use declared full domains; lambda captures and aggregate fields are currently conservative unknowns. This can reject programs whose safety needs a relational or interprocedural proof.

The report combines successful existing proof checking with a separate machine-range analysis of executable expressions. It does not reinterpret erased mathematical propositions as machine theorems, nor certify valid input, I/O success, memory/stack capacity or absence of other runtime failures. Guards remain enabled even when all obligations are proved. Existing builds without these flags retain their previous acceptance rules.

Rust API: `check_machine_arithmetic(&program, policy)` returns an `ArithmeticSafetyReport`; `verified()` is true only when every generated obligation is discharged.

## Native Windows x64

Install Rust for Windows and 64-bit MinGW-w64 GCC (for example, the MSYS2 UCRT64 toolchain). Put its `ucrt64/bin` directory on PATH, then run in PowerShell:

```powershell
./tools/build_windows.ps1
./target/release/dlm.exe build examples/valid/word_count.dlm -o wc.exe
```

The script runs the locked workspace tests, creates a release `dlm.exe`, compiles and executes a native smoke program from a directory with spaces. `-CCompiler` selects another GCC-compatible compiler path; `-SkipTests` skips only the workspace suite, not the native smoke test.

Windows generated programs use `CreateThread` with a 1 GiB reserved stack, native UTF-16-to-UTF-8 argument conversion and binary stdin/stdout to preserve byte-exact UTF-8 and newline semantics. They do not need pthreads or an MSYS shell. The backend queries the compiler target using `-dumpmachine`, chooses `shell32` for Windows and `-pthread` for POSIX, and returns the actual `.exe` path when the output has no extension. Native MSVC `cl.exe` is not supported because it lacks the runtime's `__int128` arithmetic. Cross-compiling with a MinGW target uses the same Windows runtime branch.

`.github/workflows/native.yml` runs the workspace tests on Linux and the PowerShell build/smoke workflow on Windows. Windows execution must be confirmed on that runner or a Windows machine; a Linux test run alone is not evidence that the Windows suite passed.


## v0.86.1 survivor groups: no re-tracing of returned graphs

In v0.86.0 every return traced the whole returned graph owned by the call. A graph that is returned through many
calls was therefore traced at every level: a recursion that wraps the result of the level below (for example a
closure capturing the closure from below) cost O(depth²). 40 000 levels took 91 s instead of 1.5 s in v0.85.

Since v0.86.1 the survivors of each collection form a **group**: an immutable set of blocks, all reachable from
the group's roots (the returned value, or the arguments of a self tail call / the accumulator of `fold`). A block
belongs to a group by its allocation serial number: every group covers one serial interval of its call. At the
next collection:

- if every root of a group is reachable again, the whole group survives **without being traced**; only its list
  of references to older blocks (`ext`) is processed, so those survive too;
- if only some roots or inner blocks are reachable, the group is traced block by block and split precisely, so a
  list element taken from an old list does not keep the old list alive;
- an unreachable group is freed.

The survivors (young blocks, whole groups and the kept parts of split groups) become one group of the caller.
Collection work is proportional to young blocks, split or freed groups and `ext` lists, not to the size of the
groups kept whole. Marks are collection epochs, so kept groups need no reset.

| program (compiled, `-O2`) | v0.85 | v0.86.0 | v0.86.1 |
|---|---|---|---|
| closure chain, depth 10 000 / 20 000 / 40 000 | 2.2 / 2.5 / 1.5 s, ~1 GB | 2.4 / 9.2 / 90.7 s, 16–22 MB | 0.46 / 0.43 / 0.45 s, 19–25 MB |
| recursive list building, 20 000 (`cons` copies, O(n²) in all versions) | 3.4 s, 3 GB | 5.7 s, 12 MB | 4.9 s, 12 MB |

Remaining cost: a group whose `ext` keeps growing (each step captures a *different* heap object from outside the
loop, e.g. a closure chain over the texts of a list) still processes that list on every step, as v0.86.0 did.

Checks: ASan/UBSan/LeakSanitizer on every compilable example and on 240 generated programs that return, split,
share and capture parts of results through recursion, tail calls and `fold` (output equal to `dlm run`); three
deliberate collector bugs (ignoring `ext`, not splitting partly reachable groups, not recording outward
references) are all detected by that fuzzing; a C harness asserts linear tracing work on a 20 000-level chain and
precise splitting. The Windows build of the same programs gives the same output under Wine.

## v0.86 local validation (2026-10-27)

- Linux `cargo test --workspace --locked`: 540 passed, 0 failed.
- All 11 C backend tests also pass with GCC AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`, `UBSAN_OPTIONS=halt_on_error=1`). LeakSanitizer cannot inspect `/proc` in this environment, so that run uses `ASAN_OPTIONS=detect_leaks=0`; the runtime harness separately asserts allocation counts and zero remaining blocks after cleanup.
- Boundary coverage includes 696 closed arithmetic cases checked against the interpreter, both sides of the tagged/heap threshold, u128/i128 extrema and intermediate overflow. The runtime harness verifies shared escaping graphs, closure captures, old fold accumulator reclamation and no small-number allocations. Long-computation tests include 500,000 self tail calls and 100,000 native scalar calls with a heap-sized Int input.
- A five-run comparison against the supplied v0.85 source, both built with the same `cc -O2`, gives median `fib(34)` times of approximately 0.129 s before and 0.049 s after. A 200,000-step tail computation allocating discarded word lists takes approximately 0.216 s before and 0.056 s after. These are local workload measurements, not general performance guarantees.
- Native Windows execution is pending a Windows host/CI run. The Windows implementation and reproducible build/test/smoke workflow are included.
