# RESOLUTION.md — DLM v0.34 ID / Resolver Skeleton

## 1. Purpose

`v0.34.0` adds the first explicit ID and resolver foundation for DLM.

This is an architectural hardening layer, not a language expansion. The existing parser, checker, runtime, passport rules and bridge semantics remain compatible. The new resolver is intentionally a skeleton that can be used by later HIR / ResolvedHIR passes without forcing `checker.rs` to change immediately.

## 2. Why IDs are needed

String names are acceptable in early AST, but they are unsafe as the project moves toward imports, modules, aliases, public/private declarations and multi-file checking.

The intended boundary is:

```text
Raw AST / HIR:
  user-written names are still strings

ResolvedHIR and later passes:
  theories, values, bridges, types and proofs are referenced by IDs
```

This prevents later passes from depending on fragile string equality and prepares the project for deterministic symbol tables.

## 3. ID types

`crates/dlm_core/src/ids.rs` defines separate ID spaces:

```text
FileId
ModuleId
TheoryId
ValueId
TypeId
BridgeId
ProofId
```

Each ID is a small newtype over `u32` with:

```text
Debug
Clone / Copy
PartialEq / Eq
PartialOrd / Ord
Hash
Display
raw()
new(...)
```

The newtypes are deliberately not interchangeable. A `TheoryId` cannot be accidentally passed as a `BridgeId` without an explicit type error.

## 4. ID allocator

`IdAllocator` provides monotonic process-local IDs:

```rust
let mut ids = IdAllocator::new();
let module = ids.alloc_module();
let theory = ids.alloc_theory();
```

These IDs are compiler/checker-local. They are not stable serialization IDs and must not be used as source-level names.

## 5. Resolver skeleton

`crates/dlm_core/src/resolve.rs` adds:

```text
Resolver
resolve_module(...)
ResolvedModule
ResolvedTheory
ResolvedValue
ResolvedBridge
SymbolTable
```

The current resolver performs the first deterministic structural pass:

```text
AST Module
  -> assign ModuleId
  -> assign TheoryId
  -> assign ValueId for let bindings
  -> assign BridgeId
  -> resolve bridge source/target theories
  -> build SymbolTable
```

## 6. Current checks

The v0.34 resolver checks:

```text
duplicate theory names
duplicate value names within one theory
duplicate bridge names
unknown bridge source theory
unknown bridge target theory
```

It reports these as `DiagnosticKind::NameError`.

## 7. What v0.34 does not do yet

This patch intentionally does not implement full HIR or full name resolution.

Not included yet:

```text
import graph resolution
qualified module paths
alias resolution
visibility/public/private rules
function/builtin name binding
expression rewriting to IDs
checker orchestration split
```

Those belong to the next passes after the skeleton is stable.

## 8. Invariant

The core invariant introduced by v0.34 is:

```text
After resolver success, every declared theory/value/bridge in the module has a typed ID, and every bridge endpoint references a known TheoryId.
```

This is the first step toward:

```text
AST -> HIR -> ResolvedHIR -> TypedIR -> PassportIR -> CheckedModule
```

