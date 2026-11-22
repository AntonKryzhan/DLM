# v0.90 — DLM matrix libraries and independent F2 witnesses

## Source API

`import std.matrix_f2` / `theory App extends StdMatrixF2` exposes ordinary DLM
functions and records. `std.homology_f2` / `StdHomologyF2` inherits this API.
No matrix/topology builtin or new opaque runtime type was added. All source
functions go through ordinary type checking, function contracts, nominal record
schemas, Typed Core, interpretation and the existing C backend.

| API | Result / meaning |
|---|---|
| `F2Matrix(rows, cols, entries)` | Row-major `List<Bool>`, false=0, true=1; raw record, not a shape proof |
| `mat_new`, `mat_zero`, `mat_identity` | `Result<F2Matrix, Text>` constructors |
| `mat_valid` | Shape and public-axis predicate |
| `mat_at` | Checked entry lookup, `Result<Bool, Text>` |
| `mat_add`, `mat_mul`, `mat_transpose`, `mat_tensor`, `mat_hstack` | Exact F2 operations, `Result<F2Matrix, Text>` |
| `mat_rref` | `Result<F2Reduced, Text>`; reduced matrix and ascending pivot columns |
| `mat_rank` | `Result<Nat, Text>` |
| `mat_kernel`, `mat_image` | Column bases in the original chain/vector bases |
| `mat_columns(a, columns)` | Checked selection, order and duplicates preserved |
| `mat_extend_basis(span, candidates)` | Candidate columns extending the span modulo its existing image |
| `mat_is_zero`, `mat_equal` | Predicates; malformed records return false |
| `homology_f2(down, up)` | `Result<F2Homology, Text>`, with `betti` and `cycles` |
| `cohomology_f2(down, up)` | Same result type, with dual cocycle representatives |
| `chain_valid_f2(ds)`, `chain_homology_f2(ds)` | Validate / compute a finite unaugmented chain complex |

All public numerical operations validate raw records before indexing or doing
shape arithmetic; directly constructing a malformed `F2Matrix` does not bypass
these checks. Helpers are private. Oversized Nat dimensions/indices return an
error without first overflowing a product or sum. A `Result` error is propagated,
not silently converted into a successful empty matrix. Predicates intentionally
return false on malformed input; they do not distinguish failure reasons.

`down: C_n -> C_(n-1)`, `up: C_(n+1) -> C_n`. Homology requires compatible shapes
and `down * up = 0`, then selects independent kernel columns modulo the incoming
image. Basis extension reduces the augmentation `[up | kernel(down)]` once;
retained pivots after the incoming columns identify representatives in the
original kernel. There is no repeated full-rank search for each candidate.
For a chain list, `ds[n] = d_n`, and `d_0` has zero rows. Empty chains are allowed.
There is no simplicial text parser inside the source library: pass explicit
boundary matrices. The host CLI still constructs them from `.simp` input.

Public source axes are at most **32**, chain degrees at most **16**. The private
augmentation may have 32 rows and 64 columns (2048 entries); the result again
satisfies the public bound. The tabulator has explicit recursion fuel 4096;
other recursion measures decrease a bounded Nat or a finite list. These are
reference algorithms using ordinary lists/records and boxed C runtime values,
not a dense numerical backend or a throughput claim. Input lists are allocated
by callers; this API bounds its own validated computations. Runtime stack,
allocation and operational fuel policies continue to apply.

Run:

```text
dlm run --trusted-only --lib lib examples/valid/matrix_homology_f2.dlm
dlm build --trusted-only --lib lib -o matrix_demo examples/valid/matrix_homology_f2.dlm
```

## Result patterns and IR guards

`match value { ok(x) => ..., err(e) => ... }` supports either constructor order,
catch-all arms and nested matches. Patterns must be exhaustive, disjoint and
reachable; the payload types are checked, including error payloads. A complex
scrutinee is evaluated once. Parser-generated `__match_ok` / `__match_err`
projections are reserved from user source and implemented by both runtimes.
The compiled builtin signature/mutation table now covers both projections.

The IR verifier additionally requires a dominating guard on the **same immutable
slot/global** for all four partial match projections: nonempty list, some, ok,
err. It derives local facts from `is_empty`, `is_some`, `is_none`, `is_ok`,
`is_err`, negation, and the supported short-circuit/branch contexts. Facts are
scoped to branches and lexical slot lifetimes, reset for each function, and do
not leak out of matches. A swapped branch, wrong subject, removed test or
hoisted projection is rejected even when both Result payloads have the same
type. This is a local dominance check, not a general refinement solver or a
proof of arbitrary IR transformations. Future passes must preserve recognizable
guards or provide an expanded checked representation. There is no IR format
version bump: the IR grammar is unchanged; backend builtin support is extended.
Old backends must reject unknown internal operation names.

## Portable computation witnesses

```text
dlm homology-export examples/math/torus.simp torus.f2proof
dlm homology-check torus.f2proof
dlm homology-check torus.f2proof examples/math/torus.simp
dlm homology-check examples/math/circle.f2proof
```

`homology-export` writes a validated witness. `homology-check` is standalone:
source files, source evaluation, proof search and Gaussian elimination are not
needed. Optional `.simp` input binds the artifact to independently supplied
boundary matrices. Without it, the verified claim is **the embedded boundary
matrices**, not an authenticated external file, vertex-label provenance or an
assertion about a named original space. An entirely different valid artifact
can prove an entirely different embedded claim; hashes/signatures are not used
as proof or authentication.

The Rust API is `math::witness::{certify_rank, check_rank, certify_homology,
check_homology, check_homology_against, encode_homology_certificate,
decode_homology_certificate}`. The decoder checks syntax and limits only;
call a checker before using decoded claims. Witness fields are public so
callers can supply witnesses from independent producers.

A rank witness provides `R`, `U`, `U_inverse` and pivot columns. The checker
verifies `U A = R`, both inverse equations, and the reduced-echelon pivot/zero-row
shape of `R`. It does **not** compute a row reduction. A homology witness checks:

1. All boundary shapes and `d_(n-1) d_n = 0`.
2. Independently witnessed outgoing/incoming ranks `r_out`, `r_in`.
3. Exactly `dim(C_n) - r_out - r_in` proposed representatives, all cycles.
4. A rank witness showing their independence modulo an incoming image basis.

These finite field identities justify a complete quotient basis, rather than
only a list of cycles or a claimed Betti number. The producer lives in a separate
module; the checker never calls `rank`, `rref`, `kernel`, `image` or `homology`.
Its trusted base includes the F2 dimension argument implemented by the checker,
private validated Matrix storage, elementary matrix multiplication/selection,
Rust execution and the toolchain. Low-level matrix primitives are shared with
the producer; this is algorithm-independent checking, not a second implementation
of every scalar operation or a formal soundness proof.

The assurance is **independent-runtime-witness**. No new StaticProof,
Nat-kernel rule, theorem passport, optimizer permission, or `.kproof` is created.
The source library itself is differentially tested, not formally proved correct
by these host-generated artifacts.
Five closed source specifications (empty/identity rank, oversized shape, a point
and an invalid boundary square) use existing finite-evaluation certificates,
explicitly legacy-trusted. They are not universal algorithm-correctness theorems. General proof-kernel definitions, substitution,
lemmas, logic and induction witnesses remain open.

## Format and budgets

`.f2proof` has header `DLM-F2-HOMOLOGY v1`. In order: `boundaries N`, N matrices,
`groups N`, then each `group degree`, its representative matrix and three rank
witnesses (outgoing, incoming, quotient). A rank witness is `rank K p0 ...`, then
three matrices (`R`, `U`, `U_inverse`). Final token: `end`. A matrix is
`matrix rows cols bitstring`, or `-` instead of the bitstring for zero entries.
Unsigned integers, binary payloads, counts, shapes, pivot order, EOF and trailing
tokens are checked. It is a separate format from `DLM-KERNEL-PROOF`.

Host axes remain 256 and degrees 16. Default WitnessBudget: 100,000,000
conservative scalar multiply-add positions, 256 serialized matrices, 16 MiB
text. Products are charged before execution, counting zero positions too.
Input bytes/counts are checked before their corresponding allocations; CLI
reads are capped before UTF-8 decoding. Output text is bounded before it is
returned/written; temporary serialization allocation is bounded by the fixed
matrix/degree domain. The budget is not a wall-clock guarantee. A mathematically
valid large artifact may exceed the default budget; Rust callers may supply a
larger explicit budget. Such exhaustion is `UnknownWithinBudget`, never false
or proved. Invalid identities/format return `Invalid`; no partial checked result
is returned. Producer search is bounded by the fixed matrix/degree domain; the
scalar verifier budget is not a producer work/fuel budget.

## Validation and remaining work

`source_math.rs`: all 64 binary 2x3 matrices compared against host ranks/bases/
products and C execution; nonzero tensors/selections, zero shapes, malformed
records and oversized inputs; circle, disk, sphere, torus and RP2 homology and
cohomology compared across DLM, Rust and C.
`math_witness.rs`: 512 binary 3x3 rank witnesses, maximum axis, all graphs on four
vertices, round trips, every single payload-bit mutation of a circle artifact,
false cycles, boundaries substituted for homology classes, forged inverse/rank
claims, external-claim mismatch, truncation, malformed payloads and exhaustion.
`result_patterns.rs`, `ir_verify.rs` and CLI tests cover typing, evaluation order,
C parity, guards, signature mutations, export/replay after source deletion and
binding to external boundaries.

Dense source/backend bindings with regional passports, sparse/integer/rational
matrices, torsion, persistence, generic algebraic structures, Tor/Ext, derived
categories, spectral sequences, HoTT and higher theories remain open. The new
libraries and witness checker do not close those mathematical programs or all
of stages B/C.
