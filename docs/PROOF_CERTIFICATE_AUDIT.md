# Proof Certificate Audit / Export Foundation

`v0.42.0` adds a deterministic audit/export layer for proof certificates.

This layer is intentionally textual and dependency-free. It does not introduce new `.dlm` syntax and it does not make a certificate into a new proof object.

## Core rule

```text
ProofCertificate<name:P> + Theorem<name:P> + stable fingerprint => Verified audit report
```

A certificate export is a canonical, line-oriented representation:

```text
DLM-PROOF-CERTIFICATE v1
theory: Meta
theorem: TrueIntro
proposition: kernel_checked:true_intro
status: Checked
trust: Checked
provenance: InternalDerived
fingerprint: dlm-cert-v1-...
axiom_tainted: false
trace_len: 3
trace:
  0: open:kernel_checked:true_intro
  1: exact:kernel_checked:true_intro
  2: close:TrueIntro:kernel_checked:true_intro
```

The audit report is also canonical:

```text
DLM-PROOF-CERTIFICATE-AUDIT v1
status: Verified
theory: Meta
theorem: TrueIntro
proposition: kernel_checked:true_intro
fingerprint: dlm-cert-v1-...
axiom_tainted: false
trace_len: 3
diagnostics: []
```

## Invariants

- Export requires `trace_len == trace.len()`.
- Export requires a stable fingerprint.
- Audit verifies theorem identity, theory, proposition, trust and provenance.
- Tampered traces fail audit.
- Axiom-admitted certificates remain visibly axiom-tainted.
- `export_certificate_text_unchecked` is only for forensic rendering and does not imply validity.
