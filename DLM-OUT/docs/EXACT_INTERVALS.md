# Closed rational intervals (0.101.0)

This closes numeric criterion N07 within the active section «numeric foundations and exact arithmetic»: closed
intervals `[lo, hi]` with exact rational endpoints, correct bound order, inclusion, hull and intersection,
arithmetic with guaranteed enclosures, guaranteed square-root bounds, and explicit handling of intervals that
contain zero. Profiles are those of N02–N04: endpoint numerators/denominators ≤ 16384 bits on both layers.
Computable reals (N08) are the next consumer of this family; they are not part of this patch.

## Representation and text

| Layer | Type | Invariant |
|---|---|---|
| Host (`math::interval_q`) | `IntervalQ { lo, hi }` | canonical `Rational` endpoints, `lo <= hi`; private fields, built only by validating functions |
| Ordinary DLM (`std.interval_q`) | `record IntervalQ(lo: BigRational, hi: BigRational)` | a raw record; every public operation revalidates both endpoints and `lo <= hi` |

Text: `[<lo>,<hi>]` with `n/d` endpoints (`[-1/2,3/4]`, `[0/1,0/1]`). Input is strict: brackets, exactly one comma,
no spaces, endpoints as accepted by exact rationals (`2/4` is normalized), `lo <= hi`; input longer than
`4·bits + 8` characters is UnknownWithinBudget. Host `parse`/`Display`, source `ivq_parse`/`ivq_text`.

## Operations

All results satisfy the **inclusion property**: for all `x ∈ X`, `y ∈ Y` the exact result of the operation lies
in the result interval. Because endpoints are exact rationals, every operation except `sqrt` returns the
**tightest** interval with that property (its endpoints are attained).

| Operation | Host | Ordinary DLM | Result |
|---|---|---|---|
| Construct / check | `new(lo, hi)`, `point`, `parse` | `ivq_new`, `ivq_check`, `ivq_point`, `ivq_parse` | refuses `lo > hi` (`interval bounds out of order`) |
| Membership | `contains(q)`, `contains_zero` | `ivq_contains`, `ivq_contains_zero` | `lo <= q <= hi` |
| Inclusion | `subset(Y)` | `ivq_subset` | `X ⊆ Y` |
| Hull / intersection | `hull`, `intersect` → `Option` | `ivq_hull`, `ivq_intersect` → `IntervalQMeet(nonempty, value, budget)` | smallest interval containing both / common part (empty → `None`, `nonempty = false`, `value = X`) |
| Order | `compare` → `IntervalOrder` | `ivq_compare` → `IntervalQOrder(order)` | Less (−1) when `X.hi < Y.lo`, Greater (1), Equal (0) for the same point, Overlap (2) otherwise: undecided, never guessed |
| Width / midpoint | `width`, `midpoint` | `ivq_width`, `ivq_midpoint` | `hi − lo`, `(lo + hi)/2` |
| Add / subtract / negate | `checked_add`, `checked_sub`, `checked_neg` | `ivq_add`, `ivq_sub`, `ivq_neg` | `[lo+lo', hi+hi']`, `[lo−hi', hi−lo']`, `[−hi, −lo]` |
| Multiply | `checked_mul` | `ivq_mul` | min/max of `lo·lo'`, `lo·hi'`, `hi·lo'`, `hi·hi'` |
| Square | `checked_square` | `ivq_square` | `[0, max(lo², hi²)]` if `0 ∈ X`, else ordered squares (tighter than `X·X`) |
| Absolute value | `checked_abs` | `ivq_abs` | `{|x| : x ∈ X}` |
| Inverse / divide | `checked_inv`, `checked_div` | `ivq_inv`, `ivq_div` | `[1/hi, 1/lo]`, `X·(1/Y)`; an interval containing zero (including touching it, `[0,1]`) is refused: `interval division by an interval containing zero` |
| Square root | `checked_sqrt(k)` | `ivq_sqrt(b, X, k)` | guaranteed enclosure, see below; `lo < 0` is refused: `interval square root of negative values`; `k <= 16384` |

## Square roots with guaranteed bounds

For a nonnegative canonical endpoint `x = p/q` and resolution `k`:

```text
s = isqrt(p·q·4^k)          (exact integer floor root)
lower(x) = s / (q·2^k)
upper(x) = lower(x)                  if s² = p·q·4^k
           (s + 1) / (q·2^k)         otherwise
```

Since `sqrt(x) = sqrt(p·q)/q` and `s ≤ 2^k·sqrt(p·q) < s + 1`, we have `lower(x) ≤ sqrt(x) ≤ upper(x)` and
`upper(x) − lower(x) ≤ 1/(q·2^k) ≤ 2^−k`; the bounds are exact when `x·4^k·q²` is a perfect square. The result
for `X = [lo, hi]` is `[lower(lo), upper(hi)]`; `sqrt(0) = 0`. Only integer arithmetic is used — no floating
point. The intermediate integers (`2^k`, `4^k`, `p·q`, `p·q·4^k`, the root and its remainder, `q·2^k`, `s+1`)
must fit the precision, which bounds `k` together with the endpoint sizes.

## Precision, work and assurance

Every operand endpoint and every checked intermediate value listed in the generator
(`tools/generate_interval_q101_oracle.py`) is checked against the precision; the smallest admissible precision
of an operation is therefore defined and the same on both layers, and one bit less fails with
`UnknownWithinBudget … precision exceeded`. Budgets are threaded: host operations charge the caller's
`ExactBudget`; source operations pass each successor budget on and return the final one. Host and source work
units differ (as for `std.exact`); comparisons and the final result carry no hidden reset. Results are runtime
values, not `StaticProof`; the two closed source theorems (zero divisor refused, reversed bounds refused) use the
legacy finite evaluation. The enclosure and square-root guarantees are argued above and tested, not replayed as
formal proofs.

## Evidence

* 5055 fixtures from Python `fractions.Fraction` and `math.isqrt` (byte-identical regeneration), with the
  generator asserting the square-root guarantee itself in exact arithmetic; the host matches all of them at
  their minimal precision and fails at one bit less for 4559 of them; endpoints up to 13991 bits.
* Inclusion and tightness on a grid of 28 intervals: sampled exact sums, differences, products, quotients,
  squares, absolute values and inverses lie in the results; product and inverse endpoints are attained; square
  is contained in `X·X`; hull/intersection laws; disjointness agrees with `compare`.
* Square roots for resolutions up to 300: `lower² ≤ lo`, `upper² ≥ hi`, both bounds within `2^−k`; exactness on
  rational squares; refusal of negative intervals and of `k > 16384`.
* Ordinary DLM: 233 fixtures in the interpreter and native C with identical output, 77 also one bit below the
  minimal precision; raw-record, budget, text, zero-divisor and negative-root errors; budget threading; private
  helpers stay private; `examples/valid/interval_q_demo.dlm` runs and builds with identical output.

## Open

Computable reals (N08) build on this family since 0.102 (COMPUTABLE_REALS.md); their complex numbers (N09) and elementary functions (N10) will too;
conversions (N11), a common portable format (N12), unified resource/error categories (N13), dense layouts
(N14), scalar witnesses (N15) and full platform acceptance (N16) remain open. Interval arithmetic has the usual
dependency effect (`X − X ≠ [0, 0]`); this is a property of the specification, not an error.
