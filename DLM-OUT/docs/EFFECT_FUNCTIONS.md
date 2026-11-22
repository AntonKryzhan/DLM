# Explicit callback effects — v0.130.0

`Fn<P..., R>` continues to mean a pure, total callable. `FnEffect<Bounds, P..., R>`
is a callable with an explicit upper bound on effects and partiality. Both are ordinary
copyable values: they may be parameters, results, fields and collection elements.
A callback may capture ordinary immutable values. Owned resources remain subject to
[OWNERSHIP.md](OWNERSHIP.md).

```dlm
let log = effect_fn(FnEffect(Stdout, Nat, Nat),
    fun(n: Nat, let printed = print_decimal(n) in n + 1))
fn apply<F: UnaryCallable>(value: ArgOf<F>, action: F) -> ResultOf<F> = action(value)
print_decimal(apply(3, log))
```

The first argument of `effect_fn` is a checked type expression and is erased before
execution. The second argument is a declared function, a lambda, or an existing
callable. Parameter and result types must match exactly. Conversion may widen an
existing effect bound; it cannot erase an effect or `Partial`. The declared upper
bound, rather than observed effects of one call, travels with the value.

`FnEffect<File, Text>` has no parameters and returns `Text`; wrap a named zero-argument
function with `effect_fn(FnEffect(File, Text), load)`. Existing `fun` syntax still
requires at least one typed parameter.

## Bounds

| Source bound | Contract boundary |
|---|---|
| `Pure` | No effects and no partiality; used alone |
| `Partial` | May fail through a partial operation or run without a termination proof |
| `Stdout`, `Stdin`, `Args` | `io@stdout`, `io@stdin`, `io@args` |
| `File`, `Process`, `Net`, `Window` | `io@file`, `io@process`, `io@net`, `io@window` |
| `ByteBuffer`, `TextBuffer` | `runtime@byte_buffer`, `runtime@text_buffer` |
| `Require`, `EnumPayload` | `runtime@require`, `runtime@enum_payload` |
| `GPU`, `Remote` | Existing research execution boundaries; native support is unchanged |

Combine bounds with `+`, for example `File + Stdout` or `ByteBuffer + Partial`.
Order is normalized; unknown names, duplicate bounds and `Pure + ...` are rejected.
`Partial` alone permits partial pure code. Bounds do not grant OS permissions or
provide a sandbox. Buffer allocation/mutation may require `Partial` according to
the existing builtin contract.

Creating a closure does not execute its body. Calling a callback, including through
aliases, generic instances, `case` and the effectful traversal operations, propagates
its upper bounds to the caller. Source statements and ordinary mathematical `map`
and `fold` continue to require pure, total callbacks. An explicitly `Pure` `FnEffect`
can also be used by those traversals.

The IR verifier independently checks callable signatures, conversion bounds, closure
and function effects, and partiality propagated through direct and indirect calls.
It rejects unresolved projections and unguarded recursive records through callable
fields. This is structural verification, not a proof that the compiler preserves
all source semantics. Machine overflow, runtime resource limits and I/O progress
remain separate from the structural termination model.

## Generic constraints and builtin projections

| Constraint | Accepted callable |
|---|---|
| `Callable` | `Fn` or `FnEffect`, any arity |
| `UnaryCallable` | Exactly one parameter |
| `NullaryCallable` | No parameters |
| `PureCallable` | No effects and no partiality |

These names are reserved builtin constraints. Generic instances are still produced
by monomorphization. The full callable type carries its own bounds; a generic wrapper
therefore preserves them without assuming that every callback is pure.

| Projection | Domain and result |
|---|---|
| `ArgOf<F>` | Unary `Fn` / `FnEffect`: its parameter type |
| `ResultOf<F>` | Any callable: its result type |
| `ItemOf<C>` | `List`, `Sequence`, `Option`, `NatVec`, `FloatVec`: element type |
| `OkOf<R>`, `ErrorOf<R>` | `Result<T,E>`: `T` and `E` |

Projections compose and reduce after type substitution, including record fields,
return types and erased type operands such as `none(ResultOf(F))`. Inference first
binds ordinary parameters, then checks projected arguments. Explicit type arguments
are needed when a projection alone does not determine its source type. Concrete
invalid domains and unresolved projections cannot enter executable IR.
These are builtin associated projections, not user-declared associated types or
higher-kinded polymorphism.

## Ordered traversal

The builtins support both `List<T>` and `Sequence<T>`, preserving collection kind.
They use one ordered pass, invoke the callback at most once per visited item, and
have no recursive traversal stack. A callback's own work is additional. Native
`each`, predicates and fallible `each` reclaim discarded values after every call;
kept result graphs survive their iteration region.

| Builtin | Behavior |
|---|---|
| `map_effect(xs, f)` | Collect callback results |
| `fold_effect(xs, initial, f)` | Ordered `(state, item) -> state` fold |
| `each_effect(xs, f)` | Discard ordinary results; return visited count |
| `filter_effect(xs, predicate)` | Keep matching items |
| `find_effect(xs, predicate)` | First matching item or `none` |
| `any_effect`, `all_effect` | Stop on the first decisive predicate result |
| `try_map_effect(xs, f)` | `Result<collection<T>,E>`, stop on first `err` |
| `try_each_effect(xs, f)` | `Result<Nat,E>`, stop on first `err` |

Empty collections never invoke the callback; `all_effect` returns `true` and
`any_effect` returns `false`. Effect bounds remain conservative for empty inputs.
Owned resources cannot be elements or callback parameter/result components, so
`each_effect` cannot silently discard an unclosed `File`.

Import `std.effects` and extend `StdEffects` for generic list wrappers:
`effect_call`, `effect_run`, `effect_each/map/fold/filter/find/any/all`,
`effect_on_some`, `effect_on_ok`, `effect_try_map`, `effect_try_each`, and
`effect_collect_results`. The last collects every `Result` without stopping.

## File and HTTP callbacks

`std.files` / `StdFiles` adds:

* `each_line(path, action) -> Result<Nat,Text>`: stream lines and count callbacks.
* `try_each_line(path, action) -> Result<Nat,Text>`: stop on a callback `err`.
* `transform_text(src, dst, transform) -> Result<Nat,Text>`: read/close input,
  invoke `Text -> Text`, then atomically replace the destination.
* `try_transform_text(src, dst, transform)`: a `Text -> Result<Text,Text>`
  callback can fail without replacing the destination.

Line callbacks receive the existing `file_read_line` representation. Readers close
on success and `Result` failure; a callback error takes precedence over a close
error. Runtime aborts use existing process-exit cleanup. Unbounded callback loops
have no cancellation mechanism. A missing input invokes no callbacks and reports
the file error.

`std.net` generalizes `http_serve_one` and `http_serve` to a unary callable type.
The existing `Text -> Text` call shape and pure handlers remain usable. A
`FnEffect<File+Stdout,Text,Text>` handler can read a file and log a request.
The server remains sequential HTTP/1.0; tasks, TLS and cancellation are open.

Evidence: `effect_functions.rs`, `effect_library.rs`, the builtin mutation suite in
`ir_verify.rs`, and the shared lexical-capture unit test. These cover interpreter/C
agreement, rejected bounds, corrupted IR, short-circuiting, 50,000 items, file
failure paths and an actual loopback HTTP request.
