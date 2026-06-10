# DLM Rewrite Normalization Foundation

Version: v0.44.0

This layer builds a bounded normalization/audit surface on top of the v0.43 equality rewrite core.

It does not add `.dlm` syntax and does not change checker or runtime semantics.

## Core rule

```text
ordered RewriteRule[] + term + max_steps => RewriteNormalizationReport
```

The normalization engine repeatedly applies the first forward rewrite rule whose left-hand side exactly matches the current term. If no rule matches, the term is considered normal.

## Added artifacts

```text
RewriteNormalizationStatus
RewriteNormalizationReport
normalize_with_rewrite_rules(...)
audit_rewrite_normalization_report(...)
export_rewrite_normalization_report(...)
export_rewrite_normalization_report_unchecked(...)
```

## Protected invariants

```text
Bool != EqProof<A,B>
EqProof<A,B> must become RewriteRule before use
RewriteRule[] normalization is ordered
normalization is bounded by max_steps
cyclic rewrite systems cannot silently run forever
RewriteCertificate endpoints must match the normalization report
Axiom/Oracle/Unsafe taint cannot be cleaned by normalization
```

## Audit boundary

A `RewriteNormalizationReport` is accepted only if:

```text
report.input == report.trace.from
report.normal_form == report.trace.to
report.trace.steps.len() <= report.max_steps
report.certificate == RewriteCertificate<input, normal_form>
report.certificate.trust == report.trace.trust
report.certificate.provenance == report.trace.provenance
```

Unchecked export exists only for forensic rendering of malformed or tampered reports.

## Test command

```powershell
cargo test -p dlm_core --test rewrite_normalization
```
