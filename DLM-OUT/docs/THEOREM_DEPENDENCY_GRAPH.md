# Theorem Dependency Graph / Global Metatheory Inventory

`v0.51.0` adds a global metatheory inventory layer above the existing axiom/dependency, closure, module-interface and conservative-extension audits.

The purpose is not to prove new theorems. The purpose is to make the dependency basis of already-known theorem/audit objects explicit, ordered, fingerprinted and taint-preserving.

## Core law

```text
TheoremDependencyNode[]
+ TheoremDependencyEdge[]
+ ConservativeExtensionAudit[]
=> GlobalMetatheoryInventoryReport
```

## Statuses

```text
Verified
Open
Rejected
```

A report is `Verified` only when:

- all nodes have unique semantic ids;
- all node fingerprints are unique;
- all edges point to explicit nodes;
- no edge is a self-edge;
- conservative-extension evidence is verified;
- no diagnostics are produced;
- no open closure/audit evidence remains.

A report is `Open` when the evidence is well-formed but contains open closure or open conservative-extension evidence.

A report is `Rejected` when the graph is malformed, has duplicate evidence, points to hidden dependencies, or includes rejected conservative-extension evidence.

## Node roles

Supported node roles include:

```text
Theorem
DependencyAudit
ClosureReport
ConservativeExtensionAudit
ModuleInterface
ModuleImportAudit
ProofCertificate
RewriteCertificate
Unknown
```

The role must match the passport type. For example:

```text
TheoremDependencyNodeKind::Theorem
```

requires:

```text
TypeKind::Theorem { ... }
```

A `Statement`, `Goal`, `ProofTerm`, `RuntimeWitness` or display-only object cannot be silently inserted as theorem evidence.

## Taint preservation

The inventory report preserves and exposes:

```text
max_trust
has_axiom_taint
has_oracle_taint
has_unsafe_taint
```

The inventory passport is never allowed to hide `Axiom`, `Oracle` or `Unsafe` taint.

## Passport object

`global_metatheory_inventory_passport(...)` produces:

```text
GlobalMetatheoryInventory<subject:status>
```

This object is an audit/report object, not a theorem, proof, truth claim or proof certificate.

## Why this exists

Before a full proof kernel and standard mathematical library can be trusted, DLM needs a global inventory of the foundation:

```text
which theorems exist;
which closure reports support them;
which dependency audits support the closures;
which conservative extensions preserve old theorems;
which taint remains visible;
which graph edges connect the evidence.
```

This closes another part of the metamathematical foundation before moving to ordinary mathematics of the language.
