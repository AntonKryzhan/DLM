# Numeric conversions (N11) and portable numeric texts (N12), 0.105.0

Host: `math::convert`, `math::portable`. Source: `lib/std/convert.dlm` (`std.convert`, theory `StdConvert`).
There are no implicit conversions between families; every lossy step names a rounding mode.

## Conversions

Modes: `floor` (0), `ceil` (1), `trunc` (2, toward zero), `half_even` (3).

| Host | Source | Result / refusal |
|---|---|---|
| `round_rational(q, mode)` | `cv_round(b, q, mode)` | integer; `unknown rounding mode` |
| `to_integer_exact(q)` | `cv_integer_exact(b, q)` | `rational is not an integer` |
| `to_dyadic(q, p, mode)` | `cv_dyadic(b, q, p, mode)` | `round(q 2^p) / 2^p` |
| `to_decimal(q, d, mode)` | `cv_decimal(b, q, d, mode)` | fixed `[-]I.F` with exactly `d` digits (no `-0.00`); `decimal digit count exceeds 4096` |
| `to_decimal_exact(q)` | `cv_decimal_exact(b, q)` | shortest exact text; `rational is not a finite decimal` |
| `parse_decimal(t)` | `cv_parse_decimal(b, t)` | exact rational of `[-]D+[.D+][e[-]D{1,5}]`; `invalid decimal number`, `decimal exponent out of range` (\|e\| > 4096), `UnknownWithinBudget: decimal input exceeded` (length > 2·bits + 16) |
| `to_u128`, `to_i128` | `cv_to_nat`, `cv_to_int` | `integer does not fit in Nat` / `Int` |
| `creal_round(x, mode, max_n)` | `cv_creal_round(b, x, mode, max_n)` | decided when both enclosure bounds round alike |
| `creal_decimal(x, d, mode, max_n)` | `cv_creal_decimal(b, x, d, mode, max_n)` | same, for decimal text |

Computable reals are tried at precisions `0, 8, 16, 32, … < max_n, max_n` (`max_n` ≤ 4096); if no enclosure
decides, the answer is `UnknownWithinBudget: rounding undecided at precision max_n`, never a guess (e.g. `floor`
of `sqrt 2 - sqrt 2`).

## Portable format `dlm1`

`dlm1:<family>:<payload>`, payload = the canonical text of the family:

| family | payload | example |
|---|---|---|
| `int` | integer | `dlm1:int:-42` |
| `rat` | `n/d` reduced, `d` ≥ 1 | `dlm1:rat:-3/4` |
| `ivq` | `[lo,hi]` | `dlm1:ivq:[-1/2,3/4]` |
| `cq` | `re;im` | `dlm1:cq:1/2;-3/1` |
| `creal` | creal program text | `dlm1:creal:c 2/1 ; sqrt 0` |
| `ccomplex` | `re \| im` | `dlm1:ccomplex:c 0/1 \| c 1/1` |

`parse` accepts only the exact output of `format` (round trip `format(parse(t)) = t`). Refusals:
`invalid portable number`, `unsupported portable number version` (`dlm<digits>` other than `dlm1`),
`unknown portable number family`, `non-canonical portable number`; `parse_as(t, family)` also refuses a
different family. Source: `pn_int`, `pn_rat`, `pn_ivq`, `pn_cq`, `pn_creal`, `pn_ccomplex`, `pn_check`, `pn_category`.

## Evidence

`tools/generate_convert105_oracle.py` (independent `Fraction` code; creal rows checked against mpmath) →
`tests/fixtures/convert105.tsv`: 1876 host rows, 234 also run as DLM in the interpreter and native C
(`tests/convert.rs`, 4 tests); `examples/valid/convert_demo.dlm` (`cli_creal_ownership.rs`). ASan/UBSan/LSan
and MinGW-w64 + Wine on the 234-row program, the limits program and the example: identical output, no reports.
