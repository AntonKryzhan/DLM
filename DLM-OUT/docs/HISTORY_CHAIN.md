# DLM/ЯРД Passport HistoryChain v0.9

`HistoryChain` is the first MVP mechanism for preserving the past of a value.

A passport previously described the current state of a value:

```text
Type + Construction + Capabilities + Cost + Trust + Provenance + Validation + Theory
```

`v0.9` adds:

```text
HistoryChain
```

The chain answers a different question:

```text
How did this value become what it is now?
```

## Core rule

A value may regain or preserve local capabilities, but it must not forget important past transitions.

Examples:

```text
created:literal_nat
bridge:quote:PA_quote
bridge:soundness:PA_soundness
axiom:soundness_assumption
runtime_input:read_nat
runtime_witness:require
unsafe:assumed_nat
```

## Why this matters

Without history, a later operation could make a value look locally safe while hiding that it previously crossed an unsafe, axiom, oracle or migration boundary.

`HistoryChain` makes this visible and prepares these future features:

- `MigrationBridge`;
- `Proof Expiry` / `Epoch`;
- `ReflectionBridge`;
- `MutationBridge`;
- distributed proof services;
- node-aware trust policy.

## MVP representation

In `v0.9`, `HistoryChain` is intentionally string-based:

```rust
pub struct HistoryChain {
    events: Vec<String>,
}
```

This keeps implementation small and readable. Later versions can replace it with typed events:

```rust
enum PassportEvent {
    CreatedLiteral,
    DerivedAdd,
    QuoteBridge(BridgeId),
    TransportBridge(BridgeId),
    SoundnessBridge(BridgeId),
    RuntimeInput(SourceId),
    RuntimeWitness(PredicateId),
    AxiomUsed(AxiomId),
    UnsafeUsed(UnsafeSiteId),
    Migration(NodeId, NodeId),
    Mutation(MutationId),
}
```

## MVP policy

`HistoryChain` does not yet reject code by itself. It is observability and future-policy infrastructure.

Trust rejection is still performed by `TrustLevel` and `CheckPolicy`.

Later policy modes may reject based on history, for example:

```text
--reject-history unsafe
--reject-history axiom
--reject-history migration:untrusted_node
```
