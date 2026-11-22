# Strategic Application Domains

This document expands the roadmap with the areas where DLM/YARD is expected to be especially effective.

The main claim is not that every program should be written in DLM.
The main claim is that DLM is strongest when a program has expensive or dangerous state transitions, strict trust boundaries, sensitive secrets, long-lived audit obligations or formal correctness requirements.

## Mathematical Pattern

```text
critical state transition
+ passport lattice
+ explicit capability gates
+ provenance/history chain
+ trust semilattice
+ proof/audit certificate
=> safer executable or verifiable system layer
```

DLM is therefore optimized for programs where the following distinctions are essential:

```text
Parsed != Verified
RuntimeWitness != StaticProof
Unsigned != Signed
Pending != Committed
Authorized != Settled
Private != Printable
External != Trusted
Testnet != Mainnet
Amount<float> != Amount<minor_unit>
QueryResult != VerifiableQueryResult
```

## High-Value Domains

### Banking and Finance

Effective for:

```text
ledger correctness;
payment workflows;
settlement and reconciliation;
limit/risk-policy engines;
KYC/AML state machines;
dual-control approval;
card-data safety;
audit-ready financial reports.
```

Key laws:

```text
sum(debits) == sum(credits)
PaymentIntent != PaymentSettled
Authorization != Settlement
Reversal != Deletion
AvailableBalance != LedgerBalance
CardPAN<raw> must not be printable/loggable
```

### Databases and Verifiable Storage

Effective for:

```text
ledger databases;
MVCC boundary checks;
WAL/checkpoint verification;
query-plan audit;
replication/quorum validation;
Merkle/verifiable result services;
AI-agent memory stores with provenance.
```

Key laws:

```text
UncommittedWrite != CommittedRecord
RuntimeRead != SnapshotProof
QueryResult != VerifiableQueryResult
Checkpoint references validated WAL prefix
SchemaMigration preserves compatibility or exposes taint
```

### Web3, Crypto Custody and Blockchain Infrastructure

Effective for:

```text
wallet backends;
transaction builders;
chain indexers;
bridge/oracle audit;
custody policy;
UTXO/cell proof validation;
smart-contract call policy wrappers.
```

Key laws:

```text
PrivateKey must never be printable or generally serializable
UnsignedTransaction != SignedTransaction != BroadcastReadyTransaction
Address<mainnet> != Address<testnet>
Amount<satoshi> != Amount<nanoTON> != Amount<float>
RuntimeRpcBalance != ChainProof
```

### Containers, Cloud and Zero-Trust Runtime

Effective for:

```text
container policy manifests;
zero-trust workers;
serverless execution contracts;
GPU/remote execution audit;
secret-handling policy;
supply-chain provenance;
deployable audit reports.
```

Key laws:

```text
NetworkCapability required before external RPC
HostMount taints execution unless explicitly approved
SecretCapability must not imply print or migration serialization
ImageDigest is part of runtime provenance
ContainerLog is RuntimeWitness, not StaticProof
```

### AI Agents and Memory Systems

Effective for:

```text
AI-agent memory stores;
tool-use policy engines;
retrieval provenance;
audited planning pipelines;
action approval gates;
model-output trust classification.
```

Key laws:

```text
ModelOutput != VerifiedFact
ToolObservation != StaticProof
MemoryEntry preserves source, freshness and trust
AgentPlan != AuthorizedAction
High-risk tool call requires explicit approval policy
```

### Cybersecurity and Compliance

Effective for:

```text
RBAC/ABAC engines;
zero-trust access;
security-control validation;
incident evidence chains;
secret management;
secure build/deploy gates;
supply-chain attestation;
regulatory compliance checks.
```

Key laws:

```text
Authenticated != Authorized
Role != Permission
DebugToken != ProductionSecret
OperatorA cannot approve an operation created by OperatorA when dual control is required
```

### Scientific and Engineering Computing

Effective for:

```text
reproducible lab pipelines;
numerical experiment manifests;
HPC job audit;
simulation checkpoint validation;
dataset provenance;
parameter sweep certification.
```

Key laws:

```text
RawMeasurement != CalibratedMeasurement
SimulationOutput != VerifiedResult
RuntimeExperimentLog != StaticProof
DatasetVersion is part of provenance
```

### Industrial, Robotics and IoT

Effective for:

```text
industrial control policy;
robot safety gates;
IoT firmware manifests;
remote-device command validation;
sensor trust pipelines;
actuator command approval.
```

Key laws:

```text
SensorReading != VerifiedSensorReading
SimulationCommand != HardwareCommand
RemoteCommand requires authenticated source and explicit device capability
FirmwareImage carries digest, provenance and target-device compatibility
```

### Compilers, Build Systems and Proof-Carrying Software

Effective for:

```text
proof-carrying builds;
compiler pass audit;
portable-code deployment;
module-interface compatibility;
package provenance;
semantic version migration;
trusted plugin systems.
```

Key laws:

```text
Syntax != Value
ParsedAST != ResolvedAST
RuntimeTestPass != StaticProof
Desugaring must not add trust
Plugin import requires public interface and trust audit
```

### Legal, Governance and Rule-Based Decision Systems

Effective for:

```text
contract-rule engines;
policy decision records;
approval workflows;
regulatory rule checkers;
versioned policy migration.
```

Key laws:

```text
PolicyDraft != ActivePolicy
HumanException != GeneralRule
ExternalDocument != VerifiedAuthority
Decision references policy version and evidence chain
```

### Education, Formal Reasoning and Knowledge Systems

Effective for:

```text
proof notebooks;
theorem/exercise checkers;
formal education tools;
knowledge-base audit;
mathematical dependency graphs.
```

Key laws:

```text
Statement != Theorem
ProofTerm != StaticProof
Provable<P> != Truth<P>
Consistency<T> != Proof<Consistency<T>>
Axiom-admitted result remains visibly tainted
```

## Priority Order

```text
1. Financial / Banking Safety Track
2. Database / Verifiable Storage Track
3. Container / Zero-Trust Runtime Track
4. Web3 / Crypto Custody Track
5. AI-Agent Memory / Tool-Use Track
6. Compliance / Access-Control Track
7. Scientific Reproducibility Track
8. Industrial / IoT Safety Track
```

## Shared Implementation Foundations

The same core objects support multiple strategic domains:

```text
Amount / Unit Safety
Secret NonPrintable / NonSerializable
Transaction State Machine
Policy Approval Proof
AuditTrail / EvidenceChain
VerifiableResult / Report Fingerprint
Container / Runtime Capability Passport
Database Snapshot / Commit Boundary
```
