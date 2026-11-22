# Exact limb arithmetic and rational matrices (0.93.0)

## Changed behavior and numerical contract

`math::exact` adds separate runtime types `Natural`, `Integer`, `Rational` and
`ExactBudget`. It does not change the source `Nat`/`Int` u128/i128 contract or
the existing 0.92 rational profiles. Limbs are private little-endian u32 words;
zero has no limbs, signs of zero are positive, and rational denominators are
positive/coprime with their numerators. All invariant-bearing fields are private.

The representation supports precision beyond 128 bits. The caller explicitly
chooses 1..16384 bits for each reduced magnitude/denominator. Default: 4096 bits
and 50,000,000 work units. The current safety ceiling is 16384 bits; this is a
configurable-precision implementation, not unlimited memory or unrestricted
mathematical arithmetic. Raising that ceiling requires another resource review.
No external Rust dependencies or implicit source numeric conversions are added.

`Natural` exposes `zero/from_u128/parse`, `is_zero/bit_len/to_u128`, exact Ord,
`checked_add/sub/mul/gcd`, and `div_rem`. Subtraction reports a negative-result
error rather than silently using monus. `Integer` exposes `from_i128/parse`,
`negative/magnitude/negated`, `checked_add/sub/mul/cmp`, and Euclidean `div_rem`:
0 <= remainder < abs(divisor), for all sign combinations. i128::MIN is accepted
without negating it in the signed machine domain.

`Rational` exposes `new(sign,numerator,denominator,budget)`, `parse`,
`zero/one/from_integer`, `negative/numerator/denominator/is_zero/negated`,
`reciprocal`, `checked_add/sub/mul/div/cmp`. Text is strict ASCII decimal with
optional leading minus; fractions use a positive unsigned denominator. Plus,
spaces within a token, decimal points and denominator signs are rejected.
Display is normalized signed `n/d`; integers display as signed decimal.
Natural parsing accepts leading zeros; canonical outputs remove them.

Addition uses denominator gcd and exact signed cross products; multiplication
cross-cancels first. Private intermediates may reach 2*max_bits+2 before reduced
result checks, so a representable reduced sum is not rejected just because its
cross product exceeds the public precision. Decimal input components must each
fit the requested precision before normalization. A smaller output does not
make an over-budget input admissible. Zero divisors/denominators are domain
errors. Finite precision or work exhaustion returns MathError starting with
`UnknownWithinBudget:`; it is not mathematical falsehood or a proof of failure.

## Resource accounting

The budget is caller-owned and shared across an entire calculation. It records
max_bits, remaining_work and work_used. Work charges cover decimal input,
limb arithmetic/comparisons, binary long division, Euclidean gcd and matrix
traversal. Exhaustion consumes the remaining work and stops; successful earlier
operations are not rolled back. Operands are immutable and partial numerical
results never escape an error. Callers may deliberately start another budget.

Multiplication uses bounded schoolbook limbs; division uses binary long division;
gcd uses Euclid. Intermediate sizes are bounded as above; source matrix text is
capped before tokenization. Shape checks precede dimension products/allocation.
Work units are a deterministic implementation-level policy, not a wall-clock
or formal complexity proof. Allocator failures keep ordinary Rust semantics.
Display and simple immutable accessors do not spend a budget, but their data
sizes have the hard bit/axis ceilings. Public vector arguments are already
allocated by the caller, who retains their input-allocation obligation.

## Host rational matrices

`math::rational_matrix::Matrix` validates shape, entries and supplied precision.
Public axes are <=32. Every fallible method takes `&mut ExactBudget`:
`new/zero/identity/parse`, `transpose/add/mul`, `rref/rank/kernel/image`,
`select_columns`, `determinant/inverse/solve`. Accessors are rows/cols/entries;
`is_zero` is an exact predicate on canonical entries.

RREF normalizes nonzero pivots, subtracts multiples and selects deterministic
ascending pivot columns. Kernel columns use the negated free-variable
coefficients; image columns come from the original matrix. Determinant is the
signed product of the original elimination pivots (0 for deficient rank);
det(empty 0x0)=1. Inverse validates a square nonsingular matrix. `solve(A,B)`
accepts multiple right-hand sides, rejects inconsistent systems, and returns
`Solution { particular, kernel }`. All solutions are particular+kernel*T for
an appropriately shaped T. A zero-row system has the full kernel; a zero-column
system requires B=0. Empty shapes preserve their actual dimensions.

Solving/inversion uses a private augmentation <=32x64. Public returned matrices
still have axes <=32. Any intermediate arithmetic step may exhaust the selected
precision/work even when a different algorithm could finish with that budget;
no complete solver under all resource profiles is claimed. Symbolic parameters,
polynomial coefficients, rational chain homology and independent result
certificates are not part of this API.

## DLM source and native C

`std.matrix_q` extends `StdRational` through ordinary DLM functions and records:
`QMatrix(rows,cols,entries:List<Rational>)`, `QReduced(matrix,pivots)`.
It retains the existing source rational 32-bit canonical profile and caps axes
at 8 (64 cells). Internal cell construction has fuel 256. `qmat_new/valid`,
`qmat_zero/identity/at/transpose/add/mul/is_zero`, `qmat_rref/rank/kernel/image`,
`qmat_columns` validate raw records and propagate every scalar error through
Result. No arithmetic error is replaced by a zero matrix or a fake pivot.

These are ordinary source lists/records, not dense buffers or new builtins.
Source arithmetic checks every scalar intermediate; a matrix operation may
return a profile error even if its final mathematical result would fit.
The host limb types are not yet ordinary arbitrary-precision DLM source types.
Source determinant/inverse/solve, configurable source precision, corresponding
native layouts and independent Q witnesses remain open.

Two new closed source specifications use the existing legacy finite-evaluation
certificate path. They are not independent kernel proofs of the algorithms.
`examples/valid/rational_matrix_demo.dlm` demonstrates rank, signed rational
kernel, product with the kernel and explicit overflow failure.

## CLI and strict text format

```sh
dlm matrix-q --inverse --solve examples/math/rational_rhs.qmat examples/math/rational_system.qmat
dlm matrix-q --bits 512 --work 10000000 examples/math/rational_system.qmat
dlm run --lib lib examples/valid/rational_matrix_demo.dlm
```

Input: first nonempty, noncomment line is `rows cols`; remaining whitespace
separated tokens must be exactly rows*cols integers/fractions. `#` starts a line
comment. The CLI caps each file read at 262144 UTF-8 bytes (including streams),
then parses with the shared budget. Extra/missing entries, oversized shapes,
invalid syntax and zero denominators fail explicitly. Default bits/work are
as above. `--inverse` and `--solve rhs.qmat` are optional. Output contains
coefficient domain, precision, runtime assurance, rank, RREF, kernel, square
matrix determinant, requested inverse/solution and consumed work. Success
output is buffered until all requested operations finish. A failure prints
one diagnostic, returns nonzero and emits no partial success report.

## Acceptance, laws and unfinished work

Independent Python fixtures: 6426 exact integer/Fraction vectors, including
random multi-limb carries/borrows, division/gcd and >128-bit cases; 50 rational
matrix fixtures use Fraction RREF and an independent Leibniz determinant.
Cargo reads checked-in TSV; it does not require Python. Host tests also exhaust
625 integer 2x2 matrices, verify inverse identities, all sign combinations of
small Euclidean division, 16384-bit boundaries, shared budgets, zero/empty
shapes, general solution residuals and inconsistent systems.
Source/interpreter/host/C checks exhaust all 81 {-1,0,1} 2x2 matrices and cover
fractional pivots, constructors, scalar-error propagation and bad raw records.
CLI tests cover solutions, wide entries, explicit limits and silent-on-failure
stdout. This patch adds 18 tests; the final workspace result is in the progress
journal/UPDATE. Tests validate the declared cases; no general soundness proof
or Windows result is inferred from them.

Layers/laws: host runtime computation and ordinary source -> existing
checker/Typed Core/C (1/3/4/30); explicit precision, fuel and work (29/32/40);
reproducible oracle/differential tests (39); per-patch acceptance and retained
obligations (41/42). Existing proof modes, trust/capabilities, IR grammar,
Nat/Int representation and ABI are preserved. No optimizer authority is granted
by these runtime results. B/C/ownership/generic milestones remain unfinished.

Open: configurable precision in DLM source/native C, dense/generic buffers,
Q algorithm certificates and formal proofs, rational homology, fraction-free/
sparse algorithms, symbolic algebra and real/interval arithmetic. Deferred
positive geometries and symbolic huge-number work are separately recorded in
FUTURE_MATH_APPLICATIONS.md; they are not implementations in this patch.

## 0.94 source integration

`SOURCE_EXACT_ARITHMETIC.md` adds ordinary `std.exact` integers/rationals with
explicit source budgets and precision 1..1024 bits, compiled through the existing
C backend. This is separate from the 16384-bit host profile and its limb-work
counter. Source BigRational matrices and independent Q certificates remain open;
`std.matrix_q` above retains its original scalar profile.

## 0.95 result-checking follow-up

SOURCE_BIGRATIONAL_MATRICES.md adds ordinary <=4-axis source BigRational
matrices on the explicit 1024-bit source profile. Q_MATRIX_WITNESSES.md adds
separate algorithm-independent host rank/full-kernel certificates, inverse/
solution residual checking and standalone transport. Determinant/RREF and
general source proof terms remain open; original APIs/profiles above persist.
