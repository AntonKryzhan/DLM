# Exact complex numbers over Q (v0.98.0, N06)

`math::complex_q::ComplexQ` and ordinary `std.complex_q` represent Q(i) as two
canonical exact rational components: `real + imag*i`, with `i*i = -1`.
There is no order on complex numbers. Equality compares both rational values;
the square of the modulus is rational, while the modulus itself need not be.

## Representation and entry points

The host type has private `Rational` fields, borrowed `real()`/`imag()` accessors,
`zero()`, `one()`, `i()`, `from_parts(real, imag, budget)` and
`from_integers(real: i128, imag: i128, budget)`. `Eq`/`Hash` use the canonical
representation; `checked_equal` additionally enforces the caller's profile.
Every fallible operation validates both components, even when a zero or an
unequal real part would otherwise permit a shortcut.

The source module imports `std.exact`; `StdComplexQ extends StdExact`. Records:

```text
ComplexQ(real: BigRational, imag: BigRational)
ComplexQCalc(value: ComplexQ, budget: ExactBudget)
ComplexQEqual(value: Bool, budget: ExactBudget)
```

Public constructors/validators are `complexq_new(b, real, imag)`,
`complexq_from_ints(b, real: Int, imag: Int)` and `complexq_check(b, z)`.
Rational constructors in `std.exact` normalize fractions before composition.
Raw `ComplexQ` records are descriptions: every public consumer rechecks all
components with `bigrat_check`. Unreduced fractions, negative zero, zero
denominators, malformed/padded words and out-of-profile values are rejected.
No complex constructor silently repairs a malformed rational description.

| Operation | Host method | DLM function (budget first) |
|---|---|---|
| Equality | `checked_equal` | `complexq_equal` |
| Negation | `checked_neg` | `complexq_neg` |
| Conjugation | `conjugate` | `complexq_conj` |
| Addition/subtraction | `checked_add` / `checked_sub` | `complexq_add` / `complexq_sub` |
| Multiplication/division | `checked_mul` / `checked_div` | `complexq_mul` / `complexq_div` |
| Reciprocal | `reciprocal` | `complexq_inv` |
| Squared modulus | `norm_squared` | `complexq_norm_squared` |

Complex-valued DLM results are `Result<ComplexQCalc, Text>`; equality returns
`Result<ComplexQEqual, Text>` and squared modulus returns
`Result<BigRationalCalc, Text>`. Errors never supply a fallback numeric value.
Host `Display` and source `complexq_text` display `p/q;r/s`, retaining `/1` and
canonical zero `0/1`. This display helper is not a versioned interchange codec;
the complete parse/format/portable-format obligation remains N12.

## Algorithms, precision and work

For `a+bi` and `c+di`, multiplication computes, in order, `ac`, `bd`, `ac-bd`,
`ad`, `bc`, `ad+bc`. Squared modulus computes `a*a`, `b*b`, then their sum.
Division first validates both inputs, rejects `(c,d)=(0,0)` with
`exact complex division by zero`, computes `s=c*c+d*d`, multiplies the numerator
by `c-di` using the stated order, and divides both resulting components by `s`.
Reciprocal is `1/z`. Conjugation negates only the imaginary component; zero
keeps its canonical nonnegative sign. All loops are inherited bounded scalar
algorithms; this module adds only finite call composition.

Precision is **1..16384 bits per numerator/denominator**, using the existing
`ExactBudget` profile in host, interpreter and generated C. Each rational
intermediate must fit that profile after its own canonical reduction. A later
complex cancellation does not rescue an earlier overflowing rational result:
e.g. at 8 bits `(16+16i)/(16+16i)` reports precision exhaustion because `16*16`
does not fit, although the final mathematical quotient is 1. Increase the
precision budget to evaluate that expression. No success claim covers every
expression whose final value alone fits. Internal primitive temporary bounds
remain those in `SOURCE_EXACT_PRECISION.md`; no new unchecked machine products
or unbounded word arrays are introduced.

Host work uses the existing mutable limb-work counter plus a validation/copy
charge of `sum(ceil(num_bits/32)+ceil(den_bits/32)+1)` across the two components.
Binary operations charge each operand in sequence. Source work uses only
existing scalar reservations `C(w)=20000*(2*w+3)^4`, with every successful
successor budget passed to the next call. Failed calls expose no successor
budget; source values and copied budgets remain immutable, not global gas.
Neither path resets a budget during compound arithmetic. Units differ between
host and source as already documented; N13 remains open. Precision, invalid
input and work errors preserve scalar categories/messages; domain checking
follows input validation, so invalid input/work exhaustion can precede the
division-by-zero error.

## Guarantee and remaining obligations

This is a computational library using ordinary records/functions, not a new
compiler builtin or numeric tag. Source code runs through the existing
interpreter and C lowering. Complex values carry no proof object or new trust
level, kernel rule, complex-order capability or optimizer authority. Three
closed module regression theorems check the imaginary unit, zero division and
an invalid imaginary component under unequal real parts using the existing
legacy finite-evaluation certificate path. They are not independently replayed
general field laws. Validation is runtime
validation within the declared resource profile, not a general formal proof of
field laws or compiler correctness. Existing Nat/Int and scalar APIs persist.

N06 is validated by 1059 deterministic independent Python `Fraction` pair
cases, of which 179 run through both the interpreter and standalone C. Cases
include 1-bit units and 1025/4096/16384-bit numerators and denominators, exact
cancellation, conjugation, reciprocal, squared modulus, zero division and
intermediate-precision refusals. Fixtures regenerate byte-identically with
`python3 tools/generate_complex_q098_oracle.py`; no float/production DLM
arithmetic is used to construct the expected values or source word literals.

Separate source tests exercise 464 malformed-input/profile/work refusals,
30 budget-boundary/immutability assertions, 9 constructor/text/sign/machine
extreme assertions and helper privacy. All run in the interpreter and C;
maximum source comparisons inspect each canonical word/sign without calling
production complex/rational comparison. Host tests also check Gaussian units,
distributivity, conjugation involution, `z*conj(z)=norm_squared(z)`, norm
multiplicativity, inverses, exact work thresholds and input immutability.
CLI run/build execute `examples/valid/exact_complex_q.dlm`. Full acceptance
configuration/results are recorded in `MATHEMATICS_PROGRESS.md` and `UPDATE.md`.
The full Linux workspace passes 705 tests (0 failed/ignored). All four generated
C acceptance programs pass ASan/UBSan with leak detection disabled; Windows
acceptance remains open. No new Rust-file Clippy warnings are introduced.
Computable real/complex families, intervals, conversions, complete codecs,
resource unification, dense layouts, scalar witnesses and platform acceptance
remain N07–N16. Later mathematical sections continue to wait.
