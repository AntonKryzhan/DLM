# AST_IR.md — AST, HIR, TypedIR, PassportIR Design

## 1. Compiler representation pipeline

```text
Source text
→ Tokens
→ AST
→ HIR
→ TypedIR
→ PassportIR
→ CheckedModule
→ Diagnostics / JSON output
```

## 2. AST goals

AST is syntax-preserving enough for diagnostics.

Properties:

- keeps source spans;
- preserves user-written names;
- no name resolution;
- no type inference;
- no theory inference.

Rust sketch:

```rust
pub struct AstFile {
    pub module: AstModuleDecl,
    pub items: Vec<AstModuleItem>,
    pub span: Span,
}

pub enum AstModuleItem {
    Import(AstImportDecl),
    Theory(AstTheoryDecl),
    Bridge(AstBridgeDecl),
    Alias(AstAliasDecl),
    Test(AstTestDecl),
}
```

## 3. AST module

```rust
pub struct AstModuleDecl {
    pub path: Vec<Ident>,
    pub span: Span,
}
```

## 4. AST theory

```rust
pub struct AstTheoryDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub items: Vec<AstTheoryItem>,
    pub span: Span,
}

pub enum AstTheoryItem {
    Type(AstTypeDecl),
    Fn(AstFnDecl),
    Let(AstLetDecl),
    Axiom(AstAxiomDecl),
    Theorem(AstTheoremDecl),
    Proof(AstProofDecl),
    ImportTheory(AstImportTheoryDecl),
}
```

## 5. AST bridge

```rust
pub struct AstBridgeDecl {
    pub visibility: Visibility,
    pub trust_modifier: Option<TrustModifier>,
    pub name: Ident,
    pub source: AstPath,
    pub target: AstPath,
    pub items: Vec<AstBridgeItem>,
    pub span: Span,
}

pub enum AstBridgeItem {
    Kind(BridgeKind),
    Preserves(Vec<Ident>),
    Transform(AstTypeExpr, AstTypeExpr),
}
```

## 6. HIR goals

HIR is resolved, normalized high-level IR.

Properties:

- module IDs resolved;
- theory IDs resolved;
- item IDs assigned;
- imports expanded;
- names bound;
- AST sugar reduced;
- no full type checking yet.

Rust sketch:

```rust
pub struct HirModule {
    pub id: ModuleId,
    pub imports: Vec<HirImport>,
    pub theories: Vec<HirTheory>,
    pub bridges: Vec<HirBridge>,
}

pub struct HirTheory {
    pub id: TheoryId,
    pub visibility: Visibility,
    pub items: Vec<HirTheoryItem>,
}
```

## 7. ID model

```rust
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct ModuleId(u32);

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct TheoryId(u32);

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct BridgeId(u32);

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct ItemId(u32);
```


## 7.1 v0.34 concrete ID skeleton

`crates/dlm_core/src/ids.rs` now provides concrete typed IDs for the current Rust implementation:

```rust
pub struct FileId(pub u32);
pub struct ModuleId(pub u32);
pub struct TheoryId(pub u32);
pub struct ValueId(pub u32);
pub struct TypeId(pub u32);
pub struct BridgeId(pub u32);
pub struct ProofId(pub u32);
```

The current `IdAllocator` gives each ID kind its own monotonic space. These IDs are compiler-local and are not source syntax.

`crates/dlm_core/src/resolve.rs` adds the first concrete resolved representation:

```text
ResolvedModule
ResolvedTheory
ResolvedValue
ResolvedBridge
SymbolTable
```

This is not full HIR yet. It is the compatibility-safe bridge between the old string-based AST/checker world and the future ResolvedHIR world.

## 8. TheoryContext in HIR

Each theory item receives ambient theory:

```rust
pub struct HirItemHeader {
    pub id: ItemId,
    pub name: Symbol,
    pub ambient_theory: TheoryId,
    pub visibility: Visibility,
    pub span: Span,
}
```

Module-level bridge declarations have no ambient theory but reference source and target theories explicitly.

## 9. TypedIR goals

TypedIR contains:

- resolved types;
- expression types;
- function signatures;
- effect signatures;
- unresolved or inferred passports represented as variables.

```rust
pub struct TypedModule {
    pub module: ModuleId,
    pub theories: Vec<TypedTheory>,
    pub bridges: Vec<TypedBridge>,
}

pub struct TypedExpr {
    pub id: ExprId,
    pub ty: TypeId,
    pub kind: TypedExprKind,
    pub span: Span,
}
```

## 10. Type representation

```rust
pub enum TypeKind {
    Nat,
    Bool,
    Bytes,
    Text,
    Result(TypeId, TypeId),
    Option(TypeId),
    External(TypeId),
    Runtime(TypeId),
    Checked(TypeId, PredicateId),
    StaticProof(PredicateId),
    RuntimeWitness(PredicateId),
    Term { theory: TheoryId, inner: TypeId },
    User { theory: TheoryId, item: ItemId, args: Vec<TypeId> },
}
```

## 11. PassportIR goals

PassportIR is TypedIR plus inferred passports and access checks.

```rust
pub struct PassportExpr {
    pub typed: TypedExpr,
    pub passport: Passport,
}
```

## 12. Passport struct

```rust
pub struct Passport {
    pub construction: ConstructionMode,
    pub capabilities: CapabilitySet,
    pub cost: CostDomain,
    pub trust: TrustLevel,
    pub provenance: Provenance,
    pub validation: ValidationState,
    pub universe: UniverseLevel,
    pub equality: EqualityModeSet,
    pub theory: TheoryContext,
}
```

## 13. ConstructionMode

```rust
pub enum ConstructionMode {
    Literal,
    Expression,
    Compressed,
    Recursive,
    ProofDefined,
    Definable,
    OracleDefined,
    ExternalRuntime,
    UnsafeAssumed,
}
```

## 14. CapabilitySet

Use bitflags or persistent small set.

```rust
bitflags::bitflags! {
    pub struct CapabilitySet: u64 {
        const PRINT_DECIMAL      = 1 << 0;
        const SYMBOLIC_PRINT     = 1 << 1;
        const COMPARE_DIRECT     = 1 << 2;
        const COMPARE_BY_PROOF   = 1 << 3;
        const COMPUTE_MODULAR    = 1 << 4;
        const EXPAND             = 1 << 5;
        const INSPECT_AST        = 1 << 6;
        const QUOTE              = 1 << 7;
        const TRANSPORT          = 1 << 8;
        const USE_STATIC_PROOF   = 1 << 9;
        const USE_RUNTIME        = 1 << 10;
        const REQUIRES_ORACLE    = 1 << 11;
    }
}
```

## 15. TrustLevel

```rust
pub enum TrustLevel {
    Checked,
    Builtin,
    Axiom,
    Oracle,
    Unsafe,
}
```

Order:

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

`join_trust` returns the least upper bound, i.e. the more contaminated level.

## 16. Provenance

```rust
pub enum SourceKind {
    InternalLiteral,
    InternalDerived,
    BuiltinKnown,
    Stdin,
    File,
    Network,
    ForeignFunction,
    Oracle,
    UnsafeExternal,
}

pub struct Provenance {
    pub source: SourceKind,
    pub controlled: bool,
    pub reproducible: bool,
}
```

## 17. ValidationState

```rust
pub enum ValidationState {
    Raw,
    Parsed,
    RuntimeChecked,
    ConstraintChecked,
    ProofChecked,
    AssumedUnsafe,
}
```

## 18. TheoryContext

```rust
pub struct TheoryContext {
    pub home: TheoryId,
    pub valid_in: SmallSet<TheoryId>,
    pub assumptions: SmallSet<AxiomId>,
    pub bridge_trace: Vec<BridgeId>,
}
```

## 19. BridgeIR

```rust
pub struct TypedBridge {
    pub id: BridgeId,
    pub source: TheoryId,
    pub target: TheoryId,
    pub kind: BridgeKind,
    pub trust: TrustLevel,
    pub preserves: PreserveSet,
    pub transforms: Vec<BridgeTransform>,
}

pub enum BridgeKind {
    DefinitionalExtension,
    ExplicitImport,
    Quote,
    Transport,
    Soundness,
    Reflection,
    UnsafeCast,
}
```

## 20. Operation signatures

Each primitive operation has an operation descriptor:

```rust
pub struct OperationSpec {
    pub name: Symbol,
    pub required_caps: Vec<CapabilitySet>,
    pub effect: EffectSet,
    pub transfer: PassportTransferFnId,
}
```

MVP transfer functions are implemented in Rust core only.

## 21. Diagnostics requirements

Every AST/HIR/IR node must retain `Span` for precise diagnostics.

```rust
pub struct Span {
    pub file: FileId,
    pub start: BytePos,
    pub end: BytePos,
}
```

## 22. Serialization

MVP should emit optional JSON for debugging:

```bash
dlm check file.dlm --emit ast

dlm check file.dlm --emit hir

dlm check file.dlm --emit passport-ir
```

## 23. MVP exclusions

Do not implement in MVP:

- HIR optimizations;
- macro expansion;
- user-defined passport transfer functions;
- proof normalization;
- full dependent elimination;
- codegen backend.

## v0.35.0 pass boundary

`v0.35.0` keeps the current AST unchanged but introduces the first explicit pass boundary in `CheckReport`.

Current state:

```text
Module AST
  -> resolver skeleton
  -> legacy checker
```

Target state:

```text
RawAST
  -> HIR
  -> ResolvedHIR
  -> TypedIR
  -> ProofIR
  -> PassportIR
  -> CheckedModule
```

The name `legacy_checker` is used only to mark the current monolithic semantic stage before it is split.


## v0.43.0 — Equality/rewrite artifacts

New passport-level artifact kinds:

```text
EqProof<lhs=rhs>
RewriteRule<name:lhs->rhs>
RewriteCertificate<from->to>
```

These are not parser-level AST nodes yet. They are core semantic artifacts exported by `equality.rs` for later HIR and tactic integration.

The distinction is intentional:

```text
Bool != EqProof != RewriteCertificate
```


## v0.44 Rewrite normalization IR note

Rewrite normalization is represented as core IR data only. It is not yet syntax.

```text
RewriteNormalizationReport {
  input,
  normal_form,
  trace: RewriteTrace,
  certificate: RewriteCertificate<input, normal_form>
}
```


## v0.45 Nat induction IR note

Nat induction is represented as passport-level proof artifacts before any surface syntax exists:

```text
NatInductionScheme { proposition_family }
InductionBaseCase { proposition }
InductionStepCase { proposition }
InductionProof { proposition }
```

These artifacts prepare a future ProofIR/TacticIR layer. They are not parsed from `.dlm` syntax in v0.45.


## v0.46 Module / Import IR foundation

Core-only IR structures now model module manifests, imports, exports, and resolved import graphs. The objects are represented by passports `ModuleManifest`, `ImportGraph`, and `ModuleExport`, but no new `.dlm` syntax is introduced yet.


## v0.47 module interface IR

`ModuleInterface<module>` and `ModuleImportAudit<importer->provider:status>` are core audit artifacts. They are not theorem, proof, truth, or runtime values.


## v0.48.0 metatheory dependency IR

New audit passport kinds:

```text
AxiomRegistry<T>
MetatheoryDependencyAudit<subject:status>
```

They are audit objects, not proofs, theorems or truth claims.

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
