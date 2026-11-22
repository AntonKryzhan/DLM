# Module / Import System Foundation (v0.46)

This layer introduces a small, explicit model for project-level module manifests and import graphs.
It is intentionally not wired into `.dlm` syntax yet.

## New concepts

```text
ModuleManifest<module>
ImportGraph<root=module>
ModuleExport<module.symbol:public|private>
```

A module manifest contains:

```text
module name
imports
exports
```

An import graph contains:

```text
root module
module manifests
resolved import edges
```

## Main laws

```text
private export must not cross module boundary
```

```text
import graph must be acyclic
```

```text
module manifest is not theorem/proof/truth
```

The system must not treat module visibility as a proof rule. Public exports only control name visibility; they do not add trust.

## Trust rule

Exporting a passport through a module boundary preserves the source trust:

```text
ModuleExport(source) trust >= source.trust
```

No export operation may turn:

```text
Axiom / Oracle / Unsafe
```

into:

```text
Checked
```

## MVP boundary

This version does not add parser syntax such as:

```text
import Math.Nat
export theorem plus_zero
```

It only adds the semantic core structures needed before project-level checking can be connected to the parser and CLI.
