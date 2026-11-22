# v0.100 — Checked declaration identity (Stage B)

This step removes name-based selection of checked function contracts and record
schemas at the frontend/lowering boundary (laws 11–13, 21–22, 30, 39–42).
It completes that bounded bridge after v0.99 source-file identity. It does not
claim full Resolved HIR/MIR or acceptance of C-min.

## Source declarations and identity

`FnDecl::id` and `RecordDecl::id` are process-local `DeclId` values allocated
when parsing. Clones preserve the ID; a new parse gets new IDs, even when all
names, signatures, text and line positions are identical. The allocator is
thread-safe and refuses exhaustion rather than wrapping. IDs have no stable
package, serialization, cache or ABI meaning. Export identity remains the
source symbol `module::Theory.name`; names remain available for diagnostics.

`ResolvedValue::declaration` carries the source ID for functions and records,
separately from the resolver-local ValueId. Lets and formal statements do not
gain declaration IDs in this step. Inheritance reuses the original function
and record declarations; it does not mint new identities for aliases.

## Authoritative checked facts

TypedFacts retains its existing `functions`, `records` and `expr_types` maps
as source/display views. Native lowering no longer selects semantic facts
from those mutable name-based views. Authoritative inputs are:

- `function_declarations: DeclId -> CheckedFunctionDeclaration`;
- `record_declarations: DeclId -> CheckedRecordDeclaration`;
- `executable_types: ExprId -> OrdinaryType`, with nested record references
  already resolved to nominal RecordId values by the frontend.

Each checked declaration contains a source snapshot, checked facts and an
origin (kind, module, theory, name, file/line span). Record facts carry the
nominal RecordId assigned by the checker in schema dependency order. The
frontend propagates record bindings through inherited theories. Function
signatures, record fields and inferred executable expression types use these
identities recursively, including containers and function types.

Named callbacks checked through collection contracts also record their
executable function types. Lambdas receive checked function signatures;
lowering does not reparse lambda annotations or guess missing parameter types.

Only a completed checker run without errors or type/source conflicts marks
facts as checked. Default, failed or conflicting facts are rejected by
`lower_checked_program`. This marker is an internal pipeline invariant,
not a proof certificate or an independent attestation.

## Lowering contract

For every source function or record, lowering looks up its DeclId and checks
the declaration snapshot and origin against the supplied AST. It rejects
missing IDs, mismatched names/owners/positions/snapshots, duplicate source IDs
and a schema order that disagrees with the checked nominal IDs. It also checks
that the source declaration set matches the checked set. Function contracts
are mandatory: no fallback parses a surface signature, invents empty effects
or assigns an unknown termination status when checked facts are absent.

Module names, source files, import headers, theory order, inheritance and
visibility contexts are bound to the check run. A context changed after checking
must be checked again. Independent functions can be reordered while preserving
their source snapshots and IDs; their contracts follow IDs rather than array
position or display names.

Schemas and signatures are copied from checked nominal facts. There is no
source-record-name resolution pass in lowering. The existing source adapter
still resolves value/call/local/constructor names through its scope tables
before producing numeric executable references. This is explicitly remaining
Resolved HIR work; it does not select the checked declaration facts.

These checks protect the stated declaration handoff. They are not a complete
validator of arbitrary edits to an already checked AST or public fact maps.
Source edits must normally rerun the checker. Forging both snapshots and their
authoritative metadata is outside this guarantee. Checker, lowering and the
structural IR verifier remain trusted compiler components; no new StaticProof,
machine-definedness, memory-safety or optimization authority is introduced.

## Executable provenance and verification

`IrFunction::declaration` is Some(DeclId) for a source function and None for
lifted lambdas and synthetic initializers. `IrRecord::declaration` identifies
its source schema. `IrProgram::declarations` holds compact origins rather than
AST snapshots, passports or proofs. No per-value or per-element runtime
metadata is added.

`verify_ir` checks that:

- declaration origins have valid source spans, owners and unique symbols;
- every named source function and record refers to its matching origin;
- no declaration ID is used twice, across functions and records;
- kind, symbol, display name, file and declaration line remain consistent;
- lambdas and initializers do not impersonate source declarations;
- the origin table and executable declaration set have matching cardinality.

Existing nominal layout/type/field/call/effect/source-span checks remain in
force. The readable `DLM-IR v3` listing adds declaration IDs and origins; it
remains a diagnostic view, not a serialized IR ABI. Native value layout and
C ABI are unchanged. Runtime error behavior, arithmetic, evaluation order,
resource limits and proof replay rules are unchanged by this step.

## Validation

```text
cargo test -p dlm_core --test declaration_identity --test typed_core --test record_schemas --test source_identity --test modules --test ir_verify --test mutual_recursion --locked --offline
cargo test -p dlm_cli --test cli_declaration_identity --locked --offline
cargo test --workspace --locked --offline --no-fail-fast
cargo clippy --workspace --all-targets --locked --offline
```

`declaration_identity.rs` covers clone/reparse/concurrent identity allocation,
resolver propagation, canonical nested nominal types, independence from legacy
display maps, absent/swapped/stale declarations, independent function reordering,
context rebinding, duplicate IDs, bad schema order, failed/default/conflicting
facts, inherited private helpers, named callbacks/lambdas and corrupted IR
provenance. `cli_declaration_identity.rs` compares the interpreter and native C
for an imported record program using inheritance, fold and map.

The validation platform is Linux x86-64, supplied Rust 1.99.1 and GCC. Windows
remains unvalidated in this run. The full arithmetic suite may use opt-level=2
with overflow checks and debug assertions enabled. Existing Clippy warnings,
including the v0.99 Diagnostic large-error warnings, remain explicit; they are
not suppressed or addressed by unrelated API changes here.

## Compatibility and next gates

DLM syntax, source APIs, runtime formats and native ABI retain their contracts.
The pre-1.0 Rust metadata API gains fields/types; Rust struct-literal users must
supply DeclIds and the new IR metadata. Parser, checker and lowering entry
points remain available; the already-checked lowering entry point now requires
the authoritative facts of the supplied program. Reparsed source is rechecked.

Next is separate six-condition C-min acceptance, then D/E under the roadmap's
existing dependencies. Full HIR/MIR, complete reference resolution before
lowering, declaration token spans, runtime source maps and the rest of A–H stay
open. Mathematical progress remains N01–N06 closed and N07–N16 open. The Recursive
Discovery Research Program R0–R6 is considered as a deferred research direction;
this step implements no research workflow and does not reorder current work.
