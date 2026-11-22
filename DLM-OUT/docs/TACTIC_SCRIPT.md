# Tactic Script Foundation

`v0.40.0` adds the first internal tactic-script layer above `ProofContext`.

This is intentionally not a public `.dlm` syntax feature yet. It is a typed Rust-side foundation for the later proof/tactic checker split.

## Core objects

```rust
TacticScript
TacticScriptStep
TacticStepIndex
TacticCommand
TacticScriptReport
TacticScriptStatus
execute_tactic_script(...)
```

## Supported commands

```text
Assume<P>
ExactStaticProof<TheoremName, Statement<P>, StaticProof<P>>
AdmitAxiom<TheoremName, Statement<P>, Reason>
```

## Main invariant

```text
closing tactic must be final
```

A tactic script may add assumptions and keep a goal open. It may close a goal with a static proof or explicit axiom admission. Once a closing command appears, no further tactic command is allowed in the same script.

## Soundness boundary

`ExactStaticProof` delegates to the existing proof-context rule:

```text
Goal<P> + Statement<P> + StaticProof<P> => Theorem<name:P>
```

Therefore a script cannot close a theorem with a raw `ProofTerm`, a `RuntimeWitness`, a mismatched statement, or a proof of another proposition.

`AdmitAxiom` remains visibly axiom-tainted and records an axiom theorem event in the passport history.

## Diagnostics

`v0.40.0` adds:

```text
TacticScriptError [E0911]
```

This diagnostic is currently used for tactic-script structural errors, such as a closing tactic not being the final command.

Proof obligation failures still use `ProofObligationError [E0910]`, preserving the existing distinction between proof-content mismatch and tactic-script shape.

## Test command

```powershell
cargo test -p dlm_core --test tactic_script
```
