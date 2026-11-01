# TEST_MATRIX.md — DLM / ЯРД MVP Test Matrix

## v0.100 checked declaration identity (B)

```text
cargo test -p dlm_core --test declaration_identity --test typed_core --test record_schemas --test source_identity --test modules --test ir_verify --test mutual_recursion --locked --offline
cargo test -p dlm_cli --test cli_declaration_identity --locked --offline
cargo test --workspace --locked --offline --no-fail-fast
```

Acceptance: clone/reparse/concurrent declaration IDs; resolver propagation;
nominal types resolved by the checker; legacy name/display maps cannot supply
lowering facts; missing/swapped/stale declarations, duplicate IDs, changed
contexts, failed checks and bad schema order are rejected. Independent function
reordering retains ID-bound signatures. Inheritance, private helpers, named
callbacks and lambdas preserve identity. Damaged IR declaration origins fail
verification. The CLI example compares native/interpreted output `7`.
`DECLARATION_IDENTITY.md` states the bounded handoff and remaining HIR work.

## v0.99 source identity and physical diagnostics (A/B)

```text
cargo test -p dlm_core --test source_identity --test diagnostic_spans --test typed_core --test resolver_ids --locked --offline
cargo test -p dlm_cli --test cli_source_locations --locked --offline
cargo test --workspace --locked --offline
```

Acceptance: UTF-8 byte columns, CRLF, multiline expressions and masked literals;
independent linked modules, dependency diagnostics and inherited source origins;
checked facts/record schemas, generated match nodes, lambdas and initializers;
unknown/duplicate file IDs, invalid spans and mismatched source origins rejected
by `verify_ir`. CLI checks parse/check/build locations and an imported native
program producing `42`. `SOURCE_IDENTITY.md` defines the guarantee boundary.

## v0.88 record schemas and independent equational proof replay

```text
cargo test -p dlm_core --test record_schemas --test equational_kernel --locked --offline
cargo test -p dlm_cli --test cli_proof_replay --locked --offline
cargo test --workspace --locked --offline
```

Record tests: nested/inherited schemas, separate same-named declarations, field arity/order/types, projection
layout/index/result, unknown/unresolved/cyclic IDs and metadata corruption. Both new examples must compile and
agree with the interpreter in the C backend corpus.

Proof tests: twelve Nat algebra laws plus equality composition, independent finite evaluation, serialization
round trips, invalid rules/premises/goals/scopes, externally declared axioms and reordered proof tables, all
budget classes, excessive DAG text expansion, parser mutations/fuzzing and certificate forgery after rehash.
CLI export/check succeeds after source deletion and rejects a modified artifact or a legacy-only export.

## v0.87.1 typed IR verification

```text
cargo test -p dlm_core --test ir_verify --locked --offline
cargo test -p dlm_core --test typed_core --test compile_c --locked --offline
cargo test --workspace --locked --offline
```

Six regression groups: all 54 compiled builtins (valid source lowering, corrupted result, each constrained
operand and runtime arity); empty constructors; sum branch domains/results; lost direct/transitive effects;
effectful function values/lambdas; typed initializer calls and exactly-once globals; active slot rebinding,
malformed captures/frame sizes; nominal record construction, duplicate fields and non-record projection.
The builtin table has an exhaustive coverage assertion. Result mutations keep the outer signature consistent
when the operation is the function body. Record field schemas and match reachability remain open obligations.

## 1. Purpose

This document defines programs that must pass or fail for MVP acceptance.

Test files should use naming convention:

```text
tests/pass/*.dlm
tests/fail/*.dlm
tests/warn/*.dlm
```

Expected errors can be declared in comments:

```dlm
// expect_error: E1203
```

## 2. PASS: minimal module and theory

```dlm
module tests.pass.minimal

pub theory T {
    type Nat
    let n = 7;
}
```

Expected: pass.

## 3. FAIL: value outside theory

```dlm
// expect_error: E0301
module tests.fail.value_outside_theory

let n = 7;
```

Expected: E0301.

## 4. PASS: literal passport inference

```dlm
module tests.pass.literal_passport

pub theory T {
    let n = 7;
    let m = n + 1;
}
```

Expected:

- `n` inferred as Nat Literal/Trivial/Checked;
- `m` inferred as Nat Expression or SmallFinite.

## 5. PASS: compressed number

```dlm
module tests.pass.compressed_nat

pub theory T {
    let n = 10^100;
    let d = describe(n);
}
```

Expected: pass.

## 6. FAIL: print non-expandable Graham

```dlm
// expect_error: E1203
module tests.fail.print_graham

pub theory T {
    let g = Graham();
    let s = print_decimal(g);
}
```

Expected: E1203 CannotPrintNonExpandable.

## 7. PASS: describe Graham

```dlm
module tests.pass.describe_graham

pub theory T {
    let g = Graham();
    let s = describe(g);
}
```

Expected: pass.

## 8. FAIL: print BB(1000)

```dlm
// expect_error: E1203
module tests.fail.print_bb

pub theory T {
    let bb = BB(1000);
    let s = print_decimal(bb);
}
```

Expected: E1203 with note that BB is Uncomputable.

## 9. FAIL: compare BB without proof

```dlm
// expect_error: E1204
module tests.fail.compare_bb_without_proof

pub theory T {
    let bb = BB(1000);
    let x = bb > 10^100;
}
```

Expected: E1204 CannotCompareWithoutProof.

## 10. PASS/WARN: axiom proof comparison in research mode

```dlm
// expect_warning: W2001
module tests.warn.axiom_compare_bb

pub theory T {
    let bb = BB(1000);
    axiom bb_lower : bb > 10^100;
    proof p : StaticProof<bb > 10^100> = axiom(bb_lower);
}
```

Expected:

- pass in `--research`;
- warning W2001;
- fail in `--no-axioms` with E1303.

## 11. FAIL: raw external input as Nat

```dlm
// expect_error: E1305
module tests.fail.raw_external_as_nat

pub theory T {
    let raw = io.read(stdin);
    let n : Nat = raw;
}
```

Expected: E1305.

## 12. PASS: parse external input

```dlm
module tests.pass.parse_nat

pub theory T {
    let raw = io.read(stdin);
    let n = parse_nat(raw);
}
```

Expected: pass, with IO/ExternalInput effects required depending placement.

## 13. FAIL: runtime witness as static proof

```dlm
// expect_error: E0904
module tests.fail.runtime_witness_as_static_proof

pub theory T {
    let n = io.read_nat(stdin);
    let w = require(n > 0);
    proof p : StaticProof<n > 0> = w;
}
```

Expected: E0904.

## 14. PASS: runtime witness

```dlm
module tests.pass.runtime_witness

pub theory T {
    let n = io.read_nat(stdin);
    let w : RuntimeWitness<n > 0> = require(n > 0);
}
```

Expected: pass.

## 15. FAIL: IO in pure function

```dlm
// expect_error: E1001
module tests.fail.io_in_pure

pub theory T {
    fn f() -> Nat {
        io.read_nat(stdin)
    }
}
```

Expected: E1001.

## 16. PASS: IO function with effects

```dlm
module tests.pass.io_effect

pub theory T {
    fn f() -> Result<Runtime<Nat>, IOError> effects IO, ExternalInput {
        io.read_nat(stdin)
    }
}
```

Expected: pass.

## 17. FAIL: bare Infinity

```dlm
// expect_error: E1503
module tests.fail.bare_infinity

pub theory T {
    let x = Infinity + 1;
}
```

Expected: E1503.

## 18. PASS: typed cardinal infinity

```dlm
module tests.pass.cardinal_infinity

pub theory T {
    let a : Infinity<Cardinal> = Aleph0;
}
```

Expected: pass if `Aleph0` exists in std_core.

## 19. FAIL: theory value without bridge

```dlm
// expect_error: E1401
module tests.fail.theory_no_bridge

pub theory PA {
    let seven = 7;
}

pub theory MetaArithmetic {
    let x = PA.seven;
}
```

Expected: E1401.

## 20. PASS: quote through bridge

```dlm
module tests.pass.quote_bridge

pub theory PA {
    let seven = 7;
}

pub theory MetaArithmetic {
    type Term<T>
}

pub bridge PA_quote : PA -> MetaArithmetic {
    kind = quote;
    preserves = [syntax];
}

in theory MetaArithmetic {
    let code = quote[PA_quote](PA.seven);
}
```

Expected: pass. `code` has syntax capabilities, not Nat arithmetic capabilities.

## 21. FAIL: add syntax as Nat

```dlm
// expect_error: E1207
module tests.fail.add_syntax_as_nat

pub theory PA {
    let seven = 7;
}

pub theory MetaArithmetic {
    type Term<T>
}

pub bridge PA_quote : PA -> MetaArithmetic {
    kind = quote;
    preserves = [syntax];
}

in theory MetaArithmetic {
    let code = quote[PA_quote](PA.seven);
    let bad = code + 1;
}
```

Expected: E1207 CannotUseSyntaxAsValue.

## 22. FAIL: proof as truth without soundness bridge

```dlm
// expect_error: E1405
module tests.fail.proof_is_not_truth

pub theory PA {
    type Prop
    theorem add_zero : Prop;
}

pub theory MetaArithmetic {
    type Term<T>
    type Provable<T>
}

pub bridge PA_quote : PA -> MetaArithmetic {
    kind = quote;
    preserves = [syntax];
}

in theory MetaArithmetic {
    let prov = verify_pa_proof(PA.add_zero.proof);
    let truth : StaticProof<PA.add_zero.statement> = prov;
}
```

Expected: E1405.

## 23. WARN/FAIL: unsafe assume input

```dlm
// expect_warning: W2003
module tests.warn.unsafe_assume

pub theory T {
    let raw = io.read(stdin);
    let n = unsafe_assume_nat(raw);
    let m = n + 1;
}
```

Expected:

- pass in `--allow-unsafe` with W2003;
- fail in `--trusted-only` with E1302.

## 24. FAIL: set of all sets

```dlm
// expect_error: E1502
module tests.fail.set_of_all_sets

pub theory T {
    let U = Set<AllSets>;
}
```

Expected: E1502 or parser-level equivalent depending MVP syntax.

## 25. PASS: private bridge not imported not visible

Two-file test:

`bridges.dlm`:

```dlm
module tests.pass.private_bridge.bridges

private bridge Hidden : A -> B {
    kind = quote;
}
```

`use.dlm`:

```dlm
// expect_error: E0704
module tests.pass.private_bridge.use

import tests.pass.private_bridge.bridges::Hidden
```

Expected: E0704.

## 26. MVP minimum pass count

MVP must include at least:

```text
10 pass tests
15 fail tests
3 warning/build-mode tests
```

All diagnostics must match expected codes.

## v0.19 GPU ↔ CPU transfer tests

Valid:

```text
examples/valid/gpu_cpu_transfer.dlm
```

Invalid:

```text
examples/invalid/copy_to_gpu_requires_gpu_memory.dlm
examples/invalid/copy_to_gpu_requires_serializable.dlm
examples/invalid/copy_from_gpu_requires_gpu_value.dlm
examples/invalid/print_decimal_gpu_value.dlm
```


## v0.24 BigNumber Hierarchy

Added explicit huge-number passports for `Graham()`, `TREE(n)`, `BB(n)` and `fast_growing(level)`. Bare huge numbers are rejected; huge finite numbers can be symbolically printed/proof-compared but are not decimal-printable unless a future checked evaluator provides that capability.
## v0.31 reflection/self-reference guard

```powershell
cargo check
cargo test
cargo run -p dlm_cli -- check examples\valid\reflection_self_reference_guard.dlm
cargo run -p dlm_cli -- run examples\valid\reflection_runtime_symbolic_guard.dlm
cargo run -p dlm_cli -- explain examples\valid\reflection_summary_axiom.dlm
cargo run -p dlm_cli -- check examples\invalid\reflection_quote_without_bridge.dlm
cargo run -p dlm_cli -- check examples\invalid\reflection_self_truth.dlm
cargo run -p dlm_cli -- check examples\invalid\reflection_self_unprovability.dlm
```
### v0.31 fix #2 — reflection_claim input contract

`reflection_claim(...)` must be tested with `Provable`, produced explicitly through `provable_of(proof)`. This prevents the valid reflection example from failing with `reflection_claim requires Provable; got StaticProof<...>`.
### v0.31 fix #3 — static checker proof vs runtime validation

`prove(...)` remains a static checker operation. Therefore `reflection_self_reference_guard.dlm` is a `check`/`explain` validation target, not a `run` target.

Use the runtime-safe symbolic example for `dlm run`:

```powershell
cargo run -p dlm_cli -- check examples\valid\reflection_self_reference_guard.dlm
cargo run -p dlm_cli -- explain examples\valid\reflection_summary_axiom.dlm
cargo run -p dlm_cli -- check examples\valid\reflection_runtime_symbolic_guard.dlm
cargo run -p dlm_cli -- run examples\valid\reflection_runtime_symbolic_guard.dlm
```

The invariant is intentional: static proof construction is not runtime execution, while symbolic self-reference claim construction may still be demonstrated at runtime.


## v0.34 resolver / ID regression checks

Additional Rust regression target:

```powershell
cargo test -p dlm_core --test resolver_ids
```

Expected:

```text
id allocator uses separate monotonic ID spaces
resolver assigns IDs to theories, values and bridges
resolver rejects duplicate values inside one theory
resolver rejects bridge declarations with unknown source/target theories
```

## v0.35.0 — Checker pass pipeline tests

Additional required checks:

```powershell
cargo test -p dlm_core --test checker_passes
```

The test file verifies:

```text
frontend pass report contains raw_ast_accepted and name_resolution;
valid modules reach legacy_checker;
resolution failures skip legacy_checker;
CheckReport exposes the pass pipeline.
```

## v0.36.0 — Property-style invariant tests

Additional required command:

```powershell
cargo test -p dlm_core --test property_invariants
```

Expected result:

```text
all property_invariants tests pass
```

The suite protects:

```text
trust join laws
policy prefix closure
bridge profile/law consistency
soundness-sensitive bridge boundaries
passport trust preservation
history order and multiplicity
```

## v0.37.0 — Meta-level stratification tests

New focused test command:

```powershell
cargo test -p dlm_core --test meta_levels
```

Expected checks:

```text
MetaLevelIndex ordering and naming;
object-level self-observation is rejected;
strict meta-level lifts are accepted;
meta_quote_passport creates Term only;
meta_quote_passport does not create TruthClaim/StaticProof;
meta_quote_passport preserves existing trust taint.
```

## v0.38.0 — Statement / theorem tests

New focused test command:

```powershell
cargo test -p dlm_core --test statements_theorems
```

Expected checks:

```text
Statement is not Theorem or StaticProof;
Theorem requires StaticProof evidence;
ProofTerm must be kernel-checked before theorem construction;
RuntimeWitness cannot close a theorem;
Axiom theorem construction is trust=Axiom;
Goal/Hypothesis do not become theorems implicitly.
```


## v0.39.0 — Proof context tests

Required tests:

```powershell
cargo test -p dlm_core --test proof_context
cargo test -p dlm_core --test statements_theorems
cargo test -p dlm_core --test meta_levels
cargo test -p dlm_core --test property_invariants
```

Protected invariants:

```text
ProofContext opens only from Goal<P>
HypothesisSet preserves order and multiplicity
Goal<P>, Statement<P>, StaticProof<P> must match exactly
Axiom proof closure remains trust=Axiom
```


## v0.40.0 — Tactic script tests

Required tests:

```powershell
cargo test -p dlm_core --test tactic_script
cargo test -p dlm_core --test proof_context
cargo test -p dlm_core --test statements_theorems
```

Protected invariants:

```text
Empty script keeps an open proof obligation.
Assume preserves hypothesis order and does not close the goal.
ExactStaticProof closes only matching Goal/Statement/StaticProof triples.
AdmitAxiom closes with visible Axiom taint.
Closing tactics must be final.
Proof obligation diagnostics are preserved under tactic execution.
```

## v0.41.0 — Proof certificate tests

Required tests:

```powershell
cargo test -p dlm_core --test proof_certificate
cargo test -p dlm_core --test tactic_script
cargo test -p dlm_core --test proof_context
cargo test -p dlm_core --test statements_theorems
```

Protected invariants:

```text
Closed static proof closures can emit stable certificates.
Open tactic reports cannot be certified.
Axiom admission remains status=AxiomAdmitted and trust>=Axiom.
Certificate identity must match theorem identity exactly.
Certificate fingerprints depend on trace order and contents.
Tampering with certificate contents invalidates the fingerprint.
```


## v0.42.0 — Certificate audit/export tests

Additional tests:

```powershell
cargo test -p dlm_core --test certificate_audit
```

Coverage:

- stable canonical export;
- successful certificate/theorem audit;
- rejection of tampered fingerprints;
- rejection of wrong theorem identity;
- unchecked forensic rendering remains non-validating.


## v0.43.0 — Equality/rewrite tests

New test target:

```powershell
cargo test -p dlm_core --test equality_rewrite
```

Coverage:

- reflexive equality is `EqProof`, not `Bool`;
- `EqProof` requires exact `StaticProof<Eq(lhs,rhs)>`;
- `RuntimeWitness` and raw proof terms are rejected as rewrite evidence;
- rewrite rules apply forward and reverse only on exact source terms;
- rewrite traces preserve step order;
- axiom equality taint is preserved in rewrite certificates.


## v0.44 Rewrite normalization tests

```powershell
cargo test -p dlm_core --test rewrite_normalization
```

Coverage:

- ordered normalization to normal form;
- zero-step already-normal terms;
- rejection of non-`RewriteRule` passports;
- step-limit guard for cyclic rewrites;
- taint preservation;
- audit rejection of tampered reports.


## v0.45 Nat induction tests

```powershell
cargo test -p dlm_core --test nat_induction
```

Covers:

- scheme construction;
- exact base/step static proof requirements;
- rejection of runtime witnesses and raw proof terms;
- family mismatch rejection;
- theorem construction from matching induction proof;
- axiom taint preservation;
- ordered history preservation.


## v0.46 module/import tests

- `module_imports.rs`: manifest passport boundaries.
- duplicate import/alias/export rejection.
- public/private export checks.
- missing module and cyclic import graph rejection.
- export passport trust/capability preservation.


## v0.47 module interface tests

Focused test target: `cargo test -p dlm_core --test module_interfaces`. It checks interface fingerprints, public/private import audit behavior, stale interface rejection, explicit import-edge requirements and textual audit export.


## v0.48.0 metatheory dependency tests

```powershell
cargo test -p dlm_core --test metatheory_dependencies
```

Coverage:

- duplicate/cross-theory axiom rejection;
- registry is not theorem/proof;
- declared axiom requirement;
- dependency entry trust/history preservation;
- unsafe taint preservation;
- order-sensitive audit fingerprints.

## v0.49.0 — Metatheory Closure Report Foundation

This patch continues track **1) Metamathematical foundation** by adding a global closure report layer over verified dependency audits.

New core concepts:

- `MetatheoryClosureReport`;
- `MetatheoryClosureStatus::{Closed, Open, Rejected}`;
- `ClosureObligation`;
- `ClosureObligationKind`;
- `metatheory_closure_report(...)`;
- `require_closed_metatheory_closure(...)`;
- `metatheory_closure_report_passport(...)`;
- `export_metatheory_closure_report(...)`.

Main law:

```text
verified dependency audit + closed obligations => closed metatheory closure report
```

Open obligations keep closure open. Rejected dependency audits reject closure. Axiom/oracle/unsafe taint remains visible.

## v0.50.0 conservative extension tests

New focused test target:

```text
cargo test -p dlm_core --test conservative_extension
```

Coverage:

- verified conservative extension over closed base/extension closures;
- theorem name/proposition mutation rejection;
- open extension status propagation;
- rejected base / empty preservation rejection;
- new assumption taint visibility;
- duplicate preserved theorem rejection;
- stable, order-sensitive export/fingerprint behavior.

## v0.51 theorem dependency graph tests

Focused test target:

```powershell
cargo test -p dlm_core --test theorem_dependency_graph
```

Coverage:

- verified global inventory;
- mislabeled node rejection;
- unknown edge endpoint rejection;
- open closure propagation;
- conservative-extension audit integration;
- axiom/oracle/unsafe taint preservation;
- stable and order-sensitive export/fingerprint behavior.

## v0.52 Soundness Boundary Ledger

```powershell
cargo test -p dlm_core --test soundness_boundary_ledger
```

Covers soundness/reflection/consistency/truth-lift boundary entries, duplicate rejection, safe bridge rejection, export stability, and ledger passport taint preservation.


### v0.53 Trusted Base Closure

```powershell
cargo test -p dlm_core --test trusted_base_closure
```

Covers closed/open/rejected trusted-base reports, required evidence kinds, duplicate evidence rejection, export stability and taint preservation.

### v0.54 Metatheory Foundation Exit

```powershell
cargo test -p dlm_core --test metatheory_foundation_exit
```

Covers ready/incomplete/rejected exit reports, required criteria, duplicate evidence, taint preservation, trusted-base conversion, export stability, and passport identity.


## v0.55 — Logical connectives / quantifiers

```powershell
cargo test -p dlm_core --test logical_quantifiers
```

Covers connective arity, binder validation, formula/proof separation, quantifier object passports, taint preservation and stable exports.


## v0.56.0 — Substitution / Alpha-Equivalence Foundation

```powershell
cargo test -p dlm_core --test substitution_alpha
cargo test -p dlm_core --test logical_quantifiers
cargo test
```

Coverage:

- free/bound variable extraction;
- alpha-equivalence of binder-renamed quantified formulas;
- non-equivalence when quantifier structure changes;
- capture-avoiding substitution;
- shadowing binder block;
- capture-risk rejection;
- proof/theorem/truth/runtime evidence rejection;
- taint preservation;
- stable exports and order-sensitive fingerprints.

## v0.57 Function / Lambda / Application tests

```text
cargo test -p dlm_core --test function_lambda_application
```

Coverage: function type construction, lambda construction, application domain checking, proof/theorem/runtime rejection, axiom taint preservation, stable exports and order-sensitive fingerprints.


<!-- V0_58_FUNCTION_CONTRACTS_BLOCK -->
## v0.58.0 — Function Contract / Purity / Totality Boundary

Adds `docs/FUNCTION_CONTRACTS.md` and the first contract layer above ordinary functions. Function contracts record purity, explicit effect boundaries, totality status, static evidence, open obligations and honest downgrade status.

Main law:

```text
FunctionContract is not theorem/proof/truth.
It is an audit/control object for future optimization, scheduling and assurance modes.
```


## v0.59.0 — Product / Sum / Record Type Foundation

Added `docs/STRUCTURAL_TYPES.md` and the core structural type layer: ProductType/ProductTerm, SumType/SumInjection and RecordType/RecordTerm/RecordProjection. This layer preserves trust taint, rejects proof/truth/runtime objects as ordinary structural values, and prepares future layout/ABI-aware records.


## v0.60.0 — Structural Elimination / Pattern Boundary

Adds explicit product elimination, sum case elimination and record pattern reports. Structural elimination is value-level and does not become proof, theorem or truth. Taint is preserved; proof/truth/theorem/runtime smuggling is rejected. See `docs/STRUCTURAL_ELIMINATION.md`.


## v0.61.0 — Option / Result / Partiality Type Foundation

Adds explicit `Option<T>`, `Result<T,E>` and `PartialityReport` objects for ordinary mathematical partiality. Partial functions now have a typed target representation instead of hidden null/exception semantics. Readiness delta: Local 42–50% -> 48–56%, Architectural 60–72% -> 64–76%, Fundamental 42–54% -> 46–58%.

- `sequence_types` — List/Sequence finite collection laws, explicit indexing, taint preservation, proof/truth/runtime rejection.

### v0.63 traversal boundary

- `traversal_boundary.rs` verifies bounded map/fold traversal over finite collections, fuel rejection, exact contract domain/codomain matching, taint preservation, and proof/truth/runtime exclusion.

- `recursion_boundary.rs` — recursion schemes, recursive calls, measure decrease, fuel exhaustion, taint preservation.


## v0.65 — Termination / Normalization Budget Unification

Adds `ComputationBudgetContract`, `BudgetUseReport`, and `TerminationBudgetReport` to unify rewrite-normalization, traversal, and recursion fuel into one bounded-computation ledger. See `docs/TERMINATION_BUDGET.md`.

## v0.66 Standard Algebraic Prelude Foundation

- `cargo test -p dlm_core --test standard_prelude`
- verifies canonical Nat/Bool/Option/Result/List/Sequence prelude contracts, exact signatures, explicit budgets, taint preservation and stable exports.


## v0.67 — Prelude Evaluation / Small-Step Algebraic Semantics

```powershell
cargo test -p dlm_core --test prelude_eval
```

Covers deterministic primitive evaluation, explicit option boundary for sequence index, bounded symbolic map/fold application, fuel rejection, contract rejection, evidence smuggling rejection, taint preservation and stable exports.

## v0.68.0 — Prelude lowering

```powershell
cargo test -p dlm_core --test prelude_lowering
cargo test -p dlm_core --test prelude_eval
cargo test -p dlm_core --test standard_prelude
cargo test
```

Covers:

```text
verified_erased scalar lowering;
rejected_evaluation from failed prelude evaluation;
rejected_evidence_boundary for proof/theorem/truth/runtime evidence;
gpu_batch rejects scalar operations and accepts bounded collection symbolic lowering;
Axiom/Oracle/Unsafe taint is preserved as downgraded_tainted;
export/fingerprint target sensitivity.
```


## v0.69.0 — Backend Capability / Lowering Target Contracts

```powershell
cargo test -p dlm_core --test backend_capability
cargo test -p dlm_core --test prelude_lowering
cargo test -p dlm_core --test prelude_eval
cargo test
```

Covers verified scalar backend acceptance, missing capability rejection, symbolic GPU batch acceptance, target mismatch rejection, taint preservation and stable backend exports.


## v0.70 Backend Layout / ABI Descriptor Boundary

Required Rust test:

```powershell
cargo test -p dlm_core --test backend_layout
```

The test matrix must confirm explicit ABI descriptors, target/container compatibility, metadata erasure boundaries, GPU buffer descriptors, vector dense layouts, taint preservation, and stable ABI-sensitive exports.

## v0.71 dense runtime descriptor tests

```text
cargo test -p dlm_core --test dense_runtime
```

Covers:

- scalar dense runtime descriptors;
- dense vector stride and scalar boxing rejection;
- GPU region / GPU-resident handle enforcement;
- remote region / remote handle enforcement;
- layout-container to runtime-representation matching;
- taint-preserving downgrade;
- stable descriptor/report exports.

## v0.72 example corpus integrity tests

```text
cargo test -p dlm_core --test example_corpus
cargo test -p dlm_core --test gpu_checkpoint_restore
cargo test -p dlm_cli --test cli_version
```

Covers:

- every valid example checks; every non-check-only valid example runs;
- every invalid example is rejected in its intended mode (check / trusted-only / runtime);
- no invalid example passes only because a builtin is unimplemented;
- docs/MANIFEST.json lists every document;
- GPU checkpoint/restore round trip, history law, non-live checkpoint, CPU/GPU checkpoint separation, aliases;
- `dlm --version` matches the workspace version.

## v0.73 ordinary surface tests

```text
cargo test -p dlm_core --test ordinary_surface
cargo test -p dlm_core --lib ordinary
cargo test -p dlm_core --test sequence_types
cargo test -p dlm_core --test example_corpus
```

Covers:

- fn declarations: verified contracts, effect/totality inference, downgrade, builtin shadowing, scoping;
- recursion (direct, mutual via forward reference, through map) rejected;
- Option/Result/List/Sequence/Product typing through the core reports; some/none and different lengths share a type;
- static map/fold produce verified_bounded core traversal reports; unknown-length collections are runtime bounded;
- taint flows through function bodies, collections and options; trusted-only rejects tainted functions;
- runtime semantics: lazy if/and/or, monus/pred truncation, index → Option, Nat overflow is an error;
- 15 corpus examples (4 valid, 11 invalid) checked in their intended mode.

## v0.74 recursion / records / sums tests

```text
cargo test -p dlm_core --test ordinary_recursion_records
cargo test -p dlm_core --lib ordinary
```

Covers accepted and rejected guard/decrease forms, verified/downgraded recursion schemes, fuel (explicit, default, maximum, runtime exhaustion), deep recursion on the runtime stack from a small test-thread stack, records (construction, projection, nesting, unknown/recursive records, namespace clashes), sums (injection, case typing, list typing), and taint through records, sums and recursive functions.

## v0.75 higher-order tests

```text
cargo test -p dlm_core --test ordinary_higher_order
cargo test -p dlm_core --lib ordinary
```

Covers function types, lambdas through the core LambdaTerm, closures and lexical scoping, the pure/total invariant for function values, exact function-type checking (calls, map, case), taint through function values, builtin shadowing by function bindings, recursion escape attempts (self as value, self inside lambda, shadowing lambda parameter), and accepted/rejected lexicographic measures.

## v0.76 statement / theorem tests

```text
cargo test -p dlm_core --test ordinary_theorems
cargo test -p dlm_cli --test cli_certify
cargo test -p dlm_core --test example_corpus documented_dlm_blocks_check
```

Covers proofs by computation, bounded and finite exhaustion, axioms and conjectures; certificates audited against their theorems; counterexample reporting and order; the rule that unbounded statements are never proved by testing (core rejects a Goal as a statement); purity and independence from theory values; proof objects rejected as ordinary values; the case limit; policy blocking of axioms; statement syntax errors; the runtime evaluation API; CLI certificate output.

## v0.87 Typed Core / semantics tests

```text
cargo test -p dlm_core --test typed_core
cargo test -p dlm_cli --test cli_programs run_shows_output_before_errors_and_contracts_are_reported
```

Covers expression ids and exact physical spans (multi-line expressions, UTF-8 literal with escapes, `match`);
IR types equal the checker's Typed Core facts for every node; every compilable example lowers to verified IR;
the IR verifier rejects eight kinds of corrupted IR (wrong type, undefined slot, slot out of range, call arity,
argument type, untyped node, mixed Nat/Int operands, non-Bool condition); contracts report independent
properties (a theorem does not close an overflow obligation, a guard does; tail loop vs native recursion;
effects through calls; scalar tail loops); compiled stack exhaustion is a defined failure; interpreter and
compiler agree on output, runtime error message and exit status for 12 fixed cases (lazy branches,
short-circuit, left-to-right failure order, failures inside calls) and 24 generated programs (about half fail
at run time).

## v0.86 / v0.86.1 compiled runtime tests

```text
cargo test -p dlm_core --test compile_c
cargo test -p dlm_core --test machine_arithmetic
cargo test -p dlm_cli --test cli_programs build_checks_machine_arithmetic_before_emission
```

v0.86: tagged/heap number boundaries, escaping and shared graphs, closure captures, stdin, 500 000 self tail
calls, a C harness for region ownership and `fold` accumulators, scalar functions with 128-bit values and effect
order; machine-range obligations (proofs do not discharge them, guarded bounds, exact boundaries, conservative
calls, 696 closed cases checked against the interpreter); `--check-overflow` / `--require-no-overflow`.
v0.86.1: linear tracing work for a graph returned through 20 000 calls and precise splitting of partly reachable
groups (C harness); 10 generated programs with sharing through recursion, tail calls and `fold` compared with
`dlm run`. For memory errors run the same tests with `CC` set to a wrapper that adds
`-fsanitize=address,undefined`.

## v0.85 Int tests

```text
cargo test -p dlm_core --test integers
cargo test -p dlm_core --test compile_c int_programs
```

Covers parsing of binary and unary `-` (associativity, `a - -b`, `->`/`=>`), the no-mixing rule (Nat subtraction,
Int + Nat, Nat literals in Int positions), exact arithmetic, Euclidean `div`/`mod` with the zero rules (identity
checked on a grid), text conversions including the i128 minimum, overflow errors, Int theorems proved and
cross-checked (`cases=11` per variable), false Int claims rejected, a 3000-statement kernel fuzz against the
runtime (mutation-checked), and compiled Int programs compared with `dlm run` (output and overflow messages).

## v0.84 compiler tests

```text
cargo test -p dlm_core --test compile_c
cargo test -p dlm_cli --test cli_programs build_produces_a_native_executable
```

Differential: compiled examples, I/O programs and generated programs must match `dlm run` in output and exit
status. Also covers overflow errors, deep recursion, statement erasure, unsupported constructs, closure captures and
the `dlm build` CLI. Needs a C compiler (`$CC` or `cc`).

## v0.83 text / I/O / main tests

```text
cargo test -p dlm_core --test text_io
cargo test -p dlm_cli --test cli_programs
```

Covers literals and escapes, literals containing separators, parse errors, every Text builtin, Text theorems
(folding, cross-check on sample texts, false claims rejected), `read_line` / `read_all` / `args`, `main` results and
exit status, `main` validation, effect tracking, and the CLI (`--stdin`, `--stdin-file`, `-- args`, exit code).

## v0.82 let / match / logical-line tests

```text
cargo test -p dlm_core --test patterns_let
cargo test -p dlm_core --test symbolic_kernel_soundness case_split
```

Covers logical-line joining and spans, `let` (single evaluation, shadowing, closures, rendering, nested `in`),
grouping parentheses, `match` over Nat/List/Option/Bool with proofs of equivalence to explicit definitions,
recursion through patterns and `let` aliases, syntax errors (exhaustiveness, reachability, kinds, reserved names,
parenthesization), and fuzzing of `let` / `match` / Nat case splitting against the runtime.

## v0.81 audit / project / mutual tests

```text
cargo test -p dlm_core --test audit_project
cargo test -p dlm_core --test mutual_recursion
cargo test -p dlm_core --lib project
cargo test -p dlm_cli --test cli_audit_project
```

Covers audit methods, transitive lemmas and axioms across modules, unused axioms, bridges and tainted values,
fingerprint stability, project manifests (parsing, failing roots, policy override, the repository project), mutual
groups (checking, proofs, runtime, deep recursion, measure/guard/kind/lambda rules, purity of the whole group,
syntax, forward references) and the CLI commands.

## v0.80 module tests

```text
cargo test -p dlm_core --test modules
cargo test -p dlm_core --lib program
cargo test -p dlm_cli --test cli_modules
cargo test -p dlm_core --test example_corpus every_library_module_checks
cargo test -p dlm_core --test symbolic_kernel_soundness nested
```

Covers dependency ordering, cycles, missing modules, module-name mismatches, theory clashes, inheritance of
functions/lemmas with runtime execution of inherited (and private) code, axiom taint across modules, privacy across
modules and chains, redeclaration, non-inherited values, unlinked imports, prefixed diagnostics, nested lemma
patterns, the standard library under `--trusted-only`, CLI `--lib` / `DLM_PATH`, and fuzzing of nested lemmas.

## v0.79 step / generalized induction tests

```text
cargo test -p dlm_core --test symbolic_advanced_induction
cargo test -p dlm_core --test symbolic_kernel_soundness advanced
```

Covers step-s induction through the core step scheme, generalized hypotheses, Bool case split and Bool hypotheses,
the necessity of each option, proof-clause option validation, rewrite-cycle termination, the depth guard, core
step-induction objects, and fuzzing of all induction variants against the runtime.

## v0.78 inequality / list / cross-check tests

```text
cargo test -p dlm_core --test symbolic_lists_inequalities
cargo test -p dlm_core --test symbolic_kernel_soundness
cargo test -p dlm_core --lib nonnegative_combination
```

Covers inequality goals with nonnegative combinations, `List<Nat>` theorems and structural induction through core list objects, cross-check events and skipping, a false axiom caught by the cross-check, inequality and list fuzzing against the runtime, and a semantic check of the combination rule.

## v0.77 symbolic proof tests

```text
cargo test -p dlm_core --test symbolic_proofs
cargo test -p dlm_core --test symbolic_kernel_soundness
cargo test -p dlm_core --lib symbolic
```

Covers polynomial normal forms, safe truncation rules, the rational-multiple step, `by normalize` / `by induction` / `using` in syntax, core induction objects in history, lemma eligibility and taint, residual reporting, proof-clause syntax, and soundness fuzzing of normalization and induction against runtime evaluation.

## v0.100.1 cumulative integration and compatible complex APIs

```sh
cargo test -p dlm_core --test complex_q --test exact_complex_q --test source_complex_q
cargo test -p dlm_cli --test cli_complex_q --test cli_complex_rational
python3 tools/generate_complex097_oracle.py
cargo test --workspace --locked --offline --no-fail-fast
```

The additional `complex_q.rs` cases retain 1944 independent host fixtures,
118 interpreter/native vectors and 47 source precision-boundary checks.
`source_complex_apis_coexist_at_wide_precision_and_text_limits` checks both
record/function families in one program, old and new text forms, a 16384-bit
component with failure at 16383, a valid 1025-bit profile with insufficient and
sufficient work, invalid 16385-bit precision, the 65544-character parse limit
and refusal one character above it. The host also parses that exact limit.
Private block helpers stay private. Existing wide arithmetic, proof replay,
source identity, checked declaration and complex API suites remain in the full
workspace run. Full results and configuration are recorded in UPDATE.md.

## v0.101 C-min acceptance and rational intervals

```text
cargo test -p dlm_core --test c_min_acceptance
cargo test -p dlm_cli --test cli_c_min
cargo test -p dlm_core --test interval_q
cargo test -p dlm_cli --test cli_interval_q
python3 tools/generate_interval_q101_oracle.py
```

`c_min_acceptance.rs` has one test per C-min condition (hand-written DAG checked
from text; scopes/references/root and rehashed certificate tampering; external
axiom context; 21 malformed artifacts, cycles and five budget kinds; audit of
kernel-replay versus legacy-trusted evidence; a replayed theorem leaves machine
obligations, overflow checks and the emitted C unchanged). `cli_c_min.rs` checks
standalone replay with the empty axiom context and exit codes 1/2/3.
`interval_q.rs`: 5055 independent fixtures with minimal precision and 4559
boundaries on the host, inclusion/tightness on a 28-interval grid, guaranteed
square roots, strict text, budgets; 233 fixtures (77 boundaries) in the
interpreter and native C; raw records, errors and private helpers.

## v0.102 D-min ownership and computable reals

```text
cargo test -p dlm_core --test ownership
cargo test -p dlm_core --test creal
cargo test -p dlm_cli --test cli_creal_ownership
python3 tools/generate_creal102_oracle.py
```

`ownership.rs`: nine buffer programs agree between the interpreter and native C
and release all 11 buffers in scope (trace), with a fixed number of inserted
releases per function; six runtime-error programs fail identically in both
executors and release their live buffers at exit; 26 negative lifetime scenarios
are rejected with `E0943`; physical diagnostic positions. `creal.rs`: 343
independent fixtures on the host with 340 precision boundaries, enclosure/width
and refinement checks, domain errors, undecided comparisons, grammar and
validation; 88 fixtures (30 boundaries) in the interpreter and native C; raw
records, budgets, private helpers and host/source text round-trip.
`cli_creal_ownership.rs`: run/build of both examples (zero buffers left at exit)
and `dlm check` reporting an ownership error with its position.

## v0.103 borrowed buffers and computable complex numbers

```text
cargo test -p dlm_core --test ownership --test ccomplex --test ir_verify
cargo test -p dlm_cli --test cli_creal_ownership
python3 tools/generate_ccomplex103_oracle.py
```

## v0.104 elementary functions and TextBuf

```text
cargo test -p dlm_core --test elementary --test ownership --test ir_verify
python3 tools/generate_elementary104_oracle.py   # needs mpmath
```

## v0.105 numeric conversions and portable format

```text
cargo test -p dlm_core --test convert
python3 tools/generate_convert105_oracle.py   # needs mpmath
```

## v0.106 numeric contracts and scalar witnesses

```text
cargo test -p dlm_core --test numeric_contract --test scalar_witness
```

## v0.107 dense numbers and cost profile

```text
cargo test -p dlm_core --lib dense_nat
cargo test -p dlm_core --test dense_profile --test ir_verify
```

## v0.108 numeric acceptance

```text
DLM_REQUIRE_SANITIZERS=1 DLM_REQUIRE_WINDOWS=1 cargo test -p dlm_core --release --test numeric_acceptance
tools/numeric_acceptance.sh
```

## v0.109 file handles

```text
cargo test -p dlm_core --test files --test ownership --test ir_verify
```

## v0.110 generics

```text
cargo test -p dlm_core --test generics
```

## v0.112 type classes

```text
cargo test -p dlm_core --test type_classes --test generics
```

## v0.114 machine floats

```text
cargo test -p dlm_core --test floats --test ir_verify
```

## v0.115 graphs, geometry, probability

```text
cargo test -p dlm_core --test graphs --test ir_verify
```

## v0.116 numerical linear algebra

```text
cargo test -p dlm_core --test linalg --test ir_verify
```

## v0.117 analysis and optimization

```text
cargo test -p dlm_core --test analysis --test ir_verify
```

## v0.118 number theory and groups

```text
cargo test -p dlm_core --test number --test ir_verify
```

## v0.119 SAT and topology

```text
cargo test -p dlm_core --test logic_topology --test ir_verify
```

## v0.120 Gröbner bases and FinSet

```text
cargo test -p dlm_core --test groebner --test ir_verify
```

## v0.121 SAT certificates and ILP

```text
cargo test -p dlm_core --test proofs_ilp --test ir_verify
```

## v0.122 ordered maps

```text
cargo test -p dlm_core --test maps --test ir_verify
```

## v0.123 sparse solvers, geometry, signals, statistics

```text
cargo test -p dlm_core --test kernels --test ir_verify
```

## v0.124 big primes, BPSW, BVH

```text
cargo test -p dlm_core --test bignum_bvh --test ir_verify
```

## v0.125 loops, enums, file system

```text
cargo test -p dlm_core --test apps --test files --test ir_verify
```

## v0.126 recursive data, processes, TCP

```text
cargo test -p dlm_core --test os_records --test apps --test ir_verify
```

## v0.127 own GUI G1

```text
cargo test -p dlm_core --test gui --test ir_verify --test example_corpus
```

## v0.128 vector UI, windows

```text
cargo test -p dlm_core --test gui_vector --test gui --test ir_verify
```

## v0.129 fonts, lists, Unicode input

```text
cargo test -p dlm_core --test gui_text --test gui_vector --test gui --test ir_verify
```

## v0.131 GPU presentation

```text
cargo test -p dlm_core --test gui_gpu --test gui_text --test gui_vector --test ir_verify
```
