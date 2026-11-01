# DLM — Language Guide (English)

[Русский](LANGUAGE_GUIDE_RU.md) · [Home](../README.md) · [Documentation index](README.md)

> **Source version: 0.131.0.** This guide describes features supported by the current repository and its existing documentation. DLM is an experimental general-purpose programming language with mathematical/proof-aware facilities and native compilation for its supported executable subset. It is **not** a finished replacement for Rust, C++, or Lean. A feature in the roadmap is not necessarily implemented.

## 1. Goals and architecture

DLM brings together executable programs, mathematical statements, and explicit information about how results are justified. An object's **passport** describes provenance, trust, capabilities, theory context, and derivation history. Metadata is not a proof: assumptions, axioms, and external inputs remain distinguishable.

The layers are separated: source → parsing and checking → typed facts and executable IR → C → native executable. Proof statements are not executed inside hot machine code. This **does not** certify the generated machine code as a whole. See [semantics](SEMANTICS.md), [compiler](COMPILER.md), [architectural laws](DLM_ARCHITECTURAL_LAWS.md), and [implementation status](LAW_STATUS.md).

## 2. Getting started

Building the compiler requires Rust/Cargo. `dlm build` requires a supported 64-bit C compiler (GCC/Clang on Linux; MinGW-w64 on Windows, not MSVC). Run from the repository root:

```sh
cargo build --release -p dlm_cli
cargo run --release -p dlm_cli -- --version
cargo run --release -p dlm_cli -- check examples/valid/ordinary_functions.dlm
cargo run --release -p dlm_cli -- run examples/valid/word_count.dlm
cargo run --release -p dlm_cli -- build examples/valid/word_count.dlm -o word_count
cargo test --workspace
```

When `dlm` is on `PATH`, use `dlm check ...`, `dlm run ...`, or `dlm build ...` directly. Checking a file does not imply the research constructs in it can be compiled by the native backend. A [Windows build script](../tools/build_windows.ps1) is included.

## 3. Hello, DLM

Create `hello.dlm`:

```dlm
module demo.hello

theory Hello {
    fn main() -> Nat =
        let _ = print_line("Hello from DLM!") in
        0
}
```

Run with `dlm run hello.dlm`; build with `dlm build hello.dlm -o hello` (or `hello.exe` on Windows). `fn main()` runs after the root theory's items. Its `Nat` result is used as an exit status within the documented range. See the complete [command-line example](../examples/valid/word_count.dlm).

## 4. Modules, functions, and flow

Files begin with `module`; `import` references another module/theory. Declarations live in `theory` blocks, with optional `extends`. Functions use `fn name(argument: Type) -> Type = expression`. Local binding uses `let name = value in expression`.

```dlm
module demo.arithmetic

theory Arithmetic {
    fn square(n: Nat) -> Nat = mul(n, n)
    fn capped(n: Nat) -> Nat = if_then_else(n > 10, 10, n)
    let value = square(12)
    print_decimal(capped(value))
}
```

`if_then_else` evaluates only the selected branch; arguments are evaluated left to right. `match`, patterns, `enum`, `record`, function values, and closures are supported. Recursion can carry a `decreasing` measure; a distinct tail-recursive `loop` form supports potentially non-terminating services. See [functions](../examples/valid/ordinary_functions.dlm), [records and sums](../examples/valid/ordinary_records_sums.dlm), and [loops and enums](APPS.md).

## 5. Types, collections, and generics

Executable scalar types include `Nat`, `Int`, `Bool`, and `Text`. The language also supports `Option<T>`, `Result<T,E>`, `List<T>`, `Sequence<T>`, `Product<A,B>`, `Sum<A,B>`, records, and enums. While logical arithmetic is mathematical, executable `Nat`/`Int` use bounded `u128`/`i128`; an overflow is an **error**, not unlimited precision. `Text` is UTF-8; text character positions should not be confused with byte offsets.

```dlm
let xs = list(1, 2, 3, 4)
let doubled = map(xs, fun(x: Nat, x + x))
let sum = fold(doubled, 0, fun(acc: Nat, x: Nat, acc + x))
print_decimal(sum)
```

Parameterized functions (`fn identity<T>(x: T) -> T = x`), type argument inference, `Eq`/`Ord` bounds, type classes, specialization, `Fn`, and bounded `FnEffect` are available in the documented subset. Ordinary lists are **not** automatically dense SIMD arrays. See [generics](GENERICS.md), [collections](../examples/valid/ordinary_collections.dlm), and [numeric semantics](SEMANTICS.md).

## 6. Effects, I/O, and ownership

Effects are made explicit in contracts: `Stdout`, `Stdin`, `Args`, `File`, `Net`, `Process`, `Window`, and others. Pure `Fn` and effect-bounded `FnEffect<Bounds,...>` are different callable types. Effect bounds are **not** OS privileges or a sandbox.

File APIs include `file_open`, `file_read_line`, `file_write`, `file_close`, directory operations, and atomic writes. File handles are linear resources: they must be closed or ownership transferred. Mutable `ByteBuf`/`TextBuf` implement move/borrow rules in their supported scope; this is **not** a complete general-purpose ownership framework. See [effectful callbacks](EFFECT_FUNCTIONS.md), [files](FILES.md), [ownership](OWNERSHIP.md), and a [file transformation example](../examples/valid/effect_file_transform.dlm).

## 7. Theorems, evidence, and trust

```dlm
theorem sum_example: nat_eq(2 + 3, 5)
theorem commutative_small(n: Nat, m: Nat) below 10: nat_eq(n + m, m + n)
axiom assumption(n: Nat): nat_eq(n, n)
conjecture claim(n: Nat) tested 100: nat_eq(n + 0, n)
```

A `theorem ... below N` establishes only the **bounded** claim. A `conjecture ... tested N` is still an open conjecture after testing; an `axiom` remains an explicit assumption. Proof certificates and independent replay exist for a **limited fragment of Nat equalities** (C-min). This is not a universal proof kernel nor a machine-code correctness proof. See [proof syntax](PROOF_SURFACE.md), [equational replay](EQUATIONAL_REPLAY.md), and [trust boundaries](PROOF_CERTIFICATE_AUDIT.md).

## 8. Graphics, GPU, and scientific libraries

The repository contains `std.ui`, `std.vector`, widgets, software vector rasterization, SVG, PNG, and documented X11/Win32 window support. v0.131.0 adds **OpenGL GPU presentation of graphics scenes**, with a software fallback. This is **not** a universal GPU compute backend or automatic CPU/GPU distribution for arbitrary programs. See [GUI documentation](GUI.md) and the [counter demo](../examples/valid/gui_counter.dlm).

Bounded exact-computation libraries cover integers/rationals under explicit limits, matrix methods, finite algebra, homology for finite structures, complex numbers, and interval methods in separate supported profiles. They do **not** imply that arbitrary mathematical theories are fully implemented: see [mathematical progress](MATHEMATICS_PROGRESS.md).

## 9. Diagnostics and assurance limits

```sh
dlm contracts examples/valid/integers.dlm
dlm build --emit-ir examples/valid/ordinary_higher_order.dlm -o hof
dlm build --check-overflow examples/valid/integers.dlm -o integers
```

`--require-no-overflow` can reject a build with unresolved machine-range obligations. A proven mathematical identity alone does not establish absence of overflow, correctness of external dependencies, or correctness of the downstream C compiler. Compare interpreter and native behavior and run the test suite. See [compiler](COMPILER.md), [diagnostics](DIAGNOSTICS.md), [implementation status](LAW_STATUS.md), and [roadmap](../ROADMAP.md).

## 10. Reference map

- **Syntax:** [GRAMMAR.md](GRAMMAR.md) (partly historical MVP grammar) and [ORDINARY_SURFACE.md](ORDINARY_SURFACE.md).
- **Applications:** [APPS.md](APPS.md), [FILES.md](FILES.md), [GUI.md](GUI.md), [examples/valid](../examples/valid).
- **Formal reasoning:** [PROOF_SURFACE.md](PROOF_SURFACE.md), [EQUATIONAL_REPLAY.md](EQUATIONAL_REPLAY.md), [LAW_STATUS.md](LAW_STATUS.md).
- **Architecture:** [SEMANTICS.md](SEMANTICS.md), [COMPILER.md](COMPILER.md), [DLM_ARCHITECTURAL_LAWS.md](DLM_ARCHITECTURAL_LAWS.md).
- **Future work:** [ROADMAP.md](../ROADMAP.md), [MATHEMATICS_PROGRESS.md](MATHEMATICS_PROGRESS.md).

> Some older documents describe historical designs. In case of disagreement, prefer current code, tests, and `LAW_STATUS.md`. This guide is an entry point, not a replacement for the normative specification.
