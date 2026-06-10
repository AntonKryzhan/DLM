# DLM/ЯРД v0.27 — Bridge Soundness Classification

This document fixes the first formal bridge taxonomy used by `dlm explain`.

A bridge is not a generic cast. Every bridge must declare what kind of preservation it claims. The checker may use bridge declarations to allow specific operations, but the soundness layer reports the exact metatheoretic role of each bridge.

## Core law

```text
A value may cross a theory boundary only through an explicit bridge.
A bridge must say what it preserves: syntax, value, proof, truth, or only runtime location.
Truth is never transported implicitly.
```

## Bridge kinds

| Kind | Syntax | Value | Proof | Truth | Taint | Meaning |
|---|---:|---:|---:|---:|---|---|
| `definitional` | yes | yes | yes | yes | Builtin | definition-only conservative extension |
| `conservative` | no | yes | yes | yes | Builtin | conservative extension for old-language facts |
| `quote` | yes | no | no | no | Builtin | object becomes syntax `Term<T>` |
| `transport` | no | yes | no | no | Builtin | value is moved to another theory context |
| `soundness` | no | no | yes | yes | Axiom | proof/provability is lifted toward truth |
| `reflection` | yes | no | yes | no | Axiom | reflective metatheory; never implicit |
| `migration` | no | yes | no | no | Builtin | runtime location/architecture movement |
| `materialize` | no | yes | no | no | Builtin | remote value re-enters local value space |
| `unsafe` | no | maybe | no | no | Unsafe | unsafe bridge; no safe preservation law |
| unknown | no | no | no | no | Unsafe | unsupported bridge kind |

## Soundness bridge law

`bridge kind = soundness` is intentionally Axiom-tainted. It is the explicit gate from a source-theory proof/provability artifact toward a target-theory truth-level proof.

```text
PA.Proof(phi) --soundness--> Meta.StaticProof(phi)
```

This move is not free. It records:

```text
bridge:soundness:<name>
axiom:soundness_assumption
```

and the resulting value has `TrustLevel::Axiom` unless the source is already more tainted.

## Quote bridge law

`quote` preserves syntax only:

```text
PA.Nat --quote--> Meta.Term<PA.Nat>
```

The result supports `can_inspect_ast` and `can_compare_syntax`, but not `can_add_as_nat`, `can_print_decimal`, or truth-level proof operations.

## Transport bridge law

`transport` moves a value role between theory contexts. It does not imply soundness, reflection, truth preservation, or proof preservation.

## `dlm explain`

`dlm explain` now reports:

- bridge declaration counts;
- a profile for every bridge;
- preservation flags;
- conservative/reflective/reversible flags;
- required taint level;
- invariant issues for unsafe or unknown bridge kinds.

Example:

```text
PA_quote: PA -> Meta kind=quote
  preserves=[syntax:true, value:false, proof:false, truth:false]
  role: syntax-only bridge: object becomes Term; value/proof/truth are not transported
```
