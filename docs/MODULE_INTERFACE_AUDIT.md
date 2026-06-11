# Module Interface / Import Audit Foundation (v0.47)

This layer builds on `v0.46` module manifests and import graphs. It adds stable module interface artifacts and explicit import-audit reports.

It is intentionally not wired into `.dlm` syntax yet.

## New concepts

```text
ModuleInterface<module>
ModuleImportAudit<importer->provider:verified|rejected>
InterfaceSymbol<symbol, visibility, type, trust, capabilities>
```

A module interface is a deterministic contract extracted from exported passports. It contains public and private entries, but private entries are audit data only.

## Main laws

```text
public export visibility is not proof evidence
```

```text
private interface entries must not satisfy imports
```

```text
import audit requires an explicit import edge
```

```text
interface fingerprint changes when exported evidence changes
```

## Trust rule

A module interface preserves the maximum taint of its exported symbols. It never lowers:

```text
Axiom / Oracle / Unsafe
```

into:

```text
Checked
```

The import audit report is an audit artifact. It does not become a theorem or a proof.

## MVP boundary

This version does not add parser syntax such as:

```text
interface Math.Nat
use Math.Nat.zero
```

It only adds the semantic core needed before project-level module checking can be connected to parser and CLI passes.
