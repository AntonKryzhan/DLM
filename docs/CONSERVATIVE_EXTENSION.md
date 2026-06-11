# Conservative Extension Audit

`v0.50.0` adds the first conservative-extension audit layer for DLM metatheory.

The goal is to make extension safety explicit:

```text
base metatheory closure
+ extension metatheory closure
+ preserved old theorem evidence
+ visible new assumptions
=> conservative extension audit report
```

This is still part of the **metamathematical foundation** track. It does not add new `.dlm` syntax and does not change runtime execution.

## Core law

```text
Closed(base) + Closed(extension) + PreservedTheorem[] => ConservativeExtensionAudit<verified>
```

If the extension closure is open, the conservative-extension audit remains open. If the base closure is not closed, the audit is rejected.

## What counts as preserved theorem evidence

A preserved theorem witness requires exact theorem identity:

```text
Theorem<name:P> in base
Theorem<name:P> in extension
```

The audit rejects:

```text
Theorem<old_name:P> -> Theorem<new_name:P>
Theorem<name:P> -> Theorem<name:Q>
Statement<P>       -> Theorem<name:P>
RuntimeWitness<P>  -> Theorem<name:P>
```

The point is not to prove all old theorems automatically yet. The point is to create a stable audit object that can later be fed by a real theorem dependency graph and proof kernel.

## New assumptions remain visible

A conservative extension may introduce new axioms, bridge assumptions, oracle assumptions, or unsafe external assumptions, but the audit must not hide them.

The report records:

```text
max_trust
has_axiom_taint
has_oracle_taint
has_unsafe_taint
new_assumptions[]
```

This preserves the existing invariant:

```text
Axiom / Oracle / Unsafe taint is monotonic and never hidden by reports.
```

## Status values

```text
verified
open
rejected
```

`verified` means the base is closed, the extension is closed, and at least one old theorem preservation witness is present.

`open` means the extension closure is still open, but no hard rejection was found.

`rejected` means the audit found a hard violation such as a non-closed base, rejected extension closure, empty preservation evidence, duplicate theorem witnesses, or invalid theorem preservation evidence.

## Passport type

The new passport type is:

```text
ConservativeExtensionAudit<base->extension:status>
```

It is an audit contract, not a theorem, proof, truth claim, or proof certificate.

## Why this belongs before ordinary math

Before adding richer mathematical objects, DLM must be able to answer:

```text
Did this extension preserve what the previous metatheory already proved?
Which old theorems are claimed to survive?
Which new assumptions were introduced?
Did any axiom/oracle/unsafe taint enter the extension?
```

This patch creates that audit surface.
