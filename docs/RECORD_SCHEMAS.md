# v0.100 — Checked Record Schemas and Nominal IDs (Stage B)

Since v0.100, record declarations cross the checker/lowering boundary by `DeclId` through
`TypedFacts::record_declarations`. The checker assigns nominal RecordId values and resolves nested field types.
Lowering transfers these facts to `IrProgram::records` (`IrRecord`), retaining declaration identity, display name,
stable source symbol `module::Theory.Record`, ordered fields and source span. The old name-keyed
`TypedFacts::records` (`RecordFacts`) map remains a source/display view, not a lowering input.
`DECLARATION_IDENTITY.md` specifies the checked handoff and snapshot/context guards.

`RecordId` is a u32 compilation-local identity. It is not a package identity or cache key. `OrdinaryType::Record`
belongs to source checking; executable types use `OrdinaryType::RecordRef(RecordId)`, recursively inside
Option, Result, collections, products, sums and function types. Inherited declarations keep the original ID;
equal display names in different theories receive different IDs.

Constructors carry their record ID. Projections carry their layout ID, field index and diagnostic name. The
backend uses the checked schema; it does not infer a field's type or nominal identity from its display name.
Runtime record representation and source syntax are unchanged.

## Verifier contract

* No source record name or unknown record ID survives inside executable expression, slot, parameter, result,
  global or schema types.
* Schema symbols are nonempty and unique; field names are unique. Field types refer only to earlier schemas,
  preserving the current language's well-founded, nonrecursive record model.
* A constructor's ID, nominal result type, display name, number/order/names of fields and every field value
  type agree with the schema.
* A projection's operand has the exact layout ID; its field index exists, its diagnostic name matches that
  index, and its result has the declared field type.
* Calls, applications and container operations compare resolved nested types, so equal display names do not
  make different nominal records interchangeable.

`IrProgram::source_type` reconstructs a source/display view for tools and tests. It is not used by code generation
or verification. `DLM-IR v3` listings expose record schemas and IDs; v2 text consumers must update their parser.

## Validation and API changes

`record_schemas.rs` covers nested and inherited schemas, distinct same-named declarations, constructor arity,
field order/type, bad projections, unknown/unresolved/cyclic IDs and corrupted metadata. The new
`record_schemas.dlm` example is required to compile and agree with the interpreter in the C backend corpus test.

Rust API additions/changes: `RecordId`, `RecordFacts`, `TypedFacts::records`, `IrRecord`, `IrProgram::records`,
`OrdinaryType::RecordRef`, a record ID in `IrOp::Record`, and a structured `IrOp::Field` with layout/index/name.
Source DLM programs keep their existing syntax. No external ABI/layout stability or recursive records are
introduced. v0.99 adds file identities and v0.100 replaces name-based checked-schema selection with DeclIds.
Complete frontend reference resolution and full HIR/MIR remain open; the structural verifier does not prove
source/IR semantic equivalence or ownership.
