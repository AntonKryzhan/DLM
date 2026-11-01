# Computable reals (0.102.0)

This closes numeric criterion N08 within the section «numeric foundations and exact arithmetic»: a formal grammar
of representations of real numbers, rational inclusions at a requested precision, arithmetic and square roots
with checkable error, and honest undecided comparison. It builds on the closed rational intervals of N07
(EXACT_INTERVALS.md) and uses the same precision/work budgets (≤ 16384 bits).

## Representation

A real number is denoted by a **program**: a nonempty list of at most 64 nodes; node `i` is a rational constant
or an operation on earlier nodes (`a, b < i`); the last node is the value. Sharing is explicit, so a
subexpression used twice is evaluated once.

| Code | Text | Node | Value |
|---|---|---|---|
| 0 | `c q` | constant | the exact rational `q` |
| 1–3 | `add a b`, `sub a b`, `mul a b` | binary | `x_a + x_b`, `x_a − x_b`, `x_a · x_b` |
| 4 | `neg a` | unary | `−x_a` |
| 5 | `inv a` | unary | `1 / x_a` (domain `x_a ≠ 0`) |
| 6 | `div a b` | binary | `x_a / x_b` (domain `x_b ≠ 0`) |
| 7 | `sqrt a` | unary | `√x_a` (domain `x_a ≥ 0`) |
| 8, 9 | `square a`, `abs a` | unary | `x_a²`, `|x_a|` |

Text grammar (strict, canonical output by `Display`/`creal_text`):

```text
program := node (" ; " node)*
node    := "c " rational | unary " " index | binary " " index " " index
index   := "0" | [1-9][0-9]*            (at most u128)
```

Tokens are separated by single spaces, nodes by the token `;`; rationals as in exact rationals (`2`, `-1/3`; the
canonical form prints `2/1`). Input longer than `64·(2·bits+16)` characters is UnknownWithinBudget. Unused operands
of a node are 0 and non-constant nodes carry the value 0 (checked for raw source records).

| Layer | API |
|---|---|
| Host `math::creal` | `CReal::new/parse/constant/nodes`, `Display`, `approx(n)`, `compare(other, n)` |
| Ordinary DLM `std.creal` | `record CRealNode(op, a, b, value)`, `record CReal(nodes)`, `creal_check`, `creal_const`, `creal_parse`, `creal_text`, `creal_approx(b, x, n)`, `creal_compare(b, x, y, n)` |

## Approximation

`approx(n)` (`n ≤ 4096`) returns a closed rational interval that **contains the real and has width ≤ 2^−n**, or
an explicit failure. Each round evaluates the program in interval arithmetic (N07) with square roots at
resolution `k`:

```text
k = n + 8;  repeat at most 6 rounds while k <= 16384:
    evaluate every node:
        constant            -> [q, q]
        add/sub/mul/neg/square/abs -> the tightest enclosure (exact endpoints)
        inv/div, divisor exactly [0,0]           -> error "computable real division by zero"
        inv/div, divisor interval contains 0     -> retry
        sqrt, radicand interval entirely < 0     -> error "computable real square root of a negative value"
        sqrt, radicand interval straddles 0      -> retry
        sqrt, otherwise                          -> guaranteed enclosure at resolution k
    width <= 2^-n -> result;  otherwise retry
    k = 2k
UnknownWithinBudget: computable real refinement limit reached
```

Why the result is correct: every interval operation contains its exact result for all operand values (inclusion
property), the square-root enclosure is guaranteed (EXACT_INTERVALS.md), so by induction over the nodes every node
interval contains the node's real value; the width test is exact. A domain error is reported only when the bounds
prove it (a point interval `[0,0]` is the exact value 0; an interval below zero contains the negative real).
A divisor or radicand whose interval keeps touching zero may be zero or merely small: this is **never** refuted,
it ends as UnknownWithinBudget, like exhausted precision or work. Rounds whose bounds were too wide are retried at
a doubled resolution, which narrows the intervals of every square root; rational programs are exact points in
the first round.

`compare(x, y, n)` approximates both at precision `n` and compares the intervals (N07 `compare`): `-1`/`1` when
they are disjoint, `0` only when both are the same exact point, otherwise **2 — undecided**. Equal reals that are
not exact points (`√2·√2` and `2`) stay undecided at every precision; this is the honest answer, since equality
of computable reals is not decidable.

Both layers run the same schedule and the same interval operations, so they return the same intervals; their
work units differ (as for `std.exact`) and the source layer additionally revalidates raw records.

## Evidence

* `tools/generate_creal102_oracle.py` (Python `Fraction`/`isqrt` with a 900-digit `Decimal` evaluation of each
  program) generates 343 fixtures: named constants (√2 up to n = 4000, √2+√3, the golden ratio, √√2, 1/√7),
  retries forced by divisors and widths, domain errors, refinement limits, comparisons, 260 random programs of
  2–12 nodes and 40 random comparisons. It asserts containment and the width bound itself and records the minimal
  precision (largest checked intermediate of every evaluated round).
* Host: all 343 rows match, 340 of them also fail with a precision error at one bit less; 63 domain errors,
  3 refinement limits, 2 undecided comparisons. Further tests: exact rational programs, sqrt bounds and widths for
  many n, φ² = φ + 1 within the bounds, precision/work limits, the grammar (20 malformed texts, canonical
  round-trip, operand order, the 64-node limit, unused operands).
* Ordinary DLM: 88 fixtures in the interpreter and native C with identical output, 30 also one bit below the
  minimal precision; raw-record validation, grammar and budget errors; text round-trip of every source fixture
  equals the host canonical text; private helpers stay private; `examples/valid/creal_demo.dlm` runs and builds
  with identical output. Two closed source theorems (exact zero divisor refused, forward operand refused) use the
  legacy finite evaluation.

Results are runtime values, never `StaticProof`; the correctness argument above is documented and tested, not
replayed as a formal proof.

## Open

Complex numbers over computable reals (N09), elementary functions with error bounds (N10), conversions (N11), a
common portable format (N12), unified resource/error categories (N13), dense layouts (N14), scalar witnesses (N15)
and full platform acceptance (N16). Programs are finite expression DAGs; reals defined by limits, series or
user-supplied Cauchy sequences are not representable yet.
