# Strategic Runtime Domains — Containers, Web3/Crypto and Next-Generation Databases

This document expands the long-term roadmap directions for DLM / ЯРД after the module/import foundations.

The common theme is explicit capability, trust and provenance control across runtime domains.
DLM should not treat Docker, Web3 or databases as opaque I/O side effects. They should become passport-visible domains.

## 1. Container / Docker Direction

A container is modeled as an execution location with restricted capabilities.

Planned objects:

```text
ContainerPolicy
ContainerManifest
ContainerImageDigest
MountPolicy
NetworkPolicy
SecretPolicy
ResourceBudget
ContainerAuditReport
```

Key invariants:

```text
network=none forbids NetworkCapability.
SecretCapability is non-printable and non-serializable by default.
Unsafe host mounts taint the execution audit.
Container image digest participates in provenance.
RuntimeWitness from logs is not StaticProof.
```

## 2. Web3 / Crypto Direction

DLM should become a safety layer for transaction construction, signing boundaries, custody policies and trust-visible chain data.

Planned objects:

```text
ChainId
NetworkKind
Address<chain, network>
Amount<unit>
UnsignedTransaction
CheckedTransactionIntent
SignedTransaction
BroadcastReadyTransaction
PrivateKey<non_printable, non_serializable>
RpcSource<trust>
OracleSource<trust>
ChainProof
BridgeProof
```

Bitcoin direction:

```text
BitcoinTx
BitcoinInput
BitcoinOutput
BitcoinOutPoint
UTXO
SatoshiAmount
ScriptPubKey
WitnessStack
BlockHeader
MerkleProof
```

TON direction:

```text
TONCell
BOC
TONAddress
TONMessage
TONContractState
TONStorageProof
NanoTonAmount
TLBSchema
```

Key invariants:

```text
PrivateKey cannot be printed or migrated by default.
mainnet address is not testnet address.
float amount is not crypto amount.
parsed transaction is not checked intent.
unsigned transaction is not signed transaction.
RPC result is not chain proof.
Oracle input preserves trust taint.
```

## 3. Next-Generation Database Direction

DLM is a strong fit for correctness-first database layers: transaction boundaries, storage proofs, WAL/checkpoint ordering, query plan audit, schema migration safety and verifiable results.

Planned objects:

```text
DbRecord<schema>
DbKey<namespace>
DbPage<layout>
WalRecord
Checkpoint
Snapshot<txid>
MvccVersion<txid>
TxnIntent
TxnReadSet
TxnWriteSet
TxnCommitProof
QueryPlan
IndexPlan
ReplicationQuorum
ConsensusLogEntry
MerkleStateRoot
VerifiableQueryResult
```

Key invariants:

```text
uncommitted write is not committed record.
runtime read is not snapshot proof.
query result is not verifiable query result.
WAL append precedes durable commit.
checkpoint references a validated WAL prefix.
snapshot read declares txid/version boundary.
index entry derives from the same committed record version.
replica quorum is explicit before distributed commit.
schema migration preserves typed compatibility or exposes taint.
```

## 4. Combined Deployment Direction

Long-term deployable DLM programs should combine all runtime-domain audits:

```text
ContainerPolicy
  + CryptoCustodyPolicy
  + DatabaseTransactionPolicy
  + ModuleInterfaceAudit
  + ProofCertificateAudit
  => DeployableAuditedProgram
```

The target property:

```text
The deployed program exposes runtime capabilities, data trust boundaries,
secret handling, storage transitions and external dependencies in its passport/audit chain.
```

## 5. Non-goals for early implementation

```text
Do not implement custom cryptography before the passport/policy layer is ready.
Do not claim production wallet safety without secret isolation and audited signing.
Do not build a full storage engine before WAL/snapshot/commit invariants exist.
Do not let runtime logs become static proofs.
```
