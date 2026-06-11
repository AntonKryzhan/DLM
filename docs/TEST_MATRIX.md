# TEST_MATRIX.md — DLM / ЯРД MVP Test Matrix

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
