# DLM/ЯРД v0.23 — Definability Passport

## Purpose

v0.23 adds the first explicit model of definability. The goal is to prevent
Berry-style ambiguity where a phrase such as "the smallest number not definable
in under N words" silently mixes object language, metalanguage, encoding and
resource bounds.

The new rule is:

```text
There is no bare definability.
Every definability claim must carry language, encoding, object theory, bound and meta-level.
```

## New Types

```text
Language<L0>
Encoding<Godel>
MetaLevel<Mk>
DefinableNat<language, encoding, object_theory, bound, meta_level>
```

A `DefinableNat` is not treated as an ordinary exact `Nat`. It is a definability
object with symbolic/proof-level capabilities. It can expose its definability
metadata, but it does not automatically get `can_print_decimal`.

## New Builtins

```dlm
language_L0()
encoding_godel()
meta_level(k)

definable_nat(language, encoding, bound, meta_level)
definability_bound(definable_nat)
definability_meta_level(definable_nat)
```

Example:

```dlm
theory Meta {
    let lang = language_L0()
    let enc = encoding_godel()
    let meta = meta_level(1)
    let d = definable_nat(lang, enc, 20, meta)

    print_symbolic(d)
    print_decimal(definability_bound(d))
    print_decimal(definability_meta_level(d))
}
```

Runtime output:

```text
definable_nat<L0,Godel,Meta,bound=20,M1>
20
1
```

## Rejected Berry-style forms

These are intentionally rejected:

```dlm
berry_number(20)
smallest_undefinable(20)
undefinable_nat(20)
definable_nat(20)
```

The diagnostic is `DefinabilityError[E0902]`.

## Passport Capabilities

New capabilities:

```text
can_define_in_language
can_use_encoding
can_meta_level_reason
can_definability_reason
can_extract_definability_bound
can_extract_definability_meta
```

## HistoryChain Events

```text
definability:language:L0
definability:encoding:Godel
definability:meta_level:M1
definability:definable_nat:L0:Godel:bound:20:M1
definability:bound
definability:meta_level
```

## MVP Limitations

v0.23 does not yet implement a full formal language registry or Gödel encoder.
`L0` and `Godel` are builtin symbolic descriptors. This is enough to enforce
the central safety law: definability is always relative and never bare.
