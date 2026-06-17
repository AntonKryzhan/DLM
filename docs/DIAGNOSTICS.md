# DIAGNOSTICS.md — DLM / ЯРД Diagnostic Codes

## 1. Diagnostic philosophy

DLM/ЯРД diagnostics must not merely say “type error”.

They must explain:

```text
what operation was attempted;
which passport field blocked it;
what capability/trust/theory/validation was missing;
where the value came from;
how to fix it safely.
```

## 2. Diagnostic format

Human format:

```text
error[E1203]: cannot print decimal representation of non-expandable value
  --> examples/big_numbers.dlm:6:5
   |
 6 |     print_decimal(g)
   |     ^^^^^^^^^^^^^ operation requires capability PRINT_DECIMAL
   |
   = value `g` has passport:
       type: Nat
       construction: Recursive
       cost: NonExpandable
       capabilities: SYMBOLIC_PRINT, COMPARE_BY_PROOF
       trust: Builtin
   = help: use `describe(g)` or prove a modular/structural property instead
```

JSON format:

```json
{
  "severity": "error",
  "code": "E1203",
  "message": "cannot print decimal representation of non-expandable value",
  "span": { "file": "examples/big_numbers.dlm", "start": 42, "end": 56 },
  "passport": { "type": "Nat", "cost": "NonExpandable" },
  "help": "use describe(g) or prove a modular/structural property"
}
```


## 2.1. Current implementation status — v0.33.0

`v0.33.0` introduces the first concrete span foundation in the Rust prototype. The public diagnostic object now keeps both the old line-only API and a new source span:

```rust
pub struct Diagnostic {
    pub line: Option<usize>,
    pub span: Option<SourceSpan>,
    // ...
}
```

The compatibility rule is strict:

```text
Diagnostic::error(kind, Some(line), message)
    -> keeps old human output
    -> also stores SourceSpan::line(line)

Diagnostic::error_at(kind, SourceSpan::line_col(line, col, len), message)
    -> stores precise line/column span
    -> prints line + column in human diagnostics
```

Current human output examples:

```text
error[E0002 NameError] at line 3: unknown name
error[E0001 ParseError] at line 7, column 12: bad token
```

This is intentionally a foundation layer. It does not yet implement full source snippets, secondary labels, byte offsets, file ids, or JSON emission. Those should be added after the resolver/ID skeleton, when source locations can be attached to RawAST/HIR nodes consistently.

---

## 3. Source errors

```text
E0001 SourceReadError
E0002 InvalidUtf8
```

## 4. Lexer errors

```text
E0101 UnknownCharacter
E0102 UnterminatedString
E0103 UnterminatedBlockComment
```

## 5. Parser errors

```text
E0201 ExpectedModuleDecl
E0202 ExpectedToken
E0203 InvalidTheoryItem
E0204 InvalidBridgeDecl
E0205 InvalidTypeExpr
E0206 InvalidExpr
```

## 6. AST validation errors

```text
E0301 ValueOutsideTheory
E0302 DuplicateLocalDecl
E0303 MissingBridgeKind
E0304 DuplicateBridgeKind
E0305 TopLevelProofNotAllowed
```

Example:

```dlm
module bad
let n = 7;
```

Diagnostic:

```text
error[E0301]: value-level declaration outside theory context
help: wrap it in `theory Name { ... }` or `in theory Name { ... }`
```

## 7. Module/import errors

```text
E0401 DuplicateModule
E0402 ModulePathMismatch
E0403 ImportCycle
E0501 UnknownImport
E0502 AmbiguousImport
E0503 PrivateItemImport
```

## 8. Theory errors

```text
E0601 DuplicateTheory
E0602 UnknownTheory
E0603 NoAmbientTheory
E0604 TheoryNameConflict
```

## 9. Bridge errors

```text
E0701 UnknownBridgeTheory
E0702 DuplicateBridge
E0703 InvalidBridgeKind
E0704 PrivateBridgeUse
E0705 UnsafeBridgeNotAllowed
E1401 TheoryBridgeRequired
E1402 NoBridgeInScope
E1403 InvalidBridgeApplication
E1404 SoundnessBridgeRequired
E1405 ProofIsProvabilityNotTruth
E1406 QuoteProducesSyntaxOnly
```

Example:

```dlm
in theory MetaArithmetic {
    let n = PA.seven;
}
```

Diagnostic:

```text
error[E1401]: value from theory `PA` used in theory `MetaArithmetic` without bridge
help: use `quote[PA_quote](PA.seven)` to treat it as syntax, or `transport[bridge](PA.seven)` if a transport bridge exists
```

## 10. Name resolution errors

```text
E0801 UnknownName
E0802 AmbiguousName
E0803 TheoryQualifiedValueRequiresBridge
E0804 InvalidNamespacePath
```

## 11. Type errors

```text
E0901 TypeMismatch
E0902 ExpectedProof
E0903 ExpectedRuntimeWitness
E0904 RuntimeStaticMismatch
E0905 InvalidInfinityMode
E0906 InvalidEqualityMode
```

Example:

```dlm
let n = read_nat(stdin);
proof p : StaticProof<n > 0> = require(n > 0);
```

Diagnostic:

```text
error[E0904]: runtime witness cannot be used as static proof
help: `require(n > 0)` produces RuntimeWitness<n > 0>, not StaticProof<n > 0>
```

## 12. Effect errors

```text
E1001 EffectNotAllowed
E1002 MissingEffectAnnotation
E1003 OracleEffectNotAllowed
E1004 UnsafeEffectNotAllowed
```

Example:

```dlm
fn pure_read() -> Nat {
    io.read_nat(stdin)
}
```

Diagnostic:

```text
error[E1001]: IO effect is not allowed in pure function
help: add `effects IO, ExternalInput` or move read operation outside pure function
```

## 13. Passport inference errors

```text
E1101 PassportInferenceFailed
E1102 UnsupportedPassportAnnotation
E1103 InvalidPassportConstraint
E1104 PassportConflict
```

## 14. Capability/access errors

```text
E1201 CapabilityMissing
E1202 AccessError
E1203 CannotPrintNonExpandable
E1204 CannotCompareWithoutProof
E1205 ExternalInputNotValidated
E1206 CannotUseRuntimeAsStatic
E1207 CannotUseSyntaxAsValue
```

Example:

```dlm
let bb = BB(1000);
print_decimal(bb);
```

Diagnostic:

```text
error[E1203]: cannot print decimal representation of noncomputable value
= bb has cost Uncomputable and lacks PRINT_DECIMAL
help: ask for lower bounds, proof-based comparison, or use an oracle in research mode
```

## 15. Trust/provenance/validation errors

```text
E1301 TrustTaintError
E1302 UnsafeLeakError
E1303 AxiomNotAllowed
E1304 OracleNotAllowed
E1305 RawExternalInputUse
E1306 ValidationRequired
E1307 UntrustedInputInStaticContext
```

Example:

```dlm
let raw = io.read(stdin);
let n : Nat = raw;
```

Diagnostic:

```text
error[E1305]: raw external input cannot be used as mathematical value
help: use parse_nat(raw), then require constraints if needed
```

## 16. Universe/infinity errors

```text
E1501 UniverseLevelError
E1502 SetOfAllSetsRejected
E1503 InfinityModeRequired
E1504 InvalidInfinityOperation
```

Example:

```dlm
let x = Infinity + 1;
```

Diagnostic:

```text
error[E1503]: bare Infinity is not allowed
help: specify Infinity<cardinal>, Infinity<ordinal>, Infinity<limit>, etc.
```

## 17. Warning codes

```text
W2001 AxiomUsed
W2002 OracleUsed
W2003 UnsafeUsed
W2004 LargeFiniteMayBeExpensive
W2005 ImplicitPassportInferred
W2006 BridgeChangesEqualityMode
```

## 18. Diagnostic quality requirements

Every diagnostic should include when applicable:

- primary span;
- secondary spans for value origin;
- passport summary;
- missing capability;
- current AmbientTheory;
- suggested safe fix;
- suggested unsafe fix only if build mode permits.

## 19. Error severity

```text
Error   — compilation/checking fails.
Warning — allowed but noteworthy.
Note    — explanatory message attached to error/warning.
Help    — suggested fix.
```

## 20. MVP required diagnostics

MVP must implement at least:

```text
E0301 ValueOutsideTheory
E0904 RuntimeStaticMismatch
E1201 CapabilityMissing
E1203 CannotPrintNonExpandable
E1204 CannotCompareWithoutProof
E1305 RawExternalInputUse
E1401 TheoryBridgeRequired
E1405 ProofIsProvabilityNotTruth
E1503 InfinityModeRequired
W2001 AxiomUsed
W2003 UnsafeUsed
```

## E0908 — MetaLevelError

Raised when object/meta-level stratification would be violated.

Typical cause:

```text
observer level = M0
object level   = M0
operation      = truth/provability/self-reference/syntax observation
```

Required fix:

```text
perform the operation from a strict higher meta level, e.g. M1 observing M0
```

This protects the reflection boundary: object-level code must not inspect its own truth, provability or self-reference without an explicit meta-level lift.

## E0909 StatementTheoremError

`StatementTheoremError` reports invalid construction at the statement/theorem declaration layer.

Examples:

```text
Theorem requires StaticProof; ProofTerm must be kernel-checked first
Theorem requires StaticProof; RuntimeWitness is not a static proof
theorem construction requires a Statement or Prop object
```

This diagnostic protects the boundary:

```text
Statement<P> != Theorem<P>
RuntimeWitness<P> != StaticProof<P>
ProofTerm<P> != StaticProof<P>
```


## E0910 ProofObligationError

`ProofObligationError` reports invalid proof-context construction or closure.

Typical cases:

```text
open_proof_context requires Goal<P>
Goal<P> cannot be closed by Statement<Q>
Goal<P> cannot be closed by StaticProof<Q>
```

The intended closing rule is exact:

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```


## E0911 TacticScriptError

`TacticScriptError` reports structural errors in internal tactic scripts.

Current protected case:

```text
closing tactic must be final
```

Proof-content mismatches continue to use `ProofObligationError [E0910]`.

## E0912 ProofCertificateError

Added in `v0.41.0` for proof-certificate foundation failures.

Used when:

```text
an open proof/tactic report is certified;
a certificate is emitted for a non-Theorem passport;
a certificate does not match a theorem passport;
a certificate trace length is inconsistent;
a certificate fingerprint no longer matches its contents.
```

This keeps certificate emission separate from proof construction: a certificate is an audit artifact over a closed theorem, not new proof evidence.


## E0913 ProofCertificateAuditError

`ProofCertificateAuditError` reports invalid proof-certificate export or audit state.

Typical causes:

- certificate trace length does not match the embedded trace;
- certificate fingerprint no longer matches certificate contents;
- certificate identity fields are empty;
- certificate does not verify against the requested theorem.


## E0914 EqualityRewriteError

`EqualityRewriteError` reports invalid equality-proof or rewrite-certificate state.

Typical triggers:

- using a `Bool` equality result as rewrite evidence;
- constructing `EqProof<A,B>` from a non-matching `StaticProof`;
- using `RuntimeWitness` or raw `ProofTerm` as static equality evidence;
- applying a rewrite rule to a non-matching source term;
- applying an `EqProof` directly without first deriving a `RewriteRule`.

The invariant is: `Bool == true` is not the same artifact as `EqProof<A,B>`, and a rewrite certificate must preserve the trust/taint of every equality proof used in its trace.


## E0915 RewriteNormalizationError

Raised by the bounded rewrite normalization/audit foundation.

Typical causes:

- normalization rule is not a `RewriteRule` passport;
- rewrite cycle exceeds `max_steps`;
- normalization report input/normal form does not match its trace;
- attached `RewriteCertificate` endpoints, trust or provenance do not match the report.


## E0916 — InductionError

Raised by the Nat induction foundation when an induction scheme, base case, step case or final induction proof is malformed.

Examples:

```text
RuntimeWitness cannot justify a static induction case
ProofTerm must be kernel-checked into StaticProof before induction use
base case proves `Q(0)`, but scheme requires `P(0)`
induction proof proves `forall n:Nat. P(n)`, but theorem statement requires `forall n:Nat. Q(n)`
```


## E0917 ModuleImportError

Raised for invalid module/import/export metadata: duplicate imports, duplicate aliases, duplicate exports, missing import targets, private export leakage, and cyclic import graphs.


## E0918 ModuleInterfaceError

Raised by the module interface / import audit layer when a module interface is stale, mismatched, missing a required public symbol, or when an import audit attempts to use a private symbol or a symbol without an explicit import edge.


## E0919 MetatheoryDependencyError

Raised when axiom registries or metatheory dependency audits are malformed: duplicate axioms, undeclared axiom dependencies, empty audit subjects, or rejected closure evidence.

## v0.49.0 — Metatheory Closure Report Foundation

This patch continues track **1) Metamathematical foundation** by adding a global closure report layer over verified dependency audits.

New core concepts:

- `MetatheoryClosureReport`;
- `MetatheoryClosureStatus::{Closed, Open, Rejected}`;
- `ClosureObligation`;
- `ClosureObligationKind`;
- `metatheory_closure_report(...)`;
- `require_closed_metatheory_closure(...)`;
- `metatheory_closure_report_passport(...)`;
- `export_metatheory_closure_report(...)`.

Main law:

```text
verified dependency audit + closed obligations => closed metatheory closure report
```

Open obligations keep closure open. Rejected dependency audits reject closure. Axiom/oracle/unsafe taint remains visible.

## E0921 ConservativeExtensionError

Raised when a conservative-extension audit is malformed or rejected. Typical causes:

- base metatheory closure is not closed;
- extension closure is rejected;
- no preserved theorem evidence is supplied;
- theorem name changed across extension;
- theorem proposition changed across extension;
- duplicate preserved theorem witness.

## E0922 TheoremDependencyError

Raised by the global theorem dependency graph / metatheory inventory layer.

Typical causes:

- mislabeled graph node;
- duplicate node id;
- duplicate evidence fingerprint;
- edge source or target not present in the inventory;
- self-edge;
- rejected conservative-extension evidence.

### E0923 SoundnessBoundaryError

Raised when a soundness boundary ledger contains hidden, duplicated, malformed, or insufficiently tainted boundary assumptions.


### E0924 TrustedBaseError

Raised when the final trusted-base closure gate is missing required evidence, receives rejected evidence, detects duplicate evidence ids/fingerprints, or receives a non-foundation passport as trusted-base evidence.

- `E0925 MetatheoryFoundationError` — metatheory foundation exit/checklist readiness violation.


## E0926 LogicFormulaError

Raised when logical formula or quantifier construction violates arity, binder, formula-operand or proof/truth/runtime separation rules.


## E0927 — SubstitutionError

Raised when variable scope, alpha-equivalence or substitution would violate explicit binder rules: invalid identifiers, malformed quantifier text, theorem/proof/truth/runtime evidence used as a substitution source, shadowed binder substitution, or capture risk.

## E0928 FunctionTermError

Raised when function type, lambda or application construction violates the ordinary-function boundary: invalid domain/codomain, mismatched lambda parameter domain, shadowed capture, non-function application source, proof/theorem/truth/runtime object used as ordinary function or argument.


## E0929 FunctionContractError

Raised when a function contract violates purity, effect-boundary, totality-evidence or contract-subject rules. This diagnostic protects the boundary between ordinary function objects and proof/theorem/truth/runtime evidence.


## v0.59.0 — Product / Sum / Record Type Foundation

Added `docs/STRUCTURAL_TYPES.md` and the core structural type layer: ProductType/ProductTerm, SumType/SumInjection and RecordType/RecordTerm/RecordProjection. This layer preserves trust taint, rejects proof/truth/runtime objects as ordinary structural values, and prepares future layout/ABI-aware records.


## E0931 StructuralEliminationError

Raised when product/sum/record elimination or pattern matching is attempted on an invalid subject, mismatched sum branches, missing record fields, duplicate pattern bindings, or proof/truth/theorem/runtime objects.

- `E0932 PartialityTypeError`: invalid Option/Result/Partiality construction, branch mismatch, or proof/truth/runtime object used as ordinary partiality value.

- `E0933 SequenceTypeError` — finite List/Sequence construction, item typing, and explicit index boundary errors.

## E0934 TraversalError

Raised when a finite traversal violates the traversal boundary: wrong function contract domain/codomain, invalid accumulator, rejected fuel, or an attempt to consume proof/theorem/truth/runtime evidence as an ordinary traversal value.

- `E0935 RecursionBoundaryError` — invalid recursion scheme, fuel boundary, measure decrease, or recursion contract.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.

## E0937 StandardPreludeError

Raised when a standard prelude contract has an invalid identifier, malformed type identity, signature mismatch, missing verified budget, or non-checked function contract.


## E0938 PreludeEvaluationError

Raised when a standard prelude evaluation request violates the small-step algebraic boundary: unverified contract, wrong input type, insufficient fuel, or proof/theorem/truth/runtime evidence used as an ordinary value.

## E0939 PreludeLoweringError

Raised by the prelude lowering boundary when a lowering artifact name is invalid or when downstream code requires `verified_erased` but receives symbolic, tainted, rejected-target, rejected-evaluation, or evidence-boundary status.

This diagnostic protects the line between small-step algebraic semantics and future runtime/compiler lowering.
