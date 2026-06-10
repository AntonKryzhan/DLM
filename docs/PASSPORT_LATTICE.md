# PASSPORT_LATTICE.md — Passport Product Lattice Specification

## 1. Purpose

The passport lattice defines how DLM/ЯРД safely combines, degrades, preserves or rejects mathematical access to values.

Core idea:

```text
Passport is a product lattice.
```

```text
Passport = Construction
         × Capabilities
         × Cost
         × Trust
         × Provenance
         × Validation
         × Universe
         × Equality
         × TheoryContext
```

## 2. Design principle

Never encode all passport combinations as a flat enum.

Bad:

```text
ExplicitNat
CompressedNat
RecursiveNat
DefinableNonComputableNat
...
```

Good:

```text
Nat + Passport { construction, capabilities, cost, trust, ... }
```

## 3. Partial order

Each axis defines a partial order.

For trust and cost the order generally means contamination / difficulty increases upward.

For capabilities, order is set inclusion:

```text
A <= B iff A.capabilities ⊆ B.capabilities
```

But transfer rules must not blindly use union for results.

## 4. ConstructionMode lattice

MVP order:

```text
Literal
< Expression
< Compressed
< Recursive
< ProofDefined
< Definable
< OracleDefined
< UnsafeAssumed
```

`ExternalRuntime` is not purely comparable with internal construction; it carries Provenance and Validation. For conservative MVP, treat it as at least `Definable` for static proof purposes and not usable in StaticProof without a bridge.

Join examples:

```text
join(Literal, Compressed) = Compressed
join(Recursive, Definable) = Definable
join(OracleDefined, Literal) = OracleDefined
join(UnsafeAssumed, anything) = UnsafeAssumed
```

## 5. TrustLevel lattice

```text
Checked < Builtin < Axiom < Oracle < Unsafe
```

`join_trust(a, b)` returns the more contaminated trust.

Examples:

```text
join(Checked, Builtin) = Builtin
join(Builtin, Axiom) = Axiom
join(Axiom, Unsafe) = Unsafe
```

Rules:

```text
Any result depending on Unsafe is Unsafe.
Any result depending on Oracle is at least Oracle.
Any result depending on Axiom is at least Axiom.
```

## 6. CapabilitySet order

Capabilities are permission bits.

Example capability set:

```text
PRINT_DECIMAL
SYMBOLIC_PRINT
COMPARE_DIRECT
COMPARE_BY_PROOF
COMPUTE_MODULAR
EXPAND
INSPECT_AST
QUOTE
TRANSPORT
USE_STATIC_PROOF
USE_RUNTIME
REQUIRES_ORACLE
```

For checking an operation:

```text
required_caps ⊆ actual_caps
```

For result construction:

```text
result.capabilities ⊆ preserved(lhs.capabilities ∩ rhs.capabilities, operation)
```

Important:

```text
Capabilities are not combined by union for operation results.
```

Bad:

```text
printable + noncomputable -> printable | noncomputable
```

Good:

```text
printable + noncomputable -> noncomputable, not printable
```

## 7. Capability creation rule

A result may receive a capability not present in all inputs only if justified by:

```text
1. Rust-core builtin transfer rule;
2. Checked proof object;
3. Trusted axiom;
4. Oracle;
5. Unsafe rule.
```

Any of 3–5 taints TrustLevel.

## 8. CostDomain

MVP simple CostDomain:

```text
Trivial
< SmallFinite
< LargeFinite
< Compressed
< Recursive
< NonExpandable
< ProofRequired
< Uncomputable
< OracleRequired
```

Interpretation:

- `Trivial` — literals like `7`.
- `SmallFinite` — ordinary finite values safe to expand.
- `LargeFinite` — finite but may be too large for default expansion.
- `Compressed` — expression form such as `10^100`.
- `Recursive` — recursive builder such as Graham-like definitions.
- `NonExpandable` — cannot be expanded into ordinary representation.
- `ProofRequired` — properties require proofs.
- `Uncomputable` — definable but not computable by general algorithm.
- `OracleRequired` — requires external oracle.

## 9. Cost transfer

`join(costA, costB)` is a safe upper bound, not always the precise result.

Each primitive operation defines an operation-specific transfer.

Examples:

```text
add(Trivial, Trivial) -> SmallFinite or Trivial depending on bound
add(Compressed, Trivial) -> Compressed
add(Recursive, Compressed) -> Recursive
add(Uncomputable, anything) -> Uncomputable
compare(Uncomputable, Trivial) -> requires ProofRequired or OracleRequired
print_decimal(NonExpandable) -> AccessError
```

## 10. Provenance axis

Provenance tracks where a value came from.

```text
InternalLiteral
InternalDerived
BuiltinKnown
Stdin
File
Network
ForeignFunction
Oracle
UnsafeExternal
```

Provenance is not just trust. A value can be parsed and runtime-checked but still externally sourced.

## 11. Validation axis

```text
Raw
< Parsed
< RuntimeChecked
< ConstraintChecked
< ProofChecked
```

`AssumedUnsafe` is a contaminating state and should taint TrustLevel to `Unsafe`.

External input starts as:

```text
External<Bytes>:
  provenance = Stdin/File/Network/etc.
  validation = Raw
  trust = Checked for parser infrastructure, but value content is Untrusted/Runtime
  capabilities = can_parse, can_debug_print
```

## 12. Runtime vs Static

Runtime values cannot be used in StaticProof without an explicit bridge.

Static capabilities:

```text
USE_STATIC_PROOF
```

Runtime capabilities:

```text
USE_RUNTIME
```

`require(expr)` on runtime input produces `RuntimeWitness<P>`, not `StaticProof<P>`.

## 13. EqualityModeSet

MVP equality modes:

```text
VALUE_EQUALITY
STRUCTURAL_EQUALITY
SYNTACTIC_EQUALITY
BEHAVIORAL_EQUALITY
PROOF_EQUALITY
LOSSY_GOAL_EQUALITY
```

`quote` normally changes equality to `SYNTACTIC_EQUALITY`.

`transport` preserves equality only if bridge declares it.

## 14. UniverseLevel

MVP universe model:

```text
U0 < U1 < U2 < ...
```

Rules:

```text
Set<U0> : U1
Set<U1> : U2
Class<U> is not Set<U>
```

No set may quantify over its own universe and construct itself in the same universe.

## 15. TheoryContext axis

TheoryContext is not merely a local field; it is the world in which the passport is interpreted.

```text
Passport is interpreted relative to TheoryContext.
```

Fields:

```text
home: TheoryId
valid_in: Set<TheoryId>
assumptions: Set<AxiomId>
bridge_trace: List<BridgeId>
```

## 16. TheoryBridge transfer

Bridge transfer functions must describe changes to:

```text
type
capabilities
cost
trust
equality
theory context
proof status
```

Quote example:

```text
PA.Nat -> Meta.Term<PA.Nat>
capabilities: remove ADD_AS_NAT, add INSPECT_AST
cost: usually SmallFinite/Compressed syntax representation
trust: join(value.trust, bridge.trust)
equality: SYNTACTIC_EQUALITY
theory.home: MetaArithmetic
bridge_trace += PA_quote
```

## 17. Safe versus trusted rules

MVP implements only core rules in Rust.

Future versions may allow:

```dlm
passport_rule safe ...
passport_rule trusted ...
```

But MVP does not support user-defined passport transfer rules.

## 18. Operation rule template

Every primitive operation defines:

```text
OperationSpec {
    name,
    argument required capabilities,
    effect requirements,
    result type rule,
    passport transfer rule,
    diagnostic rules
}
```

Example `add_nat`:

```text
requires: USE_RUNTIME or USE_STATIC_PROOF depending context
cap transfer:
  caps = preserve(lhs ∩ rhs, [SYMBOLIC_PRINT, COMPARE_BY_PROOF, COMPUTE_MODULAR])
  if both EXPAND and result below expansion bound: add PRINT_DECIMAL
cost = op_cost_add(lhs.cost, rhs.cost)
trust = join(lhs.trust, rhs.trust)
provenance = combine_provenance(lhs, rhs)
validation = min_validation(lhs, rhs)
theory = require_same_or_bridge(lhs.theory, rhs.theory)
```

## 19. Build mode constraints

Compilation mode defines maximum allowed trust:

```text
research:       max Unsafe
strict:         max Axiom, warn on Axiom
no-axioms:      max Builtin
trusted-only:   max Checked/Builtin depending config
allow-unsafe:   max Unsafe
```

## 20. Invariants

The checker must preserve these invariants:

```text
1. No value gains capability without justification.
2. No trust level becomes cleaner through computation.
3. Runtime input cannot become StaticProof without explicit bridge.
4. Object cannot cross TheoryContext without TheoryBridge.
5. Quote produces syntax, not truth.
6. Provability does not imply truth without SoundnessBridge.
7. Unsafe taint propagates transitively.
8. External Raw input cannot be used as mathematical value.
9. Infinity must have explicit mode.
10. Equality must have explicit or inferred EqualityMode.
```

## v0.9 HistoryChain axis

`Passport` now includes an append-only `HistoryChain`.

Purpose:

```text
current passport state + remembered transition history
```

MVP events are string labels such as:

```text
created:literal_nat
created:compressed_nat
builtin:busy_beaver_definable_noncomputable
runtime_input:read_nat
runtime_witness:require
static_proof:prove
bridge:quote:<BridgeName>
bridge:transport:<BridgeName>
bridge:soundness:<BridgeName>
axiom:soundness_assumption
unsafe:assumed_nat
equality:value
equality:syntax
equality:proof
```

Composition rules:

```text
unary transform:  history(source) + event
binary transform: merge(history(lhs), history(rhs)) + event
bridge transform: history(source) + bridge:<kind>:<name>
soundness bridge: history(source) + bridge:soundness:<name> + axiom:soundness_assumption
```

The MVP history axis is observational. It does not yet reject programs by itself. Future policy modes may reject based on history predicates.

## v0.10 distributed seed additions

New type roles:

```text
Node<arch>
Remote<T@arch>
```

New capabilities:

```text
can_host_runtime
can_accept_migration
can_migrate_out
can_serialize_for_migration
can_remote_symbolic_print
can_cross_arch_portable
```

New location axis:

```text
location=local
location=node<x86_64|aarch64>
location=remote<x86_64|aarch64>
```

Migration does not preserve all local capabilities. In MVP, `migrate(node, value)` produces a `Remote<T@arch>` with symbolic remote capabilities only. Direct local operations like `print_decimal(remote)` are rejected.


## v0.11 VirtualResourcePool

Added `VirtualCluster`, resource-aware node constructors, `virtual_pool(...)`, `pool_cores(...)` and `pool_memory_mib(...)`. This is the first MVP step toward the planned unified logical computer over many x86_64/aarch64 nodes while keeping node passports explicit. See `docs/VIRTUAL_RESOURCE_POOL.md`.


## v0.12 Scheduler seed

`schedule_on(pool, node, value)` returns a remote value:

```text
Remote<T@arch>
```

Static passport transfer:

```text
pool:  VirtualCluster + can_schedule_runtime
node:  Node<arch>     + can_accept_migration
value: T              + can_serialize_for_migration
```

Result capabilities:

```text
can_symbolic_print
can_remote_symbolic_print
```

Result history:

```text
merge(pool.history, node.history, value.history)
  -> cluster:schedule:<bridge>:to:<arch>
```

The checker does not yet prove that `node` is a member of `pool` statically.
Runtime checks membership in v0.12. A later version should introduce `ClusterMemberProof` or a dependent resource proof.

## v0.13 — DistributedMemory passport layer

New type:

```text
DistributedMemory<MiB>
```

New capabilities:

```text
can_allocate_distributed_memory
can_use_distributed_memory
can_checkpoint_memory
```

Rules:

```text
VirtualCluster + can_allocate_distributed_memory + positive memory_mib
  -> DistributedMemory<memory_mib>

DistributedMemory + can_use_distributed_memory
  -> memory_region_mib : Nat
```

A `DistributedMemory` region is not local RAM and is not a raw pointer. It is a passported logical memory allocation. Later versions may attach a consistency model and placement policy.


## v0.15 RemoteCheckpoint axis extension

`RemoteCheckpoint<T@arch>` records a checkpointed remote value. It has `can_restore_remote_checkpoint` but does not have local value capabilities. Restored and live-migrated values return as `Remote<T@target_arch>` and append `checkpoint:restore_remote` or `migration:live_remote` to HistoryChain.


## v0.16 Remote materialization

Remote values now expose `can_materialize_remote`. Materialization is explicit and produces a local value while preserving construction/cost/trust/provenance/validation and appending `remote:materialize:<bridge>` to HistoryChain. Cross-theory materialization requires `BridgeKind::Materialize`.


## v0.17 Portable Code Deploy

Introduces `PortableCode<T>` and the deployment path `compile_portable(value) -> deploy_on(pool, node, code) -> Remote<T@arch>`. The checker keeps architecture/location differences visible even when the programmer treats the cluster as one logical computer.

## v0.18 GPU memory axis

New resource types:

```text
GpuDevice<backend>
GpuPool
DistributedGpuMemory<memory_mib>
```

New capabilities:

```text
can_host_gpu_runtime
can_allocate_gpu_memory
can_use_gpu_memory
can_checkpoint_gpu_memory
can_launch_gpu_kernel
can_copy_cpu_to_gpu
can_copy_gpu_to_cpu
can_gpu_peer_transfer
can_gpu_unified_addressing
```

Rule:

```text
can_use_distributed_memory != can_use_gpu_memory
```

GPU memory is not implicitly usable as CPU distributed memory. Any future CPU↔GPU movement must be an explicit bridge/operation with its own cost and history event.

## v0.19 GPU transfer capabilities

GPU memory is not normal distributed RAM. A `DistributedGpuMemory` region receives:

```text
can_use_gpu_memory
can_checkpoint_gpu_memory
can_launch_gpu_kernel
can_copy_cpu_to_gpu
can_copy_gpu_to_cpu
can_gpu_peer_transfer
```

`copy_to_gpu(value, region)` requires `value.can_serialize_for_migration` and `region.can_copy_cpu_to_gpu`.
It returns `GpuValue<T>`, not `T`.

`copy_from_gpu(gpu_value)` requires `gpu_value.can_copy_gpu_to_cpu` and returns a local value with the capabilities appropriate for the inner type.


## v0.20 GPU kernel capabilities

New type and capability layer:

```text
GpuKernel<T>
can_compile_gpu_kernel
can_launch_gpu_kernel
```

`compile_gpu_kernel(value)` requires `can_compile_gpu_kernel` and `can_serialize_for_migration`.
`launch_kernel(region, kernel)` requires `DistributedGpuMemory` with `can_launch_gpu_kernel` and a `GpuKernel<T>` with `can_launch_gpu_kernel`.
The result is `GpuValue<T>`, not local `T`.


## v0.22 Universe Levels

Added the first mathematical universe hierarchy layer:

- explicit `U0()`, `U1()`, `U2()` constructors;
- `Set<U n -> U n+1>` as a level-raising object;
- `Class<U n>` as a meta-level view;
- `UniverseLevelError` for bare universes and set-of-all-sets style mistakes;
- `HistoryChain` events for universe, set and class formation.

See `UNIVERSE_HIERARCHY.md`.

## v0.23 Definability Axis

Definability is not a boolean property. In DLM/ЯРД it is a passport object:

```text
DefinableNat<language, encoding, object_theory, bound, meta_level>
```

This prevents Berry-style expressions from smuggling a metalanguage phrase into
an object-level definition. The following are rejected by design:

```text
berry_number(k)
smallest_undefinable(k)
undefinable_nat(k)
definable_nat(k)
```

Required capabilities:

```text
Language      -> can_define_in_language
Encoding      -> can_use_encoding
MetaLevel     -> can_meta_level_reason
DefinableNat  -> can_definability_reason
```

## v0.28 Infinity Arithmetic Extension

The infinity axis is now used in six explicit modes:

```text
Infinity<cardinal>
Infinity<ordinal>
Infinity<limit>
Infinity<potential>
Infinity<class>
Infinity<universe>
```

Mode-preserving operations are enforced by the checker:

```text
cardinal_add : Infinity<cardinal> × Infinity<cardinal> -> Infinity<cardinal>
ordinal_add  : Infinity<ordinal>  × Infinity<ordinal>  -> Infinity<ordinal>
potential_step : Infinity<potential> -> Infinity<potential>
```

`class_infinity(...)` requires `Class<U n>` and `universe_infinity(...)` requires
`Universe<U n>`. This keeps proper-class and universe-level infinity from being
created by a bare or self-referential `infinity()` form.
