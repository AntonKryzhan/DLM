# Finite computational mathematics (0.89 foundation, 0.90 source access)

This is an exact, bounded computational layer, not an implementation of all
mathematics and not an extension of the independent proof kernel.

## Access and assurance

* `lib/std/f2.dlm`: ordinary DLM functions over Bool (false=0, true=1),
  addition, multiplication, negation, partial inversion via Option; four laws
  checked by the existing Boolean finite-domain/normalization checker.
* `dlm_core::math::f2`: Rust library API for dense F2 matrices.
* `dlm_core::math::topology`: Rust library API for chain complexes,
  homology/cohomology, chain maps and total tensor products.
* `dlm_core::math::category`: Rust API for finite categories, functors and
  natural transformations checked by finite enumeration.
* `dlm homology examples/math/circle.simp`: CLI computation from generating faces.

Since 0.90, `std.matrix_f2` and `std.homology_f2` provide ordinary DLM reference
implementations over records/lists, including matrix operations and homology of
explicit boundary matrices. Their public axis bound is 32 (host bound: 256).
Simplicial parsing and categories remain host APIs. There are no opaque matrix
language types or hidden mathematical builtins. See `MATH_SOURCE_AND_WITNESSES.md`
for the source API, internal augmentation bound and independent runtime witnesses.

Matrix/topology/category results have runtime-checked computational assurance. They do not produce
StaticProof, `.kproof`, or new kernel rules. The trusted computational base is
these Rust algorithms and their execution environment, not merely a fingerprint.
No compiler optimization consumes these results as formal evidence.
The four scalar DLM theorems use the existing certificate path (legacy-trusted),
not the independent Nat-equality kernel; their checks include all Boolean cases.

## Semantics

Matrix constructors reject invalid residues and storage shapes. Private data
preserves these invariants. Addition is XOR, multiplication is AND/XOR;
row reduction is deterministic and uses no numerical approximation. Image
bases select original pivot columns. Nullspace bases use ascending free columns.
Kronecker products check output shapes before allocation.

A chain complex is finite and unaugmented: d_0 has shape 0 x dim(C_0).
Construction checks every shape and d_(n-1) d_n = 0. Homology extends an image
basis with nullspace columns and returns the newly independent columns as
representatives. Thus boundaries are removed by linear independence, not by
just reporting a dimension difference. Cohomology dualizes the complex and
returns cocycles in the original dual basis. A chain map is checked for shape
and commutation in each degree before ranks of induced homology maps are computed.

The tensor complex uses direct sums in ascending first-factor degree. Over F2
the sign in d(x tensor y) is trivial. Construction rechecks the chain law.
This is a chain-level tensor product, not a triangulation of a product space.

Generating simplices use u32 vertex labels, one simplex per line, whitespace
separated. `#` begins a comment. Input faces may be unsorted; closure inserts
all nonempty faces and deduplicates them. Repeated vertices in a face are errors.
The empty input is the empty complex, with no reported degrees. This is
ordinary homology: a point has b0=1. No torsion or integral homology is inferred
from F2 output. A filled triangle differs from its boundary.

Category composition is `compose[g * arrows + f] = g after f` and is defined
exactly for composable pairs. Validation checks endpoints, both identity laws
and every associativity equation. Functors preserve endpoints, identities and
all compositions; natural transformations check every naturality square.
Empty categories are allowed. This is ordinary finite category theory, not
infinity categories, topoi, or a proof of general categorical laws.

## Resource model and architectural boundary

Matrices: at most 256 rows and 256 columns, including intermediate concatenations.
Complexes: at most 256 total simplices, 256 generating faces, 16 chain degrees.
Text input: 65536 UTF-8 bytes; the CLI caps its read before parsing.
Finite categories: at most 64 objects and 64 arrows; associativity uses a
bounded cubic enumeration. Violations return MathError; results are never
silently truncated. These fixed bounds are a deterministic resource policy,
not a wall-clock guarantee. Allocation failures retain ordinary Rust semantics.
Public APIs require already allocated input vectors; callers own their input
allocation budgets. Dense entries contain no passports or proof metadata.

This module is runtime computation plus audit output (laws 1, 3, 4, 5, 39, 40).
0.90 source libraries use existing checked record types, ordinary function
contracts and explicit runtime shape checks; they do not bypass checker gates.
Open obligation: efficient dense builtins need shape/capability checks, backend
contracts and regional passports. Ordinary source lists are not dense buffers.

## Verification and scope remaining

Tests exhaust all 512 binary 3x3 matrices and independently enumerate kernels.
Topological cases include point, disconnected points, circle, disk, tetrahedral
sphere, periodic 3x3 torus and RP2 (coefficients F2). Tensor examples check the
circle product and circle x disk. Tests reject malformed boundaries, maps,
inputs, size violations and category/functor/naturality law failures.

0.90 adds independent runtime rank/homology witnesses (see the new witness
specification); formal proof terms for general algorithms still remain open.
Also open: general groups,
rings and modules; integral/rational matrices and torsion; sparse storage;
persistent homology; Tor/Ext and derived categories; spectral sequences;
dependent types, HoTT/univalence/higher inductive types; infinity categories;
topoi, motives, Langlands, derived geometry and mathematical quantum field theory.
None is represented by a placeholder type claiming implementation.
The 0.88 proof-kernel obligations (definitions, substitution, lemmas and
induction witnesses) remain open and are not closed by this computational module.

## v0.91 algebra extension

`FINITE_ALGEBRA.md` adds finite table-based groups, rings, fields, modules and
maps in host/source APIs. General algebra, formal law proofs and non-F2
matrix/homology coefficients remain open. The per-patch scope/remaining work is
tracked in `MATHEMATICS_PROGRESS.md`.

## v0.92 numeric extension

`NUMERIC_FIELDS.md` adds bounded rational scalar arithmetic and prime-field
matrix/homology computations with oriented boundaries. General Q matrices,
integral torsion and non-F2 independent witness transport remain open. The
historical F2 limits/API above are preserved.
