# Source exact precision through 16384 bits (0.97.0, N04)

Ordinary `std.exact` now matches the host 1..16384-bit magnitude ceiling.
Nat/Int remain u128/i128. Public function signatures, numeric tags, IR/ABI,
kernel rules and the default 256-bit source budget are unchanged.

| Source precision | Input word cap | Maximum reservation work |
|---|---|---|
| 1..1024 bits | 64 radix-65536 words | 1000000000000000 |
| 1025..16384 bits | 1024 radix-65536 words | 18446744073709551615 |

Every public consumer validates raw budgets, words, signs, actual bit width
and rational canonicality. The word constructor permits bounded zero padding.
The low-profile limits are preserved; 1025 is now valid and 16385 is rejected.
Host/source work units and text allowances remain different. Source work still
uses `C(w)=20000*(2*w+3)^4`; C(1024)=353909839464020000, exceeding the old
10^15 maximum. Compound calls pass the successor budget without a hidden reset.
Work/precision exhaustion yields UnknownWithinBudget, with no partial result.
Original budgets remain immutable/copyable, not linear gas tokens.

## Algorithms and bounds

Division streams high radix words. Sixteen restoring bit rounds produce each
quotient word, keeping remainder below the divisor and digits in 0..65535.
Leading zero quotient words are omitted; checked equality/one/quotient-one
cases avoid unnecessary division. Signed public division remains Euclidean.

Gcd runs 8192 blocks of at most 16 Euclidean steps. After at most one initial
swap, two steps at least halve the smaller operand; a 32769-bit intermediate
needs fewer than 65540 steps, below the 131072-step cap. Measures and fuel are
checked by the existing termination checker. This bound is not an independently
replayed formal correctness proof.

Input uses blocks of up to nine digits. If current_width+30<=bits, the entire
block fits because 10^9<2^30; otherwise per-digit checks preserve the previous
precision-versus-invalid-character error order. Scaling splits a large carry
into two canonical radix words. Output divides by 10^9 and pads lower groups
to nine digits. Rational slash search uses 16-character blocks. Integer and
rational text caps remain bits+2 / 2*bits+3 code points, with strict ASCII rules.

Multiplication iterates the shorter operand and skips zero words. Positive
scaling preserves canonical form without repeated trimming; small division
omits leading zero digits during construction. Integer roots fold over high
radix words, processing eight restoring digit pairs per word. This avoids
retaining 8192 recursive input frames in the reference interpreter.

| Private traversal | Maximum required | Fuel / explicit cap |
|---|---|---|
| Word arithmetic/traversal | <=2049 words plus bounded carry slots | 4096 |
| Division digit / words | 16 rounds / <=2049 words | 32 / 4096 |
| Gcd inner / outer | 16 steps / <=8192 blocks | 32 / 16384 |
| Decimal parse blocks | ceil(32771/9)=3642 | 4096 |
| Boundary digits / power of ten | <=9 | 32 / 16 |
| Decimal output | <=549 groups | 1024 steps, fuel 4096 |
| Slash blocks / characters | <=2049 / 16 | 4096 / 32 |
| Integer root | <=1024 folded words, 8 pairs each | inner fuel 16 |
| Power exponent | <=128 bit rounds | 128 steps, fuel 256 |

Products have <=32768 bits; rational cross-product sums have <=32769 bits,
or 2049 words. Reduction precedes output precision checking. Decimal scaling
and small-division numerators stay below 2^46; other word/cost arithmetic fits
machine Nat. Public data remain records/lists, with no truncation or saturation.

As a numeric dependency, the interpreter internally shares immutable function
declarations through Arc instead of copying their AST at each scalar call.
Public Runtime/RuntimeValue APIs, evaluation order, effects, fuel and measure
checks are preserved. This is declaration storage, not result caching or a
dense numeric representation.

## Verification and remaining work

`generate_wide_exact097_oracle.py` supplies 35 independent Python int/Fraction
cases and the 4933-digit text of 2^16384-1. Tests compare every result word/sign
in interpreter/native C at 1025/4096/16384 bits. Stress cases exercise all 8192
root pairs and the gcd of adjacent Fibonacci numbers at index 9000, beyond the
former 8192-step cap. Text cases reach 32771 characters; malformed values,
resource limits, budget chaining and helper privacy are checked. The complete
CLI example performs 16384-bit division, yielding a 1024-word quotient and
remainder 2. Final test/platform/package evidence is in UPDATE and the progress
journal.

Reference list algorithms may have high time/memory costs. N14 dense layouts
and measured performance thresholds remain open; reservation units are not
wall-clock or formal cost certificates. Runtime results add no StaticProof or
optimizer authority. N15 proof transport, N16 full platform acceptance and
N06–N13 numerical families/contracts remain required. Other mathematical
sections wait for the full numeric acceptance inventory to close.
