# MVP_ACCEPTANCE.md — DLM / ЯРД MVP Acceptance Criteria

## 1. MVP definition

MVP is a static checker for `.dlm` files implementing the core architectural guarantees of DLM/ЯРД.

It is not yet:

- a full theorem prover;
- a full dependent language;
- a production codegen compiler;
- a symbolic algebra engine;
- an SMT-backed verifier.

## 2. Required CLI

MVP must provide:

```bash
dlm check <file-or-dir>
dlm test <dir>
dlm check <file> --emit ast
dlm check <file> --emit hir
dlm check <file> --emit passport-ir
dlm check <file> --format human
dlm check <file> --format json
```

Build modes:

```bash
--research
--strict
--no-axioms
--trusted-only
--allow-unsafe
```

## 3. Required parser support

Parser must support:

```text
module
import
pub/private theory
bridge
kind/preserves/transforms
let
fn
type
axiom
theorem/proof declarations at parse level
StaticProof
RuntimeWitness
External/Runtime/Result types
quote[bridge](expr)
transport[bridge](expr)
require(expr)
integer and string literals
simple binary expressions
```

## 4. Required module model

MVP must enforce:

```text
1 file = 1 module
module may contain multiple theories and bridges
value-level items are forbidden outside theory
bridge is module-level
AmbientTheory is set by theory body or in theory block
```

## 5. Required theory model

MVP must:

- assign TheoryId;
- track AmbientTheory;
- reject values used across theories without bridge;
- support quote bridge;
- parse transport bridge;
- reject soundness bridge unless explicitly trusted/axiom/unsafe.

## 6. Required passport model

MVP must implement `Passport` with fields:

```text
construction
capabilities
cost
trust
provenance
validation
universe
equality
theory
```

## 7. Required lattice operations

MVP must implement:

```text
join_trust
join_cost
join_construction
capability intersection rule
effect combination
validation progression
provenance propagation
theory bridge trace update
```

## 8. Required capability checks

MVP must reject:

- printing non-expandable values;
- printing uncomputable values;
- comparing proof-required values without proof;
- using syntax as value;
- using raw external input as mathematical value;
- using runtime witness as static proof;
- using object from another theory without bridge.

## 9. Required standard core

MVP must include builtins for:

```text
Nat
Bool
Bytes
Text
Result
External
Runtime
Checked
StaticProof
RuntimeWitness
Graham
TREE
BB
print_decimal
describe
read
parse_nat
read_nat
require
quote
transport
```

Hardcoded Rust std_core is acceptable.

## 10. Required diagnostics

MVP must implement at least these diagnostics:

```text
E0301 ValueOutsideTheory
E0904 RuntimeStaticMismatch
E1001 EffectNotAllowed
E1201 CapabilityMissing
E1203 CannotPrintNonExpandable
E1204 CannotCompareWithoutProof
E1302 UnsafeLeakError
E1303 AxiomNotAllowed
E1305 RawExternalInputUse
E1401 TheoryBridgeRequired
E1405 ProofIsProvabilityNotTruth
E1503 InfinityModeRequired
W2001 AxiomUsed
W2003 UnsafeUsed
```

## 11. Required test matrix

MVP must pass all tests in `TEST_MATRIX.md` that are marked MVP-required.

Minimum acceptance:

```text
10 pass tests
15 fail tests
3 warning/build-mode tests
```

## 12. Required IR emits

For debugging, MVP must emit:

- AST JSON;
- HIR JSON;
- PassportIR JSON.

These need not be stable public APIs in MVP, but must help development.

## 13. Rust workspace acceptance

Recommended workspace:

```text
dlm/
  Cargo.toml
  crates/
    dlm_cli/
    dlm_span/
    dlm_lexer/
    dlm_parser/
    dlm_ast/
    dlm_hir/
    dlm_types/
    dlm_passport/
    dlm_checker/
    dlm_diagnostics/
    dlm_std_core/
  tests/
    pass/
    fail/
    warn/
  docs/
    SPEC.md
    GRAMMAR.md
    AST_IR.md
    PASSPORT_LATTICE.md
    COMPILER_PASSES.md
    DIAGNOSTICS.md
    STD_CORE.md
    TEST_MATRIX.md
    MVP_ACCEPTANCE.md
```

MVP may start as fewer crates and split later, but public internal boundaries must correspond to this layout.

## 14. Implementation milestones

### Milestone 0 — repository skeleton

Acceptance:

- cargo workspace builds;
- `dlm_cli` has `dlm check --help`;
- diagnostics span type exists.

### Milestone 1 — lexer/parser

Acceptance:

- parses module/theory/bridge/let/fn/type;
- emits AST JSON;
- parser errors have spans.

### Milestone 2 — AST/HIR resolver

Acceptance:

- resolves modules/imports/theories;
- catches value outside theory;
- detects duplicate theories and bridge source/target.

### Milestone 3 — type core

Acceptance:

- supports Nat/Bool/Bytes/Text/Result/External/Runtime;
- infers integer literals as Nat;
- detects simple TypeMismatch.

### Milestone 4 — passport lattice

Acceptance:

- Passport struct implemented;
- lattice functions tested;
- literals get passports;
- `10^100`, `Graham`, `BB` get distinct passports.

### Milestone 5 — capability checker

Acceptance:

- rejects `print_decimal(Graham())`;
- rejects `print_decimal(BB(1000))`;
- allows `describe(Graham())`.

### Milestone 6 — I/O provenance and RuntimeWitness

Acceptance:

- `read` returns External Raw;
- raw external cannot be used as Nat;
- `parse_nat` converts to Runtime Nat;
- `require` produces RuntimeWitness;
- RuntimeWitness cannot be StaticProof.

### Milestone 7 — TheoryBridge MVP

Acceptance:

- values cannot cross theories without bridge;
- quote bridge works;
- quote produces syntax capabilities;
- syntax cannot be added as Nat;
- proof/provability/truth confusion rejected.

### Milestone 8 — build modes and trust taint

Acceptance:

- axioms warn in research mode;
- axioms fail in `--no-axioms`;
- unsafe fails in `--trusted-only`;
- trust taint appears in PassportIR.

### Milestone 9 — test runner

Acceptance:

- `dlm test tests/` runs expected pass/fail/warn tests;
- expected diagnostic code matching works.

## 15. Done definition

MVP is done when:

```text
1. All MVP docs exist and match implementation.
2. CLI can check single files and directories.
3. Parser handles MVP grammar.
4. HIR resolver handles modules/theories/bridges.
5. Passport lattice works for core examples.
6. Capability checker rejects invalid operations.
7. I/O boundary is enforced.
8. StaticProof/RuntimeWitness distinction is enforced.
9. TheoryBridge boundary is enforced.
10. Test matrix passes.
11. JSON IR emit works.
12. README has examples and build instructions.
```

## 16. Non-goals before MVP completion

Do not implement before MVP is accepted:

- user-defined passport rules;
- SMT monotonicity checking;
- proof normalization;
- totality checker;
- borrow checker or ownership model;
- optimizer;
- machine-code backend;
- LSP server;
- package registry;
- macros.

## 17. Post-MVP roadmap

After MVP:

```text
0.2 — user-declared safe passport constraints, still no arbitrary transformers.
0.3 — small proof kernel for builtin equalities and Nat induction fragments.
0.4 — bridge verification DSL.
0.5 — SMT integration for monotonicity and simple arithmetic.
0.6 — LSP diagnostics.
0.7 — package/module manager.
1.0 — stable SPEC and std_core.
```
