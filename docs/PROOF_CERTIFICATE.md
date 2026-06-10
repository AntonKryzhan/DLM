# Proof Certificate Foundation

`v0.41.0` adds a typed proof-certificate layer on top of the existing theorem, proof-context and tactic-script foundations.

This patch does **not** add new `.dlm` syntax and does not route the legacy checker through ProofIR. It only introduces internal data structures and regression tests for certifying already-closed proof contexts.

## New module

```text
crates/dlm_core/src/certificate.rs
```

## Core types

```text
ProofCertificate
ProofCertificateStatus
```

A `ProofCertificate` records:

```text
theory
theorem_name
proposition
status
trust
provenance
fingerprint
trace_len
trace
```

The certificate is intentionally separate from `Theorem`, `StaticProof` and `ProofClosure`:

```text
Theorem<name:P> != StaticProof<P>
ProofClosure != ProofCertificate
ProofCertificate != new proof evidence
```

A certificate is an audit artifact over an already accepted closure.

## Certificate status

```text
Checked
AxiomAdmitted
```

`Checked` means the theorem was closed through a matching static proof. `AxiomAdmitted` means the theorem was closed by explicit axiom admission and must remain visibly tainted.

## API

```rust
certificate_from_closure(...)
certificate_from_tactic_report(...)
verify_certificate_against_theorem(...)
compute_certificate_fingerprint(...)
```

## Main invariant

```text
closed ProofClosure<Theorem<name:P>> => ProofCertificate<name:P>
```

Certificates can only be emitted from closed proof closures. Open obligations are rejected.

## Verification invariant

```text
certificate.theory == theorem.theory
certificate.theorem_name == theorem.name
certificate.proposition == theorem.proposition
certificate.trust == theorem.trust
certificate.provenance == theorem.provenance
certificate.fingerprint == fingerprint(certificate contents)
```

The fingerprint is deterministic and intentionally local. It is not a cryptographic security promise; it is a stable internal audit checksum for regression testing and later certificate serialization work.

## Axiom taint

A certificate emitted from `AdmitAxiom` has:

```text
status=AxiomAdmitted
trust >= Axiom
```

This prevents axiom-based proof closure from being silently represented as a clean checked proof.

## Tests

```powershell
cargo test -p dlm_core --test proof_certificate
```

Protected behavior:

```text
static proof closure emits a stable certificate;
open tactic reports cannot be certified;
axiom admission remains visibly tainted;
certificate verification rejects theorem identity mismatch;
fingerprint changes when proof trace changes;
fingerprint validation catches tampering.
```
