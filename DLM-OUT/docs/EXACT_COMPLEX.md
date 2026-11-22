# Additional exact complex API over Q (0.100.1)

This integrates the separately supplied Gaussian-rational implementation as an additional API. Numeric criterion
N06 was already closed in v0.98. The existing `math::complex_q::ComplexQ`, source `ComplexQ` records and
`complexq_*` functions retain their contracts and `re;im` display; see [EXACT_COMPLEX_Q.md](EXACT_COMPLEX_Q.md).
The new host `math::complex::ComplexRational` and source `BigComplex` / `bigc_*` entries coexist with them.
Both represent Q(i) = { a + b·i : a, b ∈ Q }, i² = −1, over existing canonical exact rational components.
The current precision profile is 1..16384 bits per numerator/denominator in host, ordinary DLM and native C;
N04 remains closed. Computable complex numbers are N09 and are outside this integration.

## Representation and text

| Layer | Type | Invariant |
|---|---|---|
| Host (`math::complex`) | `ComplexRational { re, im }` | both components canonical `math::exact::Rational` (private fields; constructed only through validating functions) |
| Ordinary DLM (`std.complex_q`) | `record BigComplex(re: BigRational, im: BigRational)` | a raw record; every operation revalidates both components (canonical words, positive denominator, no negative zero, gcd 1, width ≤ profile) |

Canonical text: `<re>+<|im|>i` or `<re>-<|im|>i`, each component `n/d` (`0/1+0/1i`, `-1/1+0/1i`, `1/2-3/4i`).
Input is strict: the last `+`/`-` that is not the first character separates the parts; the imaginary magnitude
starts with a digit and carries no sign of its own; components are read like exact rationals (`7`, `-1/2`, `2/4`
is normalized). Spaces, a leading `+`, a missing `i`, `1+i`, `1+-2i`, zero denominators and input longer than
`4·bits + 8` characters are refused (the last one as UnknownWithinBudget). Host `parse`/`Display` and source
`bigc_parse`/`bigc_text` share this canonical text and grammar. Resource refusals retain each layer's scalar
parser limits: host decimal magnitudes allow at most `bits + 1` characters, while each rational text is bounded
by `2*bits + 3`; source components retain `std.exact`'s rational-input and work limits. Excess leading zeroes
can therefore reach a layer's input budget even when the represented rational is small.

The source separator scan processes blocks of 16 characters, so accepted wide inputs do not exhaust the
8192-call scan fuel. At 16384 bits the complex input limit is 65544 characters; each rational component also
keeps its own `std.exact` input bound. The two source record families are nominally distinct; conversion uses
their explicit rational fields rather than an implicit coercion.

## Operations

| Operation | Host | Ordinary DLM | Result |
|---|---|---|---|
| Construct | `new(re, im, budget)`, `from_rational`, `zero`, `one`, `i` | `bigc_new`, `bigc_check`, `bigc_from_rational`, `bigc_i` | validated value |
| Equality | `checked_eq` | `bigc_eq` → `BigComplexFlag` | componentwise equality of validated canonical values |
| Add / subtract | `checked_add` / `checked_sub` | `bigc_add` / `bigc_sub` | `(a±c) + (b±d)i` |
| Negate / conjugate | `checked_neg` / `checked_conj` | `bigc_neg` / `bigc_conj` | `−a − bi` / `a − bi` |
| Multiply | `checked_mul` | `bigc_mul` | `(ac − bd) + (ad + bc)i` |
| Squared modulus | `checked_norm` → `Rational` | `bigc_norm` → `BigRationalCalc` | `a² + b²` (nonnegative rational) |
| Divide | `checked_div` | `bigc_div` | `(ac + bd)/n + ((bc − ad)/n)i`, `n = c² + d²` |
| Inverse | `checked_inv` | `bigc_inv` | `c/n − (d/n)i` |
| Text | `parse`, `Display` | `bigc_parse`, `bigc_text` | canonical text |

Division and inversion validate the operands first and then refuse a zero divisor with
`exact complex division by zero` (host and source use the same message). There is no partial result.

## Precision and intermediate values

Every operand component and rational result must fit the selected precision (numerator and denominator width
≤ bits). Component operations interleave validation and computation in the order below; division and inversion
explicitly validate the complete operands before the zero-divisor check:

| Operation | Checked intermediate values |
|---|---|
| add / sub | `a±c`, `b±d` |
| mul | `ac`, `bd`, `ad`, `bc`, `ac−bd`, `ad+bc` |
| norm | `a²`, `b²`, `a²+b²` |
| div | `c²`, `d²`, `n`, `ac`, `bd`, `s=ac+bd`, `bc`, `ad`, `t=bc−ad`, `s/n`, `t/n` |
| inv | `c²`, `d²`, `n`, `c/n`, `d/n` |
| neg / conj / eq / text | operands only |

So the smallest admissible precision of an operation is the largest width among its operands and these values;
with one bit less it fails with `UnknownWithinBudget … precision exceeded`. There is no truncation, rounding or
modular replacement. A result may need more precision than its operands (an intermediate product can be wider
than the final value): this is an explicit property of the profile, tested at every boundary.

## Work budgets

The work budget is threaded through the component operations: host operations charge the same
`ExactBudget` (in host work units) and fail with `UnknownWithinBudget: exact arithmetic work exhausted`; source
operations pass the budget returned by each `bigrat_*` call to the next one and return the final successor budget
inside `BigComplexCalc` / `BigComplexFlag`. Source and host work units differ (as for `std.exact`); neither is a
formal cost certificate. Invalid source budgets are refused: bits must be in 1..16384, with work at most 10^15
for bits ≤ 1024 and at most u64::MAX for the wider profile. A valid precision with insufficient work reports
work exhaustion; it does not become an invalid precision. The 1025-bit success/refusal cases test this distinction.

## Assurance

Results are runtime values computed by the host library or by ordinary DLM code compiled through the existing
C backend; they are not `StaticProof` and give no optimizer authority. The algorithms are the textbook field
operations; their correctness is supported by tests, not by a replayed proof. The two closed source theorems
in this additional API (`i_squared_is_minus_one`, `division_by_zero_is_refused`) use existing legacy finite evaluation.

## Evidence

* `tools/generate_complex097_oracle.py` produces 1944 fixtures from Python `fractions.Fraction`, independent of
  DLM, with the minimal precision of every operation; regeneration is byte-identical.
* Host: every fixture at its minimal precision, and with one bit less a precision failure (1867 boundaries);
  components up to 16014 bits; field identities on a 49-element grid (commutativity, `(z+w)−w = z`,
  `(zw)/w = z`, `z·z⁻¹ = 1`, `z·z̄ = |z|²`, `|zw|² = |z|²|w|²`, conjugation is multiplicative and an involution,
  `i² = −1`); strict text; work threading and exhaustion; operand validation before the zero check.
* Ordinary DLM: 118 fixtures in the interpreter and native C (identical output), 47 of them also at one bit less,
  including results exactly at the 1024-bit limit; raw-record, budget, text and zero-division errors; budget
  threading; private helpers stay private.
* Both source APIs run together in the interpreter and native C. The integration checks a 16384-bit component,
  rejection at 16383 bits, the valid 1025-bit profile, exact work-exhaustion text, and parser success at the
  65544-character input limit with rejection one character above it.
* `examples/valid/complex_q_demo.dlm` and the existing `exact_complex_q.dlm` have interpreter/native CLI tests.
  Current Linux commands and acceptance results are recorded in [CUMULATIVE_BUILD.md](CUMULATIVE_BUILD.md)
  and [UPDATE.md](../UPDATE.md). Historical sanitizer/MinGW claims in the supplied patch are not new platform
  acceptance for this consolidation.

## Limits and open obligations

Source widths are bounded by 16384 bits using the existing N04 block algorithms; source words still use lists
(dense storage and its performance acceptance remain N14). There are no complex powers, roots, arguments or polar forms (not required by
N06). Conversions between numeric families (N11), a versioned portable format for all families (N12), unified
resource/error contracts (N13) and independently checkable scalar witnesses (N15) remain open.
