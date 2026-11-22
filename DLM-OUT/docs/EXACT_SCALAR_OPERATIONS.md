# Exact powers, lcm and integer square roots (0.96.0)

This closes criterion N05 within the active numeric-foundation section.
Originally host magnitudes were <=16384 bits and DLM/native C <=1024 bits.
The 0.97 extension in SOURCE_EXACT_PRECISION.md aligns their precision ceilings.
No other mathematical section is advanced.

## Operations and domains

| Operation | Host API | Ordinary DLM API | Result / domain |
|---|---|---|---|
| Integer power | Natural/Integer.checked_pow(exponent: u128, budget) | big_pow(budget, value, exponent: Nat) | Exact integer; nonnegative machine exponent |
| Rational power | Rational.checked_pow(exponent: i128, budget) | bigrat_pow(budget, value, exponent: Int) | Exact rational; negative exponent requires a nonzero base |
| Least common multiple | Natural/Integer.checked_lcm(other, budget) | big_lcm(budget, a, b) | Nonnegative; lcm(0,b)=0 for every valid b |
| Integer square root | Natural/Integer.checked_sqrt_rem(budget) | big_sqrt_rem(budget, value) | Nonnegative input; floor root and exact remainder |

For these power APIs, `a^0=1`, including `0^0=1`, is the empty-product
convention. An invalid or out-of-profile base is rejected even for exponent
zero. A negative power of zero is a domain error. Rational exponents cover the
full machine Int range, including i128::MIN; its magnitude is never formed by
signed negation. Integer powers use the full u128 exponent range.

The host Natural square-root API returns `(Natural, Natural)`; the Integer API
returns `(Integer, Natural)` and rejects negative inputs. Source
`BigSquareRoot(root: BigInt, remainder: BigInt, budget: ExactBudget)` contains
canonical nonnegative integers on success. Its fields satisfy

```text
n = root * root + remainder
0 <= remainder < 2 * root + 1
```

The root is an integer floor, with no floating-point approximation. Source
power/lcm results retain `BigIntCalc` / `BigRationalCalc`; all new source APIs
return Result and carry successor budgets on success. Raw result records are
ordinary descriptions, not invariant-bearing proof types.

## Algorithms and precision

Powers use exponentiation by squaring, at most 128 exponent-bit rounds. They
skip the final unused square, so `2^1` succeeds in a 2-bit profile. Each needed
result must fit the selected profile; there is no truncation or modular
replacement. Rational negative powers first invert the validated base.

Lcm validates both operands, including a discarded operand of lcm(0,b), and
computes `abs(a)/gcd(abs(a),abs(b))*abs(b)`. Division precedes multiplication:
lcm(a,a) remains admissible when a fits but a*a would exceed precision. An
unrepresentable final lcm returns UnknownWithinBudget.

Square roots use restoring binary digit pairs. If a processed prefix is
`s*s+r`, the next prefix has candidate root `2*s` and remainder `4*r+digit`,
where digit is 0..3. The candidate is increased when the remainder is at least
`4*s+1`, the difference of the adjacent candidate squares. This avoids forming
the input square or the next-root square at the profile boundary. Both paths
have at most 8192 pair rounds; current source folds over <=1024 words, with
eight pairs per word and inner helper fuel 16.
This explanation states the algorithm; it is not a replayed formal proof.

Source private power helpers have fuel 256 and an explicit 128-round step
counter. Private word/root helpers remain inaccessible through imports and
inherited redeclarations. Shape/canonicality/precision checks precede use of
raw source operands. Existing Nat/Int types, representation tags, compiler/IR,
ABI and kernel rules are unchanged.

## Resources and errors

The host uses one mutable ExactBudget, charging loop rounds and limb operations.
Work exhaustion consumes the remaining work; operands remain unchanged and no
partial result escapes. Input profiles are validated before algebraic shortcuts.

The source retains the documented reservation policy
`C(w)=20000*(2*w+3)^4`. Powers charge initial validation and each multiplication;
negative rational powers also charge inversion. Lcm threads the remaining
budget through initial validation, gcd, division and multiplication. Square
root reserves its existing operand-size cost before its bounded private loop.
Successor budgets are passed without internal resets. A failed Result contains
no successor budget. Original source budgets remain immutable/copyable.

Source reservation units and host limb-work units are distinct; neither is a
formal cost certificate or a hard memory/time guarantee. Invalid raw records,
negative square roots and negative powers of zero are errors. Exhausted work
or precision yields `UnknownWithinBudget:`. Source validates/reserves before
checking these domains, as in its existing division operations. An exhausted
reservation can therefore prevent further domain inspection.

## Validation and execution

`tools/generate_exact_ops096_oracle.py` generates 2134 deterministic vectors
using Python int, Fraction, math.lcm and math.isqrt. Cargo consumes the checked-in
TSV and requires no Python. All vectors exercise host APIs; 96 selected vectors
also execute as ordinary DLM and standalone native C, including 1024-bit values,
perfect-square neighbours, i128::MIN and maximum machine exponents on unit
bases. Host boundaries also reach 16384 bits.

`exact_ops.rs` checks the fixtures, exhaustive root identities for 0..32768,
precision/domain failures, skipped unused squares, cancellation before lcm
multiplication and shared-work exhaustion. `source_exact_ops.rs` checks
interpreter/C parity, raw-record rejection, budget chaining, input preservation
and helper privacy. `cli_exact_ops.rs` checks run/build of the complete example.
Final workspace/platform/package results are in the progress journal and UPDATE.

```sh
dlm run --trusted-only --lib lib examples/valid/exact_scalar_operations.dlm
dlm build --trusted-only --lib lib -o scalar_demo examples/valid/exact_scalar_operations.dlm
```

Assurance remains runtime computation with differential/oracle tests. This
adds no StaticProof, independent scalar certificate or optimizer authority;
N15 remains open. Dense layouts, real/interval/complex families and full
platform acceptance remain required by N06–N16. SOURCE_EXACT_PRECISION.md
records N04; further extensions must revisit all loop/input/reservation limits.
