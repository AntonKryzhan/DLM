# v0.100 — Typed Core, Identities and Spans (Stage B)

Laws 11–13 require that checked types, effects, identities and source positions reach the backend, so that no
later stage reconstructs the meaning of the program heuristically. v0.87 establishes this path for the executable
surface.

```text
source ──parse──> AST (ExprId + file/Span; DeclId on functions and records)
       ──check──> CheckReport.typed: TypedFacts (type per ExprId, contract per function)
       ──lower──> typed IR (type + Span + origin ExprId per node; signature, effects, termination per function)
       ──verify_ir──> structural and type invariants
       ──> machine-range analysis, contracts, C backend (scalar paths read IR types)
```

## Expression identity and spans (laws 12, 13)

* `Expr::id: ExprId` is unique in the process. Clones keep it, so the copies of a scrutinee that `match`
  desugaring produces refer to the same checked fact. It identifies an expression *within a compilation*; the
  stable identity of an exported function is its symbol `module::Theory.name` (`IrFunction::symbol`).
* `Expr::span` is the exact physical range: start and end line, 1-based **UTF-8 byte** columns, end exclusive.
  The parser maps positions of a logical line (lines joined after `=`, `,`, `=>`, `in`, ...) back to physical
  lines and undoes the masking of text literals, so a span covers the literal with its quotes and escapes. Nodes
  created by `match` desugaring carry the span of the whole `match`; nodes built outside the parser carry the
  line only. Equality of expressions ignores ids and spans.
* v0.99 adds `SourceSpan::file_id` and a `SourceFile` context to modules and resolved modules.
  Linking rebinds independently parsed modules to distinct compilation-local files; the root uses `FileId::ROOT`.
  Parser errors use physical coordinates too. Expression-checking and lowering errors keep the innermost
  precise span; named files print `path:line:byte-column`. Declaration errors can remain line-only.
  See `SOURCE_IDENTITY.md` for the boundary, API and acceptance evidence.

## Typed Core facts

The checker records physical spans of all AST expressions (`TypedFacts::expr_spans`)
and types of ordinarily inferred expressions (`expr_types`), keyed by `ExprId`, and,
for every checked function, `FunctionFacts`: parameter and result types, effects (including those of callees),
purity and termination (`non-recursive`, the recursion measure, or `mutual(..)`). An expression inferred with two
different types is recorded as a conflict and left untyped; the verifier then rejects the program as an internal
error. No conflict occurs in the test suite.

v0.100 adds authoritative `function_declarations`/`record_declarations` keyed
by `DeclId`, declaration snapshots/origins, completed-check/context validation and
`executable_types` with frontend-resolved nominal RecordId references. The older
name-keyed function/record maps and `expr_types` remain display/source views;
lowering does not use them to select semantic facts. Missing checked declarations
are errors; signatures and lambda types are no longer reparsed as a fallback.
See `DECLARATION_IDENTITY.md` for the bounded contract and trusted-base limits.

## Typed IR

`IrExpr { op: IrOp, ty, span, origin }`: every node has the type of its source expression (from `TypedFacts`),
its span and the `ExprId` it was lowered from. Record names from checked facts are resolved to nominal
`RecordRef(RecordId)` types throughout the executable IR (`RECORD_SCHEMAS.md`). Synthetic nodes (initializer calls) have no origin. Lambdas get
their parameter types from their declarations; captured slots get the types of the captured locals.
`IrFunction` carries `param_types`, `ret`, `slot_types`, `effects`, `termination`, `symbol` and `line`;
`IrProgram::global_types` types theory-level values. `IrProgram::sources` maps file IDs to optional paths;
`source_spans` retains checked expression origins. `declarations` stores compact declaration origins; named functions and records carry their DeclId, and synthetic functions carry no source declaration ID. The verifier checks file identities, physical ranges and
the correspondence of each non-synthetic origin to its checked span. `dlm build --emit-ir` prints format `DLM-IR v3`, with
record schemas/identities and a signature line per function:

```text
fn f4 HigherOrder.iterate(f, n, x) captures=0 slots=3
  if nat_eq(%1, 0) then %2 else f4(%0, pred(%1), apply(%0; %2))
  : (Fn<Nat,Nat>, Nat, Nat) -> Nat effects=[] termination=nat_decreasing
```

## IR verifier

`verify_ir` runs after every lowering (`lower_program`). It checks:

* every node is typed;
* slots exist, are typed consistently and are defined before use (captures and parameters on entry, `let`
  slots inside their body);
* calls match the callee's arity, parameter types and result; applications match the function type; closures
  match the lambda's captures and signature; globals and function values exist;
* operators: `+` on two operands of the same type, Nat or Int; `^` on Nat; `>` gives Bool; conditions and
  logical operands are Bool; branches and `let` bodies have the node's type; `unwrap_or` matches the payload;
  `inl`/`inr` match the sum side; every compiled builtin has an explicit type rule, including Option/Result,
  List/Sequence, products, map/fold and I/O. Erased `ok`/`err` have exactly one runtime operand; list/sequence
  literals have at least one element (typed empty values use `Empty`);
* `case` branches take the corresponding sum payload and return the node's result type; empty constructors
  match their container; record constructors keep their nominal type and unique field names, and field access
  takes a record;
* every direct builtin effect and every callee effect is included in the caller's contract. Named function
  values and lifted lambdas are pure. The builtin effect classification is shared with the checker and the
  initializer collector; conservative extra effects are permitted;
* a `let` cannot overwrite an active parameter, capture or local. Frame metadata, including overflowing capture
  counts, is checked without allocating according to an inconsistent frame size;
* function bodies produce the declared result; initializers are typed calls of parameterless, capture-free
  `<init>` functions with the same result; every global has exactly one initializer; `main` has no parameters
  or captures.

A failure is reported as `internal compiler error: the typed IR failed verification` (a defect of DLM, not of
the program). A clean result does not prove a lowering or a pass correct.

v0.88 adds checked record schemas and nominal IDs: the verifier checks constructor layout/arity/field order and
types, exact projection layout/index/result types, and nested record identities. Source record names and unknown,
forward or cyclic schema references are rejected. The source/display view is available via `source_type`.

Open (laws 11–13): the verifier does not prove termination or source/IR semantic equivalence.
v0.90 checks bounded dominance guards for match projections; a general refinement solver remains open.
File identity was added in v0.99 and ID-based checked declaration transfer in v0.100. Separate HIR/MIR and
complete frontend resolution of value/call/local/constructor references remain open. Stage C has an initial
independent equational fragment (`EQUATIONAL_REPLAY.md`); C-min acceptance is the next gate, and full proof
replay remains open.

## Consumers

* **Scalar backend**: a function gets a native `nat_t`/`int_t`/`int` entry point when its result, every slot and
  every node have scalar types and only scalar operations occur; the types are read from the IR (the v0.86
  unification pass is gone). Self tail calls of scalar functions are compiled to loops, as in the uniform
  backend.
* **Machine-range analysis**: parameter and result ranges come from the IR types, including lambda parameters;
  the lookup of signatures by name strings and the guessing of an operand's type from the other operand are gone.
* **Contracts** (`dlm contracts`, `contracts_of`): see `SEMANTICS.md` §9.

## Tests

`tests/typed_core.rs`: exact spans (multi-line expressions, UTF-8 literal with escape, `match`), ids; IR types
equal the checker's facts for every node of a program with records, lambdas, Option and Int; every compilable
example verifies; the verifier rejects eight kinds of corrupted IR; contracts report independent properties;
compiled stack exhaustion is a defined failure; interpreter/compiler parity including runtime errors.

`tests/ir_verify.rs` (v0.87.1): six regression groups. A table covers every entry in `COMPILED_BUILTINS`, lowers a
valid source example, then corrupts its result, each constrained operand and runtime arity. A coverage assertion
prevents a newly compiled builtin from bypassing this matrix. Other mutations cover empty values, sum branch
domains/results, lost effects through calls, effectful function values/lambdas, initializer calls, globals,
active slots, malformed frame sizes and record node invariants. The enclosing function signature is kept
consistent for direct result mutations, so rejection must come from the operation's own rule.

## v0.90 match dominance

`ir_verify/guards.rs` checks that partial List/Option/Result projections have a
dominating constructor/nonempty guard for the same immutable local/global.
Branch facts are lexical and function-local. Result projections are typed from
the appropriate success/error component; both are included in the builtin
mutation table. Wrong-subject, removed/swapped-branch and hoisted projections
are rejected even when the two Result payload types agree. This closes the
previous match-precondition obligation for the parser-generated IR fragment;
file IDs, Resolved HIR and general transformation proofs remain open.
