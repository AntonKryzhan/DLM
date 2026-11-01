# v0.91 — Finite algebra, tables and maps

This is finite computational algebra with exhaustive runtime law checking.
It supplies actual operations, laws, quotient constructions and maps, rather
than treating algebraic data types as mathematical groups or rings.
It does not add StaticProofs, kernel rules or general dependent/polymorphic
algebraic interfaces. Existing F2 homology/matrix coefficient domains are unchanged.

## Host API: `dlm_core::math::algebra`

Elements are labels `0..order`; identity/zero/one labels need not be zero or
one. An operation table is row-major `table[a * order + b] = a op b`.
Order must be 1..=64. Constructors validate order before shape multiplication,
all lengths/ranges before indexing, then all law instances. Inputs are owned
by callers until accepted; callers retain their input-allocation budgets.
Validated objects and maps have private invariant-bearing fields, including
the checked first-isomorphism report; accessors return read-only references.

| API | Checked behavior |
|---|---|
| `FiniteGroup::new`, `cyclic` | Closure, both identity laws, associativity, two-sided inverses |
| `multiply`, `inverse`, `is_abelian` | Domain checks, cached inverse lookup, commutativity |
| `subgroup` | Unique members, identity, product/inverse closure; sorted embedding |
| `quotient` | Normality by conjugation; canonical left cosets, operation and projection |
| `homomorphism` | Identity/product preservation; kernel and image as checked subgroups |
| `GroupHomomorphism::first_isomorphism` | Construct/recheck the bijection G/ker(f) -> im(f) with relabeling |
| `FiniteGroup::as_category` | Checked one-object groupoid through the existing Category constructor |
| `FiniteRing::new`, `residues` | Abelian additive group, unital associative multiplication, both distributive laws |
| `add`, `multiply`, `negate`, `homomorphism` | Domain checks; ring maps preserve zero, one, addition and multiplication |
| `FiniteRing::field`, `FiniteField::prime` | Nontrivial commutative ring with every nonzero element invertible |
| `FiniteField::inverse`, `divide` | Range validation; zero division is an error |
| `FiniteModule::new`, `regular` | Unital left action with zero laws and both distributive laws/associativity |
| `scale`, `homomorphism` | Range checks; maps preserve addition and scalar action over identical base-ring tables |

Rings may be noncommutative; the one-element zero ring is allowed and is not a
field. Fields need not have prime cardinality: a supplied table for F4 is
accepted, while Z/4 is rejected as a field. `FiniteField::prime` creates the
residue ring and checks the field laws, so composite orders are rejected.

Subgroup embeddings and cosets use sorted original labels. Quotient labels are
assigned by least unassigned ambient element; callers must use the explicit
projection rather than assume ambient labels remain quotient labels. The
first-isomorphism induced map targets the **image subgroup's new labels**, not
raw target labels. Normality is required; a nonnormal subgroup does not produce
a quotient group. Group maps need not be injective or surjective.

Ring/module map kernels/images are exposed as additive subgroup data, with
ambient tables retained. Closure under ring/scalar action follows from the
checked map laws, but quotient rings/modules, an Ideal API and a generalized
first-isomorphism API for those structures are not implemented here.

Module maps require the same concrete base-ring tables/labels. Isomorphic rings
with different labelings require an explicit transport, which is still open.
An additive map need not be module-linear: Frobenius on F4 is a ring
automorphism, but is not F4-linear as a map of regular F4 modules.

## Ordinary DLM libraries

`import std.finite_algebra` and `theory App extends StdFiniteAlgebra` expose
ordinary source records and functions. Public orders are **1..=8**, so exhaustive
reference checking remains bounded in the interpreter. No mathematical builtin
or hidden Rust call was added; these functions compile through existing Typed
Core and the C backend. Raw source records are descriptions, so public numerical
operations revalidate them and return `Result<..., Text>`. Predicates return
false on invalid/out-of-domain descriptions; check validity before interpreting
a negative property result. They are not predicates about all abstract groups.

| Source record | Fields |
|---|---|
| `FiniteGroup` | `order`, `unit`, `operation: List<Nat>` |
| `FiniteRing` | `order`, `zero`, `one`, `addition`, `multiplication` |
| `FiniteModule` | `order`, `zero`, `addition`, `action` |
| `FiniteSubgroup` | `group`, `embedding` |
| `FiniteQuotient` | `group`, `projection` |
| `FiniteGroupIsomorphism` | `quotient`, `image`, `induced` |

Source operations:

* `group_valid/check/cyclic/mul/inverse/abelian`;
* `group_subgroup/normal/quotient/homomorphism/kernel/image/first_isomorphism`;
* `ring_valid/check/residues/add/mul/neg/is_field/homomorphism/kernel/image`;
* `field_check/inverse` (field_check returns the validated ring description);
* `module_valid/check/regular/scale/homomorphism/kernel/image`.

The slash notation lists function-name suffixes: e.g. `group_check`,
`group_inverse`, `ring_is_field`, `field_inverse`. The source first-isomorphism
builder explicitly checks that the induced group homomorphism is bijective.
Source report records can be constructed/edited by a program and do not carry
proof authority; numerical operations still validate the underlying tables.

Run:

```text
dlm run --trusted-only --lib lib examples/valid/finite_algebra_demo.dlm
dlm build --trusted-only --lib lib -o algebra_demo examples/valid/finite_algebra_demo.dlm
```

## Prime-field scalars: `std.prime` / `StdPrime`

This source module supports canonical residues modulo primes p <= **251**:
`fp_add/sub/mul/neg/pow/inv/div`. All return Result errors for unsupported
moduli, out-of-range residues or zero inversion. Input Nat bounds are checked
before addition/multiplication, so huge user-supplied values cannot overflow
before being rejected. `fp_pow` allows exponents <=1024 and uses explicit
recursion fuel 2048; inverse uses a checked prime modulus and the exponent p-2.
No floating-point arithmetic or unchecked wrapping is used.

`fp_supported_prime` means "a supported prime in 2..=251". `fp_prime_test`
returns a primality result for 0..=251 and an **error** beyond that bound;
exceeding the bound never returns a false primality claim. This is a bounded
trial-division facility, not a general primality prover. Scalar support through
251 does not expand the table-order limits (8 source, 64 host), nor add non-F2
matrix/homology coefficients.

## Assurance, layers and limits

The accepted algebra/table properties are **runtime-checked finite laws**.
They do not create algebraic theorem passports, change optimizer permissions,
produce `.kproof` / `.f2proof`, or prove laws universally for infinite structures.
The five new closed source specifications use existing legacy finite-evaluation
certificates. This is separate from the independent F2 witness checker in 0.90.

Data tables contain only compact element labels on the host; law checks and
metadata live on whole algebra objects, not each table entry. Source reference
lists use ordinary boxed runtime values, not a new dense layout. The trusted
computational base includes exhaustive validators, construction algorithms,
Rust/DLM/C execution and the toolchain. There is no formal proof of their
soundness. The fixed order bounds limit finite enumeration, not wall-clock time.

Group validation is O(n^3); ring validation also checks all triples. Module
checking covers ring_order * module_order^2 and ring_order^2 * module_order
instances. Table construction, map validation and derived-structure rechecking
are bounded by the same fixed orders. There is no hidden unbounded proof search;
out-of-domain construction fails explicitly. Allocation/stack policies continue
to apply. Checks are repeated in source functions because ordinary records have
no protected/refined checked-state type.

The source checker now treats the second argument of `field(record, label)` as
metadata during dependency, free-variable and recursion walks. A field named
like its getter no longer creates false recursion; a `read_nat` field no longer
adds a phantom input effect. Actual calls in the record expression are still
checked, including real recursion and function-value restrictions. This is a
narrow repair, not completion of Resolved HIR or all metadata-name analysis.

## Evidence and open work

`finite_algebra.rs` exhausts every operation table/identity choice of orders 2
and 3 (59,081 candidates), compares acceptance to relabelings of cyclic groups,
and tests S3/A3, nonnormal rejection, kernels/images/first isomorphism, arbitrary
identity labels, F4, Z/n, M2(F2), unital ring maps and module scalar linearity.
`source_algebra.rs` compares all order-2 tables to the host, checks prime tests
through 251 with explicit out-of-bound errors, and compares source/host/C
behavior for quotients, F4, upper-triangular matrix rings, modules, invalid raw
records, huge inputs and metadata labels. Existing corpus tests check the new
library modules, their own closed specifications, and the executable example.

Open: infinite/presented groups and rings; generic algebraic interfaces and
formal law evidence; ideals/quotient rings/modules, module tensor products;
exact integers/rationals beyond machine bounds, non-F2 matrices/homology,
integer torsion, Tor/Ext, derived categories, HoTT and higher theories.
See `MATHEMATICS_PROGRESS.md` for the per-patch acceptance scope and remaining work.
