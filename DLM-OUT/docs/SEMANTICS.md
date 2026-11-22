# v0.87 — Normative Semantics of Executable DLM (Stage A)

This document is the reference that the checker, the interpreter (`dlm run`) and the compiler (`dlm build`) are
measured against (law 30). It describes the executable surface as it exists in v0.87; constructs of the research
layer (bridges, meta levels, GPU/remote values) are checked but not specified here and are not compiled.

Every rule names the tests that check it. A rule without such a reference is a gap and is listed in
`LAW_STATUS.md`.

## 1. Programs

| Item | Meaning |
|---|---|
| `module m` | One file. `import a.b` makes the declarations of module `a.b` available; imported theory-level `let`s and expressions are **not executed**. |
| `theory T [extends B] { ... }` | Items run in source order. `extends` inherits functions, records and statements. |
| `let x = e` | Evaluates `e` once, at its position, and binds the theory-level value `x`. |
| expression item | Evaluated at its position for its effects; the value is discarded. |
| `fn f(..) -> T = e` | Declares a function. Its body runs only when called. |
| `record R(..)` | Declares a nominal record type. |
| `theorem` / `axiom` / `conjecture` | Checked statements. They are **erased at run time**: never evaluated, never observable. |
| `fn main() -> T` | Called after all theory items of the root module. A Nat result is the exit status (saturated at 255); any other result gives status 0. |

## 2. Values and types

| Type | Values | Representation (interpreter / compiled) |
|---|---|---|
| `Nat` | mathematical naturals, **executed in u128** | exact `u128`; compiled: tagged word below 2^62, heap object above |
| `Int` | mathematical integers, **executed in i128** | exact `i128`; compiled: tagged word in −2^62..2^62−1, heap above |
| `Bool` | `true`, `false` | static objects |
| `Text` | UTF-8 text; `text_length` and `substring` count Unicode scalar values | byte buffer |
| `Option<T>`, `Result<T, E>` | `none`/`some(v)`, `ok(v)`/`err(e)` | tagged object |
| `List<T>`, `Sequence<T>` | finite sequences | array of values; `cons` and `tail` **copy** (O(n)) |
| `Product<A, B>`, `Sum<A, B>`, records | pairs, `inl`/`inr`, named fields | objects |
| `Fn<P.., R>` | **pure, total** functions: lambdas and named functions used as values | closure: code + captured values |

Values are immutable. Equality of `Nat`, `Int` and `Text` is by value (`nat_eq`, `int_eq`, `text_eq`).

## 3. Evaluation (strict, left to right)

| Construct | Rule |
|---|---|
| call `f(a1, .., an)` (function, builtin, record constructor) | arguments are evaluated **left to right**, each exactly once, then the call is performed |
| `a + b`, `a - b`, `a > b`, `a ^ b`, `mul`, `div`, `mod`, ... | operands left to right, then the checked operation (§5) |
| `if_then_else(c, a, b)` | `c`, then **only** the selected branch |
| `and(a, b)` / `or(a, b)` | `a`; `b` only if `a` is `true` / `false` |
| `unwrap_or(o, d)` | `o`; `d` only if `o` is `none` or `err` |
| `case(s, f, g)` | `s`, then only the selected function value, applied to the payload |
| `let x = v in b` | `v` once, then `b` with `x` bound |
| `match s { p1 => e1, .. }` | desugared into `let`/`if_then_else` (see `PATTERNS_AND_LET.md`): `s` is evaluated once, arms are tested in order, the first matching arm is evaluated; the helper operations that extract `some(x)` / `cons(h, t)` are only reached after the test succeeded and cannot fail |
| `fun(x: T, e)` | creates a closure that captures the current values of its free local names |
| `map(xs, f)` / `fold(xs, z, f)` | applies `f` to the elements in list order (`fold` from the left) |
| theory items, `main` | in source order, then `main` |

Checked by: `typed_core.rs` (`runtime_errors_and_lazy_branches_agree_between_interpreter_and_compiler`: unselected
branches, short-circuit, `unwrap_or` default, left-to-right failure order), `compile_c.rs`
(`scalar_functions_use_native_128_bit_values_and_preserve_effect_order`), `patterns_let.rs`.

## 4. Effects

| Effect | Builtins | Order |
|---|---|---|
| `io@stdout` | `print_line`, `print_text`, `print_decimal`, `print_symbolic` | one line per call, in evaluation order |
| `io@stdin` | `read_line` (`Option<Text>`, `none` at end of input), `read_all`, `read_nat` | consumes input in evaluation order; input is read lazily |
| `io@args` | `args()` | program arguments, without the program name |

The effects of a function are the effects of its body including the functions it calls (checked contract).
Function values are pure and total by construction: a lambda whose body has an effect is rejected (`E0929`), so
higher-order functions need no effect parameters yet. A proof (statement) may not depend on effects.

Checked by: `text_io.rs`, `typed_core.rs` (`contracts_report_independent_properties`: effects through a call chain),
`ordinary_higher_order.rs`.

## 5. Arithmetic: mathematical numbers, machine execution

The checker, the symbolic kernel and theorems treat `Nat` and `Int` as **mathematical** numbers. Execution is in
`u128` / `i128` with **checked** operations: a result outside the range is a runtime error, never a wrapped value.
For an expression `e` and input `x`, `Defined_128(e, x)` holds when every operation of `e` — including every
intermediate one — stays in range. A theorem `Q` about `e` speaks about executions only together with
`Defined_128`:

    P(x) and Defined_128(e, x)  ==>  Q(x, eval_128(e, x))

| Operation | Defined when | Error message (interpreter = compiler) |
|---|---|---|
| Nat `+` | result ≤ 2^128−1 | `addition result overflows MVP runtime exact Nat range` |
| Nat `mul` | result ≤ 2^128−1 | `multiplication result overflows MVP runtime exact Nat range` |
| `a ^ b` | `b` ≤ 2^32−1 and result ≤ 2^128−1 | `power exponent is too large ...` / `power result overflows ...` |
| Int `+`, `-`, `mul`, unary `-` | result in −2^127..2^127−1 | `Int addition/subtraction/multiplication/negation overflows the exact Int range` |
| `int(n)` | `n` ≤ 2^127−1 | `int overflows the exact Int range` |
| Int `div`/`mod` | not (`MIN`, `−1`) | `div/mod overflows the exact Int range` |
| `monus`, `pred`, Nat `div`/`mod`, `abs` | always | — |

Division is total: `div(a, 0) = 0`, `mod(a, 0) = a`; Int division is Euclidean (`0 ≤ mod(a, b) < |b|`).
`text_to_nat` / `text_to_int` return `none` instead of failing. Optimizations may not change which operation
fails or whether one fails (law 19): `monus(M + 1, 1)` must fail when `M + 1` does.

`dlm build --check-overflow` reports, per function, which operations are proved to stay in range for all inputs
of the declared types; `dlm contracts` shows the same per function.

Checked by: `integers.rs`, `machine_arithmetic.rs` (696 closed cases against the interpreter), `typed_core.rs`
(generated failing programs), `compile_c.rs` (`tagged_numbers_preserve_word_and_128_bit_boundaries`).

## 6. Outcomes and failure categories

| Category | Example | Representation |
|---|---|---|
| expected absence or failure | empty list, missing line, bad number text | `Option` / `Result` value |
| arithmetic error | overflow, `MIN / -1` | runtime error (§5): the run stops, exit status 1 |
| checked condition | — (no `require`/refinements in the executable surface yet) | — |
| non-termination | not possible for checked functions: every recursion has a proved measure; there is no loop construct yet | — |
| resource exhaustion | see §7 | runtime error, exit status 1 |
| implementation defect | IR verification failure, a C runtime type check such as `condition is not a Bool value` | reported as an internal error; never a property of the program |

## 7. Resource policies (where interpreter and compiler differ by design)

| Resource | Interpreter (`dlm run`) | Compiled program |
|---|---|---|
| recursion depth | per-function fuel (`decreasing n fuel N`, default 10000): `recursion fuel exhausted for fn 'f'`; a stack reserve guard on a 1 GiB thread | fuel is erased (termination is proved). Self tail calls are loops (constant stack). Other recursion uses a 1 GiB stack; every function checks the remaining stack on entry: `stack exhausted: the recursion is deeper than the native stack allows`. A C compiler may also turn a recursion into a loop, in which case the run completes |
| memory | Rust allocation | call regions free temporaries; `out of memory` on allocation failure |

Functional equivalence (law 19, law 30) is required for runs that stay within both resource policies; a run that
exceeds a policy ends with the corresponding runtime error and exit status 1 (never with a crash).

Checked by: `typed_core.rs` (`compiled_stack_exhaustion_is_a_defined_runtime_failure`), `compile_c.rs`
(`runtime_errors_and_deep_recursion`, `tail_calls_reuse_regions_for_long_running_programs`).

## 8. Observable behaviour

`Obs(P, input)` = the printed lines in order, the runtime error that ended the run (if any) and the exit status.
Output printed before a runtime error is part of `Obs`: `dlm run` prints it under
`program output (before the runtime error):`, a compiled program has already written it to stdout. Memory
reclamation, stack use and timing are not part of `Obs`.

The interpreter and the compiler must agree on `Obs` for every checked program (within §7).
Checked by: `typed_core.rs` (fixed cases and 24 generated programs, about half of which fail at run time),
`compile_c.rs` (all compilable examples, generated programs), `cli_programs.rs`.

## 9. Properties of a function are independent (law 31)

| Property | Established by | Reported as |
|---|---|---|
| types | checker | signature in the IR and `dlm contracts` |
| effects | checker (contract, with callees) | `effects:` |
| termination | checker (non-recursive, measure, mutual group) | `termination:` |
| machine definedness | interval analysis over the typed IR | `machine arithmetic: closed / open` |
| stack | backend code shape | `stack: no recursion / loop / native recursion` |
| mathematical specification | theorems proved about the function | `specification (mathematical):` |

None implies another. `dlm contracts file.dlm` prints all of them per function.

## 10. Lowering to the typed IR

| Construct | IR operation (`IrOp`) |
|---|---|
| literal | `Nat`, `Int` (negated literal), `Bool`, `Text` |
| local / theory value / function name | `Local(slot)`, `Global(index)`, `FunctionValue(id)` |
| call of a function / function value / builtin | `Call`, `Apply`, `Builtin` |
| `+`, `>`, `^` | `Add`, `Gt`, `Pow` (`-` and unary `-` are `Builtin __sub` / `__neg`) |
| `if_then_else`, `and`, `or`, `unwrap_or`, `case` | `If`, `And`, `Or`, `UnwrapOr`, `Case` (lazy) |
| `let` | `Let { slot }` |
| lambda | `Closure` of a lifted function with explicit captured slots |
| `none`, `empty_list`, `inl`/`inr`, records, `field` | `Empty`, `Inject`, `Record`, `Field` |

Every IR node carries the checked type of its source expression, the exact source span and the `ExprId` it came
from; `verify_ir` checks the result (see `TYPED_CORE.md`).
