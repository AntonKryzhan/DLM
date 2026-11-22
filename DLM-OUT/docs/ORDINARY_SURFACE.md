# v0.73 — Ordinary Mathematics Surface Wiring

Before v0.73 the Stage 2 layers (v0.55–v0.67: functions, contracts, Option/Result, List/Sequence, products, map/fold)
existed only as Rust APIs in `dlm_core`. They could not be written in a `.dlm` file, and the parser silently dropped
`fn` lines. v0.73 connects `.dlm` syntax, the checker and the runtime to those layers.

```text
.dlm surface  ->  checker::ordinary_surface  ->  Stage 2 core reports  ->  passports
              ->  runtime::ordinary_surface  ->  exact values
```

The checker does not re-implement the layers. Every constructor goes through the core report:

| surface                     | core layer                                                     |
|-----------------------------|----------------------------------------------------------------|
| `some`, `none`, `ok`, `err` | `partiality::{option_*, result_*}`                             |
| `list`, `sequence`, `empty_*` | `sequence::{list_*, sequence_*}`                             |
| `pair`                      | `structural::{product_type, product_term}`                     |
| `fn` declaration            | `function::function_type` + `function_contract::function_contract` |
| one-argument call           | `function::application_term` (cross-check)                     |
| `map`, `fold`               | `traversal::{map,fold}_collection_passport`                    |

## Core fix: types are not value descriptors

Stage 2 core compared item types through `TypeKind::to_string()` of *value descriptors*:

```text
some(1)       -> OptionValue<some:Nat>
none(Nat)     -> OptionValue<none:Nat>      // a different "type"
list(1)       -> ListValue<Nat;len=1>
list(1, 2)    -> ListValue<Nat;len=2>       // a different "type"
```

So `list(some(1), none(Nat))` was impossible, and so was a list of lists of different lengths. v0.73 adds
`passport::ordinary_type_name`, which maps descriptors to canonical types, and uses it in every Stage 2 whitelist
(partiality, sequence, structural, structural elimination, traversal, function application):

```text
OptionValue<_:T>      -> Option<T>
ResultValue<_:_:T,E>  -> Result<T,E>
ListValue<T;len=_>    -> List<T>
SequenceValue<T;..>   -> Sequence<T>
ProductTerm<..:A*B>   -> ProductType<A*B>
SumInjection<..:A+B>  -> SumType<A+B>
Value<T>              -> T
```

Base types (`Nat`, `Bool`, `Text`, ...) are unchanged. This is a breaking change for Rust callers that declared
collection item types with descriptor strings such as `OptionValue<some:Nat>`; they must use `Option<Nat>`.

`TypeKind::TypedValue { of_type }` (displayed `Value<T>`) is a new ordinary value whose concrete shape is not known
statically: function parameters, function results, `index`, `unwrap_or`, and traversals over collections of
unknown length.

## Syntax

```text
fn name(param: Type, ...) -> Type = expression
```

- One line, inside a theory.
- Types: `Nat`, `Bool`, `Text`, `Option<T>`, `Result<T, E>`, `List<T>`, `Sequence<T>`, `Product<A, B>`.
  Call form is accepted too: `Option(Nat)`.
- Bool literals: `true`, `false`.
- Functions share the theory namespace with `let`; duplicates are `E0002`.
- A function may call only builtins and previously declared functions.

Type arguments inside expressions use identifiers or call form: `none(Nat)`, `ok(5, Text)`,
`empty_list(Option(Nat))`.

## Builtins

```text
Bool        true false not and or
Nat         nat_eq mul monus pred            (monus = truncated subtraction; Nat has no negatives)
control     if_then_else(cond, then, else)   (lazy at runtime; both branches must have the same type)
Option      some(x) none(T) is_some is_none unwrap_or
Result      ok(x, E) err(e, T) is_ok is_err unwrap_or
List/Seq    list(...) sequence(...) empty_list(T) empty_sequence(T) length index head
Product     pair(a, b) fst snd
traversal   map(xs, f)  fold(xs, init, f)
```

`index` and `head` return `Option<T>`; out-of-range access is `none`, never an exception.

## Function contracts

Every `fn` produces a `FunctionContract` passport, reported as `Theory.name`:

```text
purity    pure, unless the body (or a called function) uses an effectful builtin:
          print_* -> io@stdout, read*/parse_nat -> io@stdin, require -> runtime@require,
          GPU transfers/launch -> gpu_execution, deploy/schedule/migrate -> remote_execution
totality  total, unless the body uses require / read_nat / read / parse_nat or calls a partial function
status    verified        pure + total + untainted
          downgraded      effectful, partial, or Axiom/Oracle/Unsafe-tainted
```

Totality evidence is `StaticProof<total_structural(name)>` for non-recursive functions: the body has no self-reference
and calls only previously declared functions, so mutual recursion cannot be written. Recursive functions need a
declared measure (v0.74, below) and get `StaticProof<well_founded(name:param:measure)>`.

## Traversal

`map` and `fold` require a verified contract (`E0929` otherwise).

- **Statically shaped collections** (`list(...)`, `sequence(...)`, `map` results) produce a core traversal report with
  `fuel = length`. The report must be `verified_bounded`, and its status and fingerprint are recorded in the result
  history. `map` keeps the static length.
- **Collections of unknown length** (function parameters) are traversed at runtime under the same verified contract,
  with exactly one application per item. The result history records `runtime_bounded_by_length`.

Fold steps have signature `(Acc, Item) -> Acc`. Their contract domain is `ProductType<Acc*Item>`, which is exactly
what the traversal layer expects.

## Trust

- Taint is never cleaned. A function whose body uses `axiom_nat()` has an Axiom-tainted body, a downgraded contract,
  and call results with `trust=Axiom`. `--trusted-only` rejects such a function and every call to it.
- Function objects are `Builtin`-trust by core design (`function_type_passport`), so values produced by `map`/`fold`
  report `trust=Builtin`. This is visible and allowed by `--trusted-only`.

## Runtime

New runtime values:

```text
some(x)  none  ok(x)  err(x)  [a, b]  seq[a, b]  (a, b)  fn:name
```

Nat arithmetic is exact `u128`; overflow is a runtime error, never wraparound. Nested user-function calls are guarded
by `MAX_CALL_DEPTH = 256`. Checked programs cannot reach this limit because functions are non-recursive.

## Diagnostics

```text
E0928 FunctionTermError        body/return or argument type mismatch, non-ordinary body
E0929 FunctionContractError    map/fold over a non-verified contract
E0932 PartialityTypeError      Option/Result misuse, proof/truth objects as Option/Result values
E0933 SequenceTypeError        mixed item types, non-collection argument
E0934 TraversalError           map/fold signature mismatch
E0935 RecursionBoundaryError   recursive fn
E0937 StandardPreludeError     Bool/Nat/if_then_else misuse
E0002 NameError                fn shadows a builtin, duplicate name, forward reference
```

## Not yet in the surface

```text
user-declared effects clause (effects are inferred)
effectful function values (function values are pure and total by design)
mutual recursion, recursive record types
direct application of a call result: f(1)(2) — bind it first: let g = f(1)
string literals (Text values come only from builtins)
pattern matching syntax (case/field/fst/snd/unwrap_or are the eliminators)
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language   (last recorded delta: v0.61)
Local readiness:         48–56% -> 70–78%   surface syntax, checker, runtime, 13 surface tests, 15 corpus examples
Architectural readiness: 64–76% -> 74–82%   core layers driven from syntax; descriptor-as-type defect fixed
Fundamental readiness:   46–58% -> 52–60%   no formal typing/soundness proof for the surface; recursion not exposed
```

# v0.74 — Well-Founded Recursion, Records and Sums

## Recursion with a declared measure

```dlm
fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
fn total(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, unwrap_or(head(xs), 0) + total(tail(xs)))
fn steps(n: Nat) -> Nat decreasing n fuel 64 = if_then_else(n > 1, steps(monus(n, 2)) + 1, 0)
```

The checker decides well-foundedness syntactically. For the measured parameter `m`:

| measure type | recursive argument | guard (recursive call must be inside) |
|---|---|---|
| `Nat` → `nat_decreasing` | `pred(m)`, `monus(m, k)` with literal `k >= 1` | then-branch of `m > k`, `not(nat_eq(m, 0))`; else-branch of `nat_eq(m, 0)`, `nat_eq(0, m)` |
| `List`/`Sequence` → `structural_subterm` | `tail(m)` | then-branch of `not(is_empty(m))`, `length(m) > k`; else-branch of `is_empty(m)`, `nat_eq(length(m), 0)` |

The guard makes the measure positive (resp. non-empty), so the argument is strictly smaller. Because `if_then_else`
is lazy, an unguarded branch is never evaluated. Other arguments (accumulators) are unrestricted. The function may
be referenced only through direct calls, so recursion through `map`, `fold` or `case` is rejected. Functions still
call only previously declared functions, so there is no mutual recursion.

Every recursive function produces a core `RecursionScheme` report, published as `Theory.f.recursion`:

```text
verified_well_founded   pure, total, untainted body
downgraded              effectful, partial or tainted body (still terminating; not usable by map/fold)
```

**Fuel.** `fuel N` is the maximum recursion depth the runtime executes. It defaults to `DEFAULT_RECURSION_FUEL = 10000`,
and values above `MAX_RECURSION_FUEL = 100000` are rejected by the checker. The measure guarantees termination
mathematically; fuel bounds it operationally. Exhausting fuel is a runtime error (`recursion fuel exhausted`), not a
type error.

**Runtime checks.**

- The runtime re-checks, on every direct recursive call, that the measured value strictly decreases (defense in depth).
- Programs execute on a dedicated thread with a 1 GiB virtual stack (`RUNTIME_STACK_BYTES`), so deep recursion does
  not depend on the caller's stack.
- When less than 64 MiB of that stack remains, a call fails with `runtime stack budget exhausted` instead of aborting
  the process. The per-level cost depends on how deeply the body nests expressions.
- Ordinary operations and user functions are dispatched before the legacy builtin table, and `eval_expr` arms live in
  small functions. Together this cut the per-level stack cost in a debug build from about 80 KB to about 10 KB.

## Records

```dlm
record Point(x: Nat, y: Nat)
record Segment(from: Point, to: Point)

let s = Segment(Point(0, 0), Point(3, 4))
let x = field(field(s, to), x)
```

- `record Name(field: Type, ...)` goes through `structural::record_type`. Names start with an uppercase letter and
  share the theory namespace with `let`/`fn`.
- `Name(v1, ...)` constructs positionally through `structural::record_term`, which checks field types.
- `field(r, name)` projects; its type comes from the declaration.
- The type is `Record<Name>`, written `Name` or `Record<Name>` in signatures and type arguments. An unknown record
  name is `E0002`.
- A record may reference only records declared earlier, and never itself, so record types are well-founded.

## Sums

```dlm
fn from_nat(n: Nat) -> Nat = n
fn from_flag(b: Bool) -> Nat = if_then_else(b, 1, 0)
fn weight(v: Sum<Nat, Bool>) -> Nat = case(v, from_nat, from_flag)
let values = list(inl(7, Bool), inr(true, Nat))
```

- `inl(x, B)` and `inr(y, A)` go through `structural::{sum_type, sum_injection}`; the type is `SumType<A+B>`.
- `is_left(s)` returns a `Bool`.
- `case(s, f, g)` takes named functions `f: A -> R` and `g: B -> R`. Branch parameter and return types must match
  (`E0931`). Taint from both branch bodies flows into the result.

## Additional builtins

```text
tail(xs)  is_empty(xs)  cons(x, xs)  inl  inr  is_left  case  field
```

## Readiness delta (v0.74)

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         70–78% -> 80–86%   recursion, records, sums in syntax; 11 more tests; 11 more corpus examples
Architectural readiness: 74–82% -> 80–86%   RecursionScheme/record/sum core reports driven from syntax
Fundamental readiness:   52–60% -> 56–64%   syntactic well-foundedness only; no lexicographic/mutual measures
```

# v0.75 — Lambdas, Function Values, Higher-Order Functions, Lexicographic Measures

## Function types and values

```dlm
fn twice(f: Fn<Nat, Nat>, x: Nat) -> Nat = f(f(x))
fn adder(k: Nat) -> Fn<Nat, Nat> = fun(n: Nat, n + k)
let inc = fun(n: Nat, n + 1)
let curry = fun(n: Nat, fun(m: Nat, mul(n, m)))
let total = fold(list(1, 2, 3), 0, fun(acc: Nat, x: Nat, acc + x))
```

- `Fn<P1, ..., Pn, R>` is an ordinary type (canonical `Fn<P1,...,Pn,R>`). Function values can be stored in lists,
  options, records and products, returned from functions, chosen by `if_then_else`, and called with `f(args)`.
- `fun(x: T, ..., body)` is a lambda. Parameters are the leading `name: Type` arguments, and the last argument is the
  body. The result type is inferred.
- A declared `fn` used as a value (`map(xs, double)`, `let g = double`) has type `Fn<...>`.

## Invariant: function values are pure and total

A lambda is rejected (`E0929`) if its body has an effect (print, read, require, GPU or remote operations) or is
partial. Effectful or partial `fn`s can be called directly but not used as values. As a result:

- calling a function-typed parameter adds no effect, so higher-order functions keep `verified` contracts;
- `map`, `fold` and `case` accept any function value. Its contract is synthesized as pure/total with a
  `StaticProof<function_value_invariant(...)>` evidence and the value's taint;
- taint is not part of the invariant. A lambda that uses `axiom_nat()` is a valid value with `trust=Axiom`. Results of
  calling it are Axiom-tainted, `map`/`fold` reject it (its contract is downgraded), and `--trusted-only` rejects it.

## Lambdas through the core

Every lambda goes through `function::function_type` and `function::lambda_term`. The bound variable is the parameter
(or `args` over the product domain for several parameters), the body text is the rendered expression, and captures
are the enclosing function or lambda parameters used in the body. The `LambdaTerm` fingerprint is recorded in the
value's history.

## Scoping and closures

Scoping is lexical. A lambda sees its own parameters, the enclosing function or lambda parameters (captured by value
at creation) and theory-level values. Calls resolve in the order: local value, theory value, declared `fn`, record
constructor. The checker and runtime use the same order.

A function-typed binding (`let`, `fn` parameter, lambda parameter) may not use a builtin name (`E0002`), because
builtin dispatch comes first and the binding could never be called.

## Recursion cannot escape through function values

- A recursive `fn` may not reference itself as a value, call itself inside a lambda, or be shadowed by a lambda
  parameter (`E0935`).
- Functions still see only previously declared functions.
- With simple types (no self-application), first-order measured recursion and total function values, every checked
  program terminates.

## Lexicographic measures

```dlm
fn ack(m: Nat, n: Nat) -> Nat decreasing (m, n) = if_then_else(nat_eq(m, 0), n + 1, if_then_else(nat_eq(n, 0), ack(pred(m), 1), ack(pred(m), ack(m, pred(n)))))
```

A recursive call is accepted when, for some measured position `i`, the argument for `mi` decreases (pred/monus/tail)
under a guard for `mi`, and every earlier measured parameter is passed unchanged. Later measured and unmeasured
arguments are unrestricted. The core report is `RecursionScheme<f:lexicographic:...>`. The runtime re-checks that the
measured tuple strictly decreases in lexicographic order on every direct recursive call.

## Documentation checks

`tests/example_corpus.rs::documented_dlm_blocks_check` checks every ```` ```dlm ```` block in `README.md` and this
document. Fragments without a `theory` are wrapped in one.

## Readiness delta (v0.75)

```text
Stage 2 — Ordinary mathematics of the language
Local readiness:         80–86% -> 86–90%   lambdas, Fn types, higher-order fns, closures, lexicographic measures; 10 tests; 10 corpus examples
Architectural readiness: 80–86% -> 84–88%   core LambdaTerm and Lexicographic RecursionScheme driven from syntax
Fundamental readiness:   56–64% -> 60–66%   termination argument is informal (simple types + syntactic measures), not machine-checked
```

# v0.90 — Result matching and source F2 libraries

`match r { ok(value) => ..., err(error) => ... }` binds Result payloads with
their respective types, supports either order/catch-alls/nesting, and evaluates
a complex scrutinee once. Exhaustiveness, duplicate/mixed/unreachable patterns
and branch result types are checked. Internal projections are reserved, guarded
and supported by both backends.

`std.matrix_f2` and `std.homology_f2` are ordinary source libraries, with explicit
Result errors and public axes <=32; no mathematical builtins were introduced.
See `MATH_SOURCE_AND_WITNESSES.md` for access, shape validation, dense-backend
limitations and the separate host-side computation-witness assurance.
