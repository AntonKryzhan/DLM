# Portable Code Deploy v0.17

DLM/ЯРД v0.17 adds the first portable-code layer for cross-architecture execution.

## Core rule

A value is not deployed to a node directly as machine code. It is first converted into a portable, passport-aware code object:

```dlm
let code = compile_portable(payload)
let job = deploy_on(pool, arm, code)
```

This yields:

```text
PortableCode<Nat> -> Remote<Nat@aarch64>
```

## Builtins

- `compile_portable(value)` / `portable_code(value)` / `make_portable(value)`
- `deploy_portable(node, code)` / `deploy_code(node, code)`
- `deploy_on(pool, node, code)` / `deploy_to_pool(pool, node, code)`

## Passport effects

`compile_portable(value)` requires:

- `can_compile_portable_code`
- `can_serialize_for_migration`

`deploy_portable(node, code)` requires:

- target node has `can_accept_migration`
- code has `can_deploy_portable_code`

`deploy_on(pool, node, code)` additionally requires:

- pool has `can_schedule_runtime`
- runtime target node is a member of the virtual pool

## Why this matters

This is the first explicit model of one source program becoming portable code that can run on x86_64 and aarch64 nodes through the same logical cluster interface.
