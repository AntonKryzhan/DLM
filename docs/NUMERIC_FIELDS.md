# Bounded exact rationals and oriented prime-field homology (0.92.0)

## Behavior and representation

`math::rational::Rational` is a canonical signed rational with separate sign,
u128 numerator magnitude and positive u128 denominator. Nonzero values are
reduced; zero is always positive 0/1. Fields are private. `new` normalizes;
`from_integer(i128)` accepts MIN without signed negation. Accessors are
`negative`, `numerator`, `denominator`; Display prints a signed `n/d`.

`checked_add/sub/mul/div`, `negated`, `reciprocal` and Ord are exact. A zero
denominator, zero divisor or reduced result outside the profile yields MathError.
There is no rounding, saturation, implicit arbitrary precision or claim of
closure as the whole field Q. Addition uses the denominator gcd, private exact
256-bit cross products, and cancellation before testing result bounds.
Comparison uses full-width products. The private division performs exactly 256
bit steps. Multiplication cross-cancels before checking u128 products.

`std.rational` is an ordinary source library, not a builtin. Its Rational record
has the same three fields but a smaller public profile: canonical numerator and
denominator <=4294967295. Raw records are revalidated. `rat_new(sign,n,d)` may
normalize any Nat inputs, then checks the reduced result against that profile.
The gcd has an explicit 256-step budget and fuel 512; exhaustion returns an
error. Public arithmetic on valid records uses products below 2^64 and sums
below 2^65, within the existing u128 Nat backend. It exposes `rat_valid/new`,
`rat_neg/inv/add/sub/mul/div`, `rat_equal`, `rat_cmp` (Result<Int,Text>, -1/0/1)
and `rat_text`. Invalid predicates return false; fallible operations return
Result errors. No change to existing Nat/Int arithmetic is made.

## Prime-field matrices

`math::prime::PrimeField::new(p:u16)` accepts only primes 2..251. Its private
modulus is returned by `modulus()`. `element` validates a canonical residue;
`reduce_integer(i128)` explicitly reduces it. Scalar `add`, `subtract`,
`multiply`, `inverse` validate operands. Zero has no inverse. u16 arithmetic
suffices for products of canonical residues (<=62500).

`math::prime::Matrix::new(field,rows,cols,entries)` validates canonical u8
residues and axes <=256. Matrix fields are private. APIs: `zero/identity`,
`field/rows/cols/entries`, `is_zero`, `transpose/add/mul`, `rref/rank`,
`kernel/image`, `select_columns/hstack/extend_basis/tensor`. Binary operations
require the same field even for 0x0 matrices. RREF normalizes each nonzero
pivot and subtracts multiples. Kernel pivot coordinates are the NEGATIVE of
RREF free-column coefficients. Image selects original pivot columns.

`base.extend_basis(candidates)` returns original candidate columns extending
the base span. A single private augmented reduction admits <=256 rows and
<=512 columns; only selected original columns leave it. Public concatenation
and all returned matrices still have axes <=256. Empty shapes preserve their
field; the kernel of 0xn is the identity of size n.

`std.matrix_fp` extends `StdPrime` using ordinary checked functions and records
`FpMatrix(prime,rows,cols,entries)` / `FpReduced(matrix,pivots)`. Its public axes
are <=16; private augmentation is <=16x32 (512 cells). Cell construction has
fuel 1024. It exports `pmat_valid/new/zero/identity/at`, `pmat_add/mul/transpose`,
`pmat_is_zero/rref/rank/kernel/image/columns/hstack/extend_basis`. The public
operations revalidate shapes, entries and primes before private arithmetic.
No source matrix tensor operation is added here. Source lists remain ordinary
lists: this is not dense-memory lowering or a performance claim.

## Oriented topology

`SimplicialComplex::chains_over(field)` uses the existing sorted vertex basis.
Omitting vertex j from an oriented simplex gives coefficient (-1)^j modulo p.
For F2 this agrees with existing unsigned boundaries. Over odd fields, replacing
all coefficients by +1 is incorrect; the filled-triangle test rejects it.

`math::prime_topology::ChainComplex::new(field,dimensions,boundaries)` checks
one boundary per degree, <=16 degrees, <=256 dimensions, fields and shapes,
d0 with zero rows, and every adjacent product d_(n-1)d_n=0. Empty complexes
retain their field. Simplicial input retains the existing limit of 256 total
simplices/256 facets and 65536 UTF-8 bytes.

`homology(n)` returns degree, Betti dimension and a column basis of cycles
modulo boundaries. `cohomology(n)` computes the dual quotient. `induced_ranks`
first checks each matrix, field, degree and commuting square of a chain map,
then computes the ranks on homology. These are ordinary unaugmented groups,
so a point has H0 dimension 1. Odd-field chain tensor products (including
Koszul signs) remain open; only the matrix tensor is implemented here.

`std.homology_fp` extends `StdMatrixFp`. `homology_fp(down,up)` and
`cohomology_fp(down,up)` accept adjacent explicit boundaries, check their
product and return `FpHomology(betti,cycles)`. The source library has no
simplicial parser, whole-chain wrapper or induced-map API in this patch.

CLI:

```sh
cargo run -p dlm_cli -- homology --prime 3 examples/math/projective_plane.simp
cargo run -p dlm_cli -- run --lib lib examples/valid/rational_prime_demo.dlm
```

The first command reports Betti numbers (1,0,0); with prime 2 they are (1,1,1).
Output identifies coefficients, runtime-checked assurance and weighted cycle
representatives. `homology <file>` still uses the old F2 API. The standalone
`.f2proof` export/check format remains F2 only.

## Validation, laws and trusted boundary

Tests include 4455 independently generated Python Fraction oracle vectors
(check-in TSV; Python is not needed for cargo tests), explicit wide cancellation,
MIN, overflow and zero cases; all 729 F3 2x3 and 625 F5 2x2 matrices with
independent enumeration of all solutions and kernel spans; all 512 binary 3x3
matrices compared to the prior F2 implementation; inverses of all nonzero
F251 residues; nonunit pivots, negative kernel coefficients, 256x512 private
augmentation, bad/empty/mixed-field matrices and rejected chain maps.

Circle, disk, sphere, torus and projective plane are checked over 2,3,5,251,
including cycle/cochain representatives. Differential source tests compare
ordinary DLM, interpreter, host computation and compiled C: 44 rational pairs,
all F3 2x2 matrices, F5/F251 cases, adjacent homology/cohomology boundaries and
invalid records/huge Nat arguments. CLI tests cover coefficient-sensitive
results and invalid/composite/out-of-profile moduli. Six closed source
specifications use the existing legacy finite-evaluation path.

Layers: host runtime computation/audit plus ordinary source -> existing
checker/Typed Core/C. Laws 1/3/4: no proof/passport data in dense entries;
29/32/40: explicit numeric/resource limits and errors; 30/39: differential
backend checks; 41/42: per-patch acceptance and retained open obligations.
This adds no compiler builtin, optimizer authority, kernel rule or independent
Fp/Q certificate transport. Runtime test coverage is not a general algorithm
proof; the existing legacy source certificates do not become independent
Nat-kernel proofs. ABI/layout, ownership and system APIs are unchanged.

Open obligations: arbitrary-precision rationals, Q matrices/homology,
integer homology/torsion, extension-field matrix coefficients, independent
Fp/Q witnesses and formal algorithm proofs, sparse/persistent computations,
source chain maps/parser/tensors, efficient dense buffers and regional
passports. General proof-kernel, refinement/SMT, ownership, generic types,
HoTT and higher mathematical milestones remain unfinished. Acceptance of this
bounded patch does not close these larger blocks. See MATHEMATICS_PROGRESS.md.

## 0.93 follow-up

`EXACT_RATIONAL_MATRICES.md` adds a separate limb-based runtime API with explicit
precision/work budgets and rational matrix algorithms. Ordinary `std.matrix_q`
retains the bounded source rational profile. The 0.92 APIs/profiles above are
preserved; general source arbitrary precision, rational homology and independent
Q/Fp witnesses remain open. See the current progress journal for status.
