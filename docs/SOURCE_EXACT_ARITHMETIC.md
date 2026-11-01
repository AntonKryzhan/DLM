# Exact integers and rationals in ordinary DLM (0.94.0)

Current 0.97 extension: SOURCE_EXACT_PRECISION.md specifies the 1..16384-bit
source/native range and conditional work/word caps. The original 0.94 profile
below remains historical context.

## Behavior and scope

`std.exact` / `StdExact` makes multiprecision integer and rational arithmetic
available directly from `.dlm` and from programs compiled with `dlm build`.
It uses ordinary checked functions, records, lists, Result and the existing C
backend. No new compiler builtin, host callback, FFI, runtime numeric tag, IR
rule, ABI or proof-kernel rule is introduced. Existing source Nat/Int remain
u128/i128; `std.rational` and `std.matrix_q` keep their previous profiles.

The first source profile supports 1..1024 bits per integer magnitude and per
reduced rational numerator/denominator. This is a representation with explicit
resource limits, not unlimited precision. The separate 0.93 host runtime/CLI
still supports up to 16384 bits. Their layouts, admissible text lengths and
work accounting are distinct and are not implicitly converted.

## Representations and validation

`BigInt(negative:Bool,words:List<Nat>)` uses little-endian radix 65536 words.
Zero is an empty list with positive sign. A nonzero canonical value has no
high zero word. Public records have <=64 words; every word is in 0..65535 and
the actual bit width must fit the supplied precision.

`BigRational(negative:Bool,numerator:List<Nat>,denominator:List<Nat>)` uses the
same magnitude encoding. The denominator is nonzero, gcd(n,d)=1 and zero is
positive 0/1. Raw records are descriptions, not evidence of these invariants.
Every public consumer validates them. Integer construction from words permits
high zero padding within the 64-word input cap and normalizes it; arithmetic
rejects noncanonical raw operands. Rational construction normalizes; arithmetic
rejects unreduced raw fractions. Empty/negative-zero/zero-denominator cases
are handled explicitly. Private helpers cannot be called from importing
modules or replaced by redeclaring inherited names (tested).

Word addition uses values <2^17; multiply/scale carry steps use values <2^32.
Small decimal division has carry<10. Dimension/index/cost values are bounded
before multiplication. These intermediates fit the existing machine Nat,
while the full integer value is never packed into a Nat. No saturation or
wrapping is substituted for an exact result. Rational cross products and sums
may use up to 2049 bits /129 private words before reduction; output precision
is checked after gcd cancellation. This permits a representable reduced result
whose intermediate numerator exceeds the public precision.

## Explicit source budgets and result types

`ExactBudget(bits:Nat,work:Nat)` is valid for bits=1..1024 and work<=10^15.
`exact_budget(bits,work)` validates a requested profile. `exact_default_budget()`
returns 256 bits and 10^15 reservation units. Raw invalid budgets are rejected.

Successful arithmetic returns a value AND successor budget:
- `BigIntCalc(value:BigInt,budget:ExactBudget)`;
- `BigRationalCalc(value:BigRational,budget:ExactBudget)`;
- `BigTextCalc(value:Text,budget:ExactBudget)`;
- `BigCompare(order:Int,budget:ExactBudget)`, order=-1/0/1;
- `BigDivision(quotient:BigInt,remainder:BigInt,budget:ExactBudget)`.

Pass the returned budget to the next operation. Values/budgets are immutable:
a failed operation returns Result.err and no continuation budget. The caller
must stop that calculation or explicitly choose another budget. This library
does not supply linear ownership, an uncopyable gas token or global metering;
copying/restarting a budget is possible and explicit. Original operands and
budget records are unchanged on success and failure.

For an operation with largest operand word count w, reserve
`C(w)=20000*(2*w+3)^4` units. Parse reserves using ceil(bits/16), so its cost is
known before digit processing. Validation first checks bounded record shapes;
work reservation precedes expensive rational canonicality gcd. Insufficient
work or precision returns text beginning `UnknownWithinBudget:`. Malformed
records/text, invalid configuration and zero division are domain errors.

This deterministic reservation formula is an implementation policy, not a
measured time, a formal upper-bound certificate, or the host limb-work counter.
Independent caps on input lengths/words and explicit recursive measures bound
the algorithms. Private gcd has an explicit 8192-step limit returning Result.err;
binary division, digit/word construction and formatting have declared fuel
within the existing language limits. Fuel is an interpreter safeguard and is
erased by the existing C lowering for checked terminating recursion; the actual
size/step checks remain executable source code. No claim of bounded process
memory or hard real-time execution follows from the reservation units.

A correct chain (error handling preserved):

```dlm
module examples.exact_chain
import std.exact

theory App extends StdExact {
    fn twice(b: ExactBudget, text: Text) -> Result<BigIntCalc, Text> =
        match big_parse(b, text) {
            err(e) => err(e, BigIntCalc),
            ok(parsed) => big_add(field(parsed, budget), field(parsed, value), field(parsed, value))
        }
}
```

## Public operations

Integers:
- `big_from_words(b,negative,words)`, `big_from_int(b,i:Int)`, `big_parse(b,text)`;
- `big_add/sub/mul(b,a,c)`, `big_neg(b,a)`, `big_cmp(b,a,c)`;
- `big_div_rem(b,a,c)`, `big_gcd(b,a,c)`, `big_text(b,a)`.

Integer div/rem is Euclidean: a=c*q+r with 0<=r<abs(c), for all signs of c.
Gcd is nonnegative, including gcd(0,0)=0. `big_from_int` accepts i128::MIN
without negating the machine Int. Canonical zero never acquires a minus sign.

Rationals:
- `bigrat_from_ints(b,n:BigInt,d:BigInt)`, `bigrat_parse(b,text)`;
- `bigrat_add/sub/mul/div(b,a,c)`, `bigrat_cmp(b,a,c)`;
- `bigrat_neg/inv/text(b,a)`.

A signed denominator is accepted by `bigrat_from_ints`, which moves its sign
into the result. Text fractions require an unsigned positive denominator.
Division/inversion by zero returns an error, never a placeholder value.

Parsing/formatting:
- strict ASCII digits, an optional leading minus, and optional `/denominator`
  for rational input; no plus, whitespace, Unicode digits or decimal points;
- integer text length<=bits+2; rational total text length<=2*bits+3;
- each parsed component must fit bits before rational normalization;
- leading zeros are accepted within the text caps; output is canonical;
- integer output is signed decimal, rational output signed `n/d` including `/1`.

The implementation uses radix-word add/sub/scale, shift-and-add multiplication,
exact binary long division and Euclidean gcd. It is a reference implementation
on ordinary lists, not a dense/vectorized performance backend.

## Running and building

```sh
dlm run --trusted-only --lib lib examples/valid/exact_arithmetic.dlm
dlm build --trusted-only --lib lib -o exact_demo examples/valid/exact_arithmetic.dlm
```

The example computes 2^128+2^128, 2^128*2^128, a >128-bit rational, 1/2+1/3,
and explicit work exhaustion. Compiled output is identical to the interpreter
and requires no Python or external big-number package. It uses the established
record/list C representation and runtime memory management.

## Validation, architectural laws and open obligations

`source_exact.rs` compares ordinary DLM with the existing host arithmetic and
native C. Tests cover carries/borrows, all sign cases used by Euclidean division,
gcd, fractional arithmetic/order, strict text, i128::MIN, >128-bit products,
1024-bit input and overflow, raw bad words/signs/fractions, zero division,
shared budget sequencing and private helper visibility/shadowing. An independent
Python integer/Fraction fixture supplies 124 additional vectors; cargo consumes
the checked-in TSV and does not require Python. CLI run/build tests exercise the
standalone example. Two closed specifications retain existing legacy finite
computation certificates, not independent algorithm proofs.

Layers/trust (laws 1/3/4/24/28/40): ordinary source functions -> existing
checker/Typed Core/C; no StaticProof or optimizer authority follows from runtime
answers. Machine/profile separation and bounds (29/30/32) are explicit, with
differential tests (39) and per-patch acceptance/remaining work (41/42).
Public validation is part of the API; an ordinary record type alone does not
prove canonicality. Tests do not prove all arithmetic algorithms or the runtime
collector correct. Final platform/regression results are in UPDATE and the
progress journal.

Open: source precision up to the host 16384-bit ceiling, efficient dense limb
layouts/generics, BigRational source matrices (the previous std.matrix_q still
uses its 32-bit scalar profile), independent arithmetic/Q certificates and formal
algorithm proofs, rational homology, polynomials and real/interval arithmetic.
Proof-kernel/ownership milestones and the deferred positive geometry/BIG FOOT
tracks retain their separate obligations. This patch does not implement them.

## Native memory/undefined-behavior check

The compiled example was also run with GCC AddressSanitizer and UBSan
(`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie`),
`ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`.
It exited successfully with the expected output and no sanitizer diagnostics.
LeakSanitizer could not enumerate /proc tasks in this execution environment;
leak detection was therefore disabled for that check. No new leak-free or
general collector-correctness claim is made.

## 0.95 matrix integration

`bigrat_check` validates a scalar under a supplied source budget and returns
it with the successor budget. `std.matrix_bigq` uses this public boundary for
all coefficients; source <=4-axis BigRational matrices are described in
SOURCE_BIGRATIONAL_MATRICES.md. Q_MATRIX_WITNESSES.md adds separate host exact
rank/kernel and residual checking. Full source precision expansion, efficient
layouts and proof-kernel integration remain open.

## 0.96 scalar operations

`big_pow`, `bigrat_pow`, `big_lcm` and `big_sqrt_rem` add budgeted powers,
least common multiples and integer floor roots with exact remainders. Source
precision remains 1..1024 bits. BigSquareRoot carries nonnegative root and
remainder values plus the successor budget on success. Domains, empty-product
convention, helper bounds, reservation sequencing and independent fixtures are
specified in EXACT_SCALAR_OPERATIONS.md. No new proof/kernel authority follows.
