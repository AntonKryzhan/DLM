# COMPILER_PASSES.md — DLM / ЯРД Compiler Pipeline

## 1. MVP compiler goal

MVP compiler is a checker, not a machine-code compiler.

Primary command:

```bash
dlm check path/to/file.dlm
```

Optional debug commands:

```bash
dlm check file.dlm --emit ast

dlm check file.dlm --emit hir

dlm check file.dlm --emit passport-ir
```

## 2. Pipeline overview

```text
1. Source loading
2. Lexing
3. Parsing
4. AST validation
5. Module resolution
6. Import resolution
7. Theory graph construction
8. Bridge graph construction
9. Name resolution
10. Type checking
11. Effect checking
12. Passport inference
13. Capability checking
14. Trust/provenance/validation checking
15. TheoryBridge checking
16. Diagnostics emission
17. Test matrix runner
```

## 3. Source loading

Responsibilities:

- load file from path;
- assign FileId;
- detect UTF-8 errors;
- normalize line endings internally;
- keep original text for diagnostics.

Errors:

```text
E0001 SourceReadError
E0002 InvalidUtf8
```

## 4. Lexing

Input: source text.
Output: token stream with spans.

Responsibilities:

- identify keywords, identifiers, literals, punctuation;
- skip comments;
- preserve spans.

Errors:

```text
E0101 UnknownCharacter
E0102 UnterminatedString
E0103 UnterminatedBlockComment
```

## 5. Parsing

Input: token stream.
Output: AST.

Responsibilities:

- parse `module` declaration;
- parse imports;
- parse theories;
- parse bridges;
- parse theory items;
- parse expressions and type expressions.

Errors:

```text
E0201 ExpectedModuleDecl
E0202 ExpectedToken
E0203 InvalidTheoryItem
E0204 InvalidBridgeDecl
```

## 6. AST validation

Syntactic sanity not requiring name resolution.

Checks:

- only one module declaration;
- no value-level declarations at module top-level;
- bridge has exactly one `kind` item;
- bridge kind is known;
- duplicate local names inside same syntactic block are rejected early when possible.

Errors:

```text
E0301 ValueOutsideTheory
E0302 DuplicateLocalDecl
E0303 MissingBridgeKind
E0304 DuplicateBridgeKind
```

## 7. Module resolution

Input: AST files.
Output: ModuleIds and module graph.

Responsibilities:

- map `module foo.bar` to ModuleId;
- detect duplicate module IDs;
- resolve file path to module path;
- prepare import graph.

Errors:

```text
E0401 DuplicateModule
E0402 ModulePathMismatch
E0403 ImportCycle
```


## 7.1 v0.34 resolver skeleton status

`v0.34.0` implements the first isolated resolver skeleton before full HIR exists.

Current implemented subset:

```text
AST Module
  -> ModuleId
  -> TheoryId for each theory declaration
  -> ValueId for let declarations inside theories
  -> BridgeId for bridge declarations
  -> bridge source/target names resolved to TheoryId
  -> SymbolTable
```

Current resolver checks:

```text
duplicate theory names
duplicate value names within one theory
duplicate bridge names
unknown bridge source theory
unknown bridge target theory
```

The checker is not yet driven by this resolver. This keeps the patch compatible while establishing the ID-based contract for the next HIR / ResolvedHIR split.

## 8. Import resolution

Responsibilities:

- resolve imported modules and symbols;
- apply aliases;
- build scope tables.

Errors:

```text
E0501 UnknownImport
E0502 AmbiguousImport
E0503 PrivateItemImport
```

## 9. Theory graph construction

Responsibilities:

- assign TheoryId to each theory;
- associate theory with module;
- build theory namespace;
- set AmbientTheory for theory body items.

Errors:

```text
E0601 DuplicateTheory
E0602 UnknownTheory
E0603 NoAmbientTheory
```

## 10. Bridge graph construction

Responsibilities:

- assign BridgeId;
- resolve source and target theories;
- classify bridge kind;
- validate bridge trust requirements;
- add bridge to scope only if imported/public.

Errors:

```text
E0701 UnknownBridgeTheory
E0702 DuplicateBridge
E0703 InvalidBridgeKind
E0704 PrivateBridgeUse
E0705 UnsafeBridgeNotAllowed
```

## 11. Name resolution

Responsibilities:

- resolve type names;
- resolve function calls;
- resolve qualified names like `PA.Nat` or `module::theory::item`;
- distinguish syntax reference from semantic transport.

Errors:

```text
E0801 UnknownName
E0802 AmbiguousName
E0803 TheoryQualifiedValueRequiresBridge
E0804 InvalidNamespacePath
```

## 12. Type checking

Responsibilities:

- check expressions against expected types;
- infer simple literal types;
- check function signatures;
- check bridge transform type expressions;
- ensure `StaticProof<P>` and `RuntimeWitness<P>` are distinct.

Errors:

```text
E0901 TypeMismatch
E0902 ExpectedProof
E0903 ExpectedRuntimeWitness
E0904 RuntimeStaticMismatch
E0905 InvalidInfinityMode
```

## 13. Effect checking

Responsibilities:

- ensure `IO` functions are not called in `Pure` functions;
- ensure `Oracle` requires explicit effect;
- ensure `Unsafe` requires explicit effect and build mode.

Errors:

```text
E1001 EffectNotAllowed
E1002 MissingEffectAnnotation
E1003 OracleEffectNotAllowed
E1004 UnsafeEffectNotAllowed
```

## 14. Passport inference

Responsibilities:

- infer passports for literals;
- infer passports for core primitive expressions;
- infer passports for function calls based on signature and core transfer rules;
- infer theory context;
- propagate trust, provenance, validation;
- attach Passport to each TypedExpr.

Examples:

```text
7 -> Nat<Literal, PRINT_DECIMAL|COMPARE_DIRECT, Trivial, Checked, InternalLiteral, ProofChecked, U0, VALUE, current theory>
10^100 -> Nat<Compressed, SYMBOLIC_PRINT|COMPARE_BY_PROOF, Compressed, Checked, InternalDerived, ProofChecked, U0, VALUE, current theory>
BB(1000) -> Nat<Definable, COMPARE_BY_PROOF, Uncomputable, Builtin, BuiltinKnown, ProofChecked, U0, PROOF, current theory>
```

Errors:

```text
E1101 PassportInferenceFailed
E1102 UnsupportedPassportAnnotation
E1103 InvalidPassportConstraint
```

## 15. Capability checking

Responsibilities:

- verify operation required capabilities;
- reject missing capabilities;
- explain why capability is absent;
- suggest parse/require/bridge/proof when relevant.

Errors:

```text
E1201 CapabilityMissing
E1202 AccessError
E1203 CannotPrintNonExpandable
E1204 CannotCompareWithoutProof
E1205 ExternalInputNotValidated
```

## 16. Trust/provenance/validation checking

Responsibilities:

- propagate trust taint;
- enforce build mode maximum trust;
- prevent Raw external input from mathematical use;
- distinguish RuntimeWitness from StaticProof.

Errors:

```text
E1301 TrustTaintError
E1302 UnsafeLeakError
E1303 AxiomNotAllowed
E1304 OracleNotAllowed
E1305 RawExternalInputUse
```

## 17. TheoryBridge checking

Responsibilities:

- detect values used outside home theory;
- require explicit bridge;
- apply bridge transfer rules;
- reject soundness/reflection without trust marking;
- ensure `quote` produces syntax, not truth.

Errors:

```text
E1401 TheoryBridgeRequired
E1402 NoBridgeInScope
E1403 InvalidBridgeApplication
E1404 SoundnessBridgeRequired
E1405 ProofIsProvabilityNotTruth
E1406 QuoteProducesSyntaxOnly
```

## 18. Diagnostics emission

Responsibilities:

- collect errors and warnings;
- sort by file/span;
- emit human-readable diagnostics;
- optionally emit JSON diagnostics.

MVP must support:

```bash
dlm check file.dlm --format human

dlm check file.dlm --format json
```

## 19. Test runner

MVP must support a simple test harness:

```bash
dlm test tests/
```

Test files can include expected diagnostics:

```dlm
// expect_error: E1203
let g = Graham();
print_decimal(g);
```

## 20. Pass dependency graph

```text
Source loading
  -> Lexing
  -> Parsing
  -> AST validation
  -> Module resolution
  -> Import resolution
  -> Theory graph
  -> Bridge graph
  -> Name resolution
  -> Type checking
  -> Effect checking
  -> Passport inference
  -> Capability checking
  -> Trust/validation checking
  -> TheoryBridge checking
  -> Diagnostics
```

## 21. MVP implementation order

1. Source loader + diagnostics spans.
2. Lexer.
3. Parser for module/theory/bridge/let/fn/type.
4. AST validation.
5. Module and theory resolver.
6. Minimal type checker for Nat/Bool/Bytes/Result/External.
7. Passport struct and lattice operations.
8. Passport inference for literals and primitive calls.
9. Capability checker.
10. Trust taint and build modes.
11. I/O provenance model.
12. TheoryBridge checks for quote/transport.
13. Test runner.
14. JSON emit for PassportIR.

## v0.35.0 — First executable pass pipeline

The compiler/checker pipeline now has an explicit runtime representation:

```text
raw_ast_accepted -> name_resolution -> legacy_checker
```

The existing semantic checker is intentionally called `legacy_checker` in the pass report. This is not a deprecation of semantics; it is a boundary marker showing where future split passes will be inserted.

The new invariant is:

```text
failed name_resolution => skipped legacy_checker
```

This prevents semantic checking from running over an invalid symbol graph and prepares later `HIR`, `ResolvedHIR`, `TypedIR`, `ProofIR` and `PassportIR` stages.

## v0.36.0 — Invariants before deeper pass splitting

Before splitting `legacy_checker` into `typeck`, `proofck`, `passport_infer`, `bridgeck` and `audit`, DLM now has a property-style invariant suite:

```text
cargo test -p dlm_core --test property_invariants
```

This suite is part of the pass-splitting safety net. Future compiler passes must preserve the same trust/passport/bridge laws that the old checker path currently satisfies.

## v0.37.0 — Meta-level foundation before theorem/proof passes

`v0.37.0` adds `meta_level.rs` as a semantic foundation for future HIR/TypedIR/ProofIR passes.

This layer is intentionally not a full pass yet. It defines the invariant that later passes must preserve:

```text
observing syntax/provability/truth/self-reference of level N requires observer level > N
```

This prevents future reflection, theorem and proof-goal passes from silently collapsing object language and meta language.

## v0.38.0 statement/theorem layer

`v0.38.0` introduces the declaration vocabulary that future HIR/ProofIR passes will use:

```text
Statement<P>
Goal<P>
Hypothesis<P>
Theorem<name:P>
```

The legacy checker is not split yet. The new layer is a pure passport/model API so later passes can separate:

```text
proposition formation
statement declaration
goal opening
hypothesis accounting
proof closure
theorem export
```

The key pass invariant is:

```text
Theorem export must require StaticProof evidence or explicit axiom-tainted admission.
```


## v0.39.0 proof-context layer

`v0.39.0` adds an internal proof-context layer after the statement/theorem foundation.

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```

This remains API-level only for now. Parser, CLI syntax and legacy checker behavior are unchanged.


## v0.40.0 tactic-script layer

`v0.40.0` adds a Rust-side tactic-script layer above `ProofContext`.

Current shape:

```text
Raw AST
  -> name_resolution
  -> legacy_checker
  -> statement/theorem model
  -> proof_context
  -> tactic_script
```

The layer is not wired into `.dlm` parsing yet. It exists to define the future proof orchestration API and to protect tactic-specific invariants before ProofIR is introduced.

## v0.41.0 — Proof certificate foundation

The proof-certificate layer sits after proof closure / tactic execution:

```text
Goal<P>
  -> ProofContext
  -> TacticScript
  -> ProofClosure<Theorem<name:P>>
  -> ProofCertificate<name:P>
```

The legacy checker is not routed through this pass yet. The new code is API-level infrastructure for later ProofIR / certificate serialization work.

Important rule:

```text
open obligations cannot produce certificates
```


## v0.42.0 — Proof certificate audit/export foundation

The certificate audit/export layer is an internal post-checking artifact pass.

It validates certificate stability, renders a canonical text form, and produces an audit report against a theorem passport.


## v0.43.0 — Equality/rewrite foundation

The equality/rewrite layer is currently a core model, not an active compiler pass.

Future pass placement:

```text
parse -> resolve -> type/check -> proof/equality checking -> rewrite planning -> certificate/audit
```

`v0.43.0` only introduces the artifacts and laws needed for a later rewrite-planning pass.
