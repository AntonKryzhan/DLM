# Independent exact Q rank and kernel witnesses (0.95.0)

## Claim and trusted boundary

`math::rational_witness` checks finite exact rational rank and full kernel basis
claims without calling Gaussian elimination, RREF, rank search, solve or inverse.
The producer is a separate module using the existing host solver; it is not
trusted by the checker. The codec is separate again. Acceptance is an
algorithm-independent runtime witness, not a Nat-kernel StaticProof or a general
formal proof of scalar arithmetic/compiler correctness.

The trusted numerical base includes private-invariant exact rationals, bounded
matrix shape checks and exact matrix multiplication/identities. It is shared
with the host arithmetic engine. Independence of the result-checking algorithm
does not mean an independently implemented integer arithmetic engine, external
axiom proof or verified machine-code execution. No compiler optimization
permission is derived from these witnesses. Public witness/report records alone
are data; callers must actually run the checker to accept a claim.

## RankCertificate and verification equations

For A of shape m x n and claimed rank r, a certificate embeds A and six matrices:

| Field | Shape | Meaning |
|---|---|---|
| columns C | m x r | rank factorization |
| coordinates R | r x n | A=C*R |
| left L | r x m | lower-bound identity |
| right U | n x r | L*A*U=I_r |
| kernel K | n x (n-r) | claimed full nullspace basis |
| kernel_left V | (n-r) x n | V*K=I_(n-r) |

The verifier checks rank bounds, every shape/profile and these exact equations:

1. A=C*R gives rank(A)<=r.
2. L*A*U=I_r gives rank(A)>=r.
3. A*K=0 places each kernel column in ker(A).
4. V*K=I_(n-r) makes the n-r columns independent; rank-nullity then makes
   them a complete basis of ker(A).

C need not be literally a subset of A's columns for the proof; the current
producer chooses original pivot columns. K may be any basis satisfying the
equations. No free-variable/RREF ordering is trusted. Zero-row/zero-column,
rank-zero and full-rank cases use the actual empty identity/matrix shapes.

`check_rank(certificate,budget)` returns rank, nullity, kernel and work spent
inside verification. `check_rank_against(expected,certificate,budget)` also
requires equality with the caller's expected matrix. Without that argument,
the claim is about the EMBEDDED matrix, not an unrelated source file.

`check_solution(certificate,B,P,budget)` checks the rank/full-kernel claim and
A*P=B; the accepted solution family is P+K*T for all suitably shaped T.
`check_inverse(A,X,budget)` checks square shapes and BOTH A*X=I and X*A=I.
These helpers also avoid elimination. Solution/inverse helper claims currently
use API values rather than a separate serialized solution/inverse format.
No determinant-value or full RREF certificate is implemented in this version.

## Resource/error semantics

Host axes remain <=32. Every operation uses a shared caller-owned ExactBudget
(1..16384-bit scalar profile and explicit arithmetic work). The CLI fixes
4096 bits and 100,000,000 work units for its whole read/parse/produce/check/
encode or decode/check calculation. API callers may choose other supported
profiles. A valid witness may need more precision/work than the chosen budget;
UnknownWithinBudget does not mean the mathematical claim is false.

Errors have distinct `Invalid` and `UnknownWithinBudget` kinds. Structural,
format or identity failures are invalid within this format; precision/work/text
exhaustion is unknown within budget. Public Matrix/Rational storage already
enforces canonical values; the checker separately enforces the supplied profile
for all witness matrices, including those with empty products. No partially
accepted CheckedRank is returned on failure. Budget consumption retains host
ExactBudget semantics, distinct from source reservations.

Text limit is 1 MiB for the entire certificate. The encoder uses a bounded
writer and stops instead of building an arbitrarily large output. The decoder
checks byte length before tokenizing and dimensions before products/allocation.
Scalar parse limits and the shared arithmetic work budget also apply. Display
work is bounded by matrix/bit/text ceilings; this is not a hard timing or memory
certificate. Caller-allocated API arguments retain their allocation obligation.

## Standalone text format

`DLM-Q-RANK v1` is whitespace-tokenized with this strict field order:

```text
DLM-Q-RANK v1
rank r
matrix input m n <m*n integer/fraction tokens>
matrix columns m r <m*r tokens>
matrix coordinates r n <r*n tokens>
matrix left r m <r*m tokens>
matrix right n r <n*r tokens>
matrix kernel n n-r <n*(n-r) tokens>
matrix kernel_left n-r n <(n-r)*n tokens>
end
```

The symbols m/n/r above explain dimensions; actual files contain unsigned
ASCII integers <=32. Each matrix has exactly rows*cols tokens. Fractions are
parsed as exact values and normalized; an alternate spelling of the same value
is not a forged numerical claim. Labels/order/version/end are mandatory; no
trailing tokens, comments, duplicate/unknown fields or implicit missing matrices.
The format is distinct from .kproof and .f2proof and does not upgrade their trust.

CLI:

```sh
dlm matrix-q-export examples/math/rational_rank.qmat result.qproof
dlm matrix-q-check result.qproof examples/math/rational_rank.qmat
dlm matrix-q-check result.qproof
dlm matrix-q-check examples/math/rational_rank.qproof examples/math/rational_rank.qmat
```

Export validates the produced certificate before encoding/writing. Check runs
without the source matrix file; the optional expected input binds the claim.
Success identifies rank/nullity, binding and independent-runtime-witness
assurance with StaticProof:no. Failures emit no success report and return
nonzero. File writes retain ordinary filesystem error semantics.
The checked-in small .qproof is an explicit hand-constructed fixture with a
simple kernel left inverse, independent of the producer's choice.

## Validation and remaining work

Tests cover all 81 {-1,0,1} 2x2 matrices, all 50 prior independent Fraction
fixtures, transport roundtrip and external mismatch, zero dimensions and
maximum 32-axis identities, >128-bit inverse identities and affine solution
residuals. All 28 single-entry +1 mutations across the seven matrices of a
rank-one fixture are rejected; forged rank, missing kernel columns, truncation,
wrong versions/labels/counts, trailing data, precision/work and both encoding/
decoding text caps are also tested. A source guard prevents adding direct
solver calls to the verifier module. CLI replay after source deletion is tested.
This validates the cases/architecture checked; it is not a general soundness
proof. Final regression/platform details are in UPDATE and the progress journal.

Open: general scalar/proof-kernel integration, source-level witness synthesis,
serialized solution/inverse/determinant/RREF certificates, Q homology witnesses,
extension-field/integral coefficients, and formal proofs of algorithms and
resource models. These witnesses close only the declared finite Q rank/kernel
and residual-checking scope. Broad mathematics and deferred applications remain
open; no proof/ownership/generic milestone is declared complete.
