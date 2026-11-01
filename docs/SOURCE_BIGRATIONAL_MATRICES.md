# Budgeted BigRational matrices in DLM (0.95.0)

Since 0.97 imported `std.exact` scalar validation accepts 1025..16384-bit
profiles under SOURCE_EXACT_PRECISION.md. Axis bounds remain unchanged; the
0.95 acceptance below used <=1024-bit scalars. Wide matrix operations retain
the shared budget and may exhaust it. This numeric extension does not close
later matrix/profile/proof obligations.

## Scope and representation

`std.matrix_bigq` / `StdMatrixBigQ` extends `StdExact` with ordinary source
matrices whose coefficients are canonical `BigRational` records. Public axes
are <=4; scalar precision is the explicit std.exact profile, 1..1024 bits.
Private solving/inversion permits a <=4x8 augmented matrix; public returned
matrices retain axes <=4. This is a reference list/record implementation using
the existing checker, Typed Core and C runtime. It adds no matrix builtin,
compiler numeric type, native layout, FFI callback or proof-kernel rule.

`BigMatrix(rows:Nat,cols:Nat,entries:List<BigRational>)` is row-major. Raw
construction is not evidence of validity. `bmat_check` first checks the budget,
axes and exact entry count, then validates every coefficient with the new
public `bigrat_check`. Unreduced fractions, zero denominators, bad words, high
zero words, negative zero and out-of-profile values are errors. Shape checks
precede multiplication/allocation by dimensions. Empty shapes are explicit.
Private helpers cannot be called across imports or shadowed by inherited names.

The previous `std.matrix_q` 8-axis /32-bit scalar API and all Nat/Int semantics
remain unchanged. The 0.93 host rational matrix API still has axes <=32 and
its separate up-to-16384-bit profile. No implicit conversion between these
representations is introduced.

## Results and APIs

All public operations take the source `ExactBudget` as their first argument.
Success results carry the successor budget:

| Result | Fields |
|---|---|
| BigMatrixCalc | value:BigMatrix, budget:ExactBudget |
| BigMatrixReduced | matrix, pivots:List<Nat>, scales:List<BigRational>, odd_swaps:Bool, budget |
| BigMatrixRank | rank:Nat, budget |
| BigMatrixFlag | value:Bool, budget |
| BigMatrixSolution | particular:BigMatrix, kernel:BigMatrix, budget |

Scalar results reuse `BigRationalCalc`; text reuses `BigTextCalc`.

- `bmat_new/check/zero/identity/at` construct or validate matrices.
- `bmat_transpose/add/mul`, `bmat_is_zero`, `bmat_columns` provide arithmetic
  and selection. `bmat_is_zero` is fallible and budgeted, not an unchecked Bool.
- `bmat_rref/rank/kernel/image` compute normalized pivot data and bases.
- `bmat_det/inverse/solve` handle square determinants, inverses and general
  systems with multiple right-hand sides.
- `bmat_text` emits a `.qmat`-compatible `rows cols` header and canonical signed
  fraction tokens. Coefficient text conversion also spends the returned budget.

RREF uses ascending nonzero pivot columns, row swaps, normalized pivots and
subtraction of multiples. Kernel pivot coordinates are the negative free-column
coefficients. Image selects original pivot columns. RREF stores the original
pivot scales and swap parity; determinant multiplies them only on request.
This keeps determinant overflow from making an otherwise representable rank
calculation fail. A deficient square matrix has determinant zero; det(0x0)=1.

`bmat_solve(b,A,B)` returns P and K such that A*P=B and columns of K span ker(A).
The complete affine family is X=P+K*T, with T shaped nullity(A) by cols(B).
Inconsistent systems fail. Reduction pivots only the coefficient columns of
[A|B]. Zero-row systems have full homogeneous kernels; zero-column systems
require B=0. Empty square matrices have an empty inverse. A singular inverse
request fails, possibly through the inconsistent-system diagnostic.

Public consumers revalidate raw operands. Private operations use only values
already validated or produced by checked scalar operations. Index defaults in
private getters are not a public validation bypass.

## Budget/precision contract

Budgets from one coefficient/operand/cell are passed to the next; they are not
reset inside a calculation. Input validation, arithmetic and formatting all
thread their successor budgets. Error propagation preserves Result.err; it is
never replaced with a zero value or placeholder pivot. Operations do not expose
partly constructed output on error. Inputs and original budgets are immutable.

The source scalar reservation formula is unchanged from 0.94 and differs from
the host work counter. Matrix metadata traversal and known constant cloning
are bounded by the axis/cell limits; no additional global gas meter is claimed.
Known zero/identity construction can succeed at zero work, whereas later
validation/arithmetic may need reservations. Empty matrices still validate the
budget even though they contain no coefficients. Budget copying remains
possible; no linear ownership or uncopyable resource token is introduced.

Source helper loops have explicit fuel <=128 for matrices (<=32 private cells).
Scalar helpers retain their own word/text/gcd caps. Exact operations may return
`UnknownWithinBudget` for scalar precision or reservation exhaustion, including
intermediate expressions. This is not a claim that a mathematical matrix or
solution does not exist. Passing tests do not establish a formal wall-clock,
allocation or general algorithm bound.

## Programs and assurance

```sh
dlm run --trusted-only --lib lib examples/valid/bigrational_matrix_demo.dlm
dlm build --trusted-only --lib lib -o bigmatrix_demo examples/valid/bigrational_matrix_demo.dlm
```

The example computes determinant/inverse/rank with an entry 2^128 and solves
an exact fractional system. Interpreter and standalone C outputs agree.
Two closed computational specifications retain the existing legacy source
certificate path. Runtime outputs and raw result records are not StaticProof.

`Q_MATRIX_WITNESSES.md` adds a separate algorithm-independent runtime checker
for exact Q rank/full-kernel claims, solution residuals and inverses. It shares
the host exact scalar engine, not the DLM solver. No automatic source proof-term
elaboration or determinant certificate is claimed here.

## Validation and open work

Source/interpreter/host/C tests exhaust all 81 {-1,0,1} 2x2 matrices for rank,
RREF, kernel and determinant. Nine independent Python Fraction fixtures cover
empty/rectangular/3x3/4x4 shapes. Tests include fractional arithmetic/columns,
>128-bit inverse, a 1024-bit 1x1 determinant/inverse, multiple RHS, affine
kernels, inconsistent/singular systems, 4x8 private augmentation, exact budget
sequencing, raw invalid values/dimensions and private helper visibility.
CLI run/build exercise the new example. Full platform/regression results are
recorded in UPDATE and MATHEMATICS_PROGRESS.md.

Layers/trust (laws 1/3/4/24/28/40): ordinary source -> existing checker/Typed
Core/C; no optimizer authority or kernel rules. Bounds and machine arithmetic
(29/30/32), oracle/differential checks (39) and scoped acceptance (41/42) are
explicit. Source limb words still use safe bounded Nat operations; full
passports/proof objects do not enter coefficient word storage.

Open: larger source axes/precision, dense/generic limb buffers, sparse and
fraction-free algorithms, source witness synthesis/transport, determinant/RREF
certificates, formal algorithm proofs, rational chain homology, polynomials
and real/interval arithmetic. Proof-kernel/ownership and both deferred
application tracks retain their independent obligations.

## Native check configuration

The new example also passed GCC AddressSanitizer/UBSan with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie`,
`ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. It produced expected output with no diagnostics.
Leak detection was disabled; no general collector or leak-free claim follows.
