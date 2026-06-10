# Checker Orchestration — v0.35

`v0.35.0` introduces the first explicit checker pass pipeline without changing the public DLM syntax.

The old semantic checker still performs the actual passport/type/proof/runtime checks, but it is no longer treated as the only stage in the pipeline. The checker report now carries a `PassPipelineReport` so future compiler phases can be split and tested independently.

## Current pipeline

```text
RawAST accepted
  -> name_resolution
  -> legacy_checker
```

### `raw_ast_accepted`

This pass records that the parser has already produced a `Module` AST. Parsing still happens before `Checker::check_module(...)`.

### `name_resolution`

This pass runs the `v0.34` resolver skeleton:

```text
Module -> ResolvedModule
```

It assigns IDs to modules, theories, values and bridge declarations and rejects structural name-resolution errors such as duplicate names or bridge references to unknown theories.

### `legacy_checker`

This pass is the existing checker logic. It still owns passport inference, bridge policy validation, proof/runtime boundary checks and trust policy checks.

## Failure behavior

If name resolution fails, `legacy_checker` is marked as `Skipped`. This prevents later semantic stages from running over an invalid symbol graph.

If name resolution succeeds but the semantic checker reports errors, `legacy_checker` is marked as `Failed`, while the semantic diagnostics remain in `CheckReport::diagnostics`.

## Invariant

A later pass must not silently repair an earlier failed pass.

```text
failed name_resolution => skipped legacy_checker
```

This prepares the project for the next splits:

```text
RawAST
  -> HIR
  -> ResolvedHIR
  -> TypedIR
  -> ProofIR
  -> PassportIR
  -> CheckedModule
```
