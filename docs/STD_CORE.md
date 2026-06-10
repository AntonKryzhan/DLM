# STD_CORE.md — Minimal Standard Core for DLM / ЯРД MVP

## 1. Purpose

`std_core` provides the minimum types, primitive operations and builtin passport transfer rules required to check MVP programs.

## 2. Core modules

Proposed layout:

```text
std/core/bool.dlm
std/core/nat.dlm
std/core/bytes.dlm
std/core/text.dlm
std/core/result.dlm
std/core/proof.dlm
std/core/runtime.dlm
std/core/io.dlm
std/core/theory.dlm
std/core/infinity.dlm
```

In MVP these may be hardcoded in Rust and exposed as virtual modules.

## 3. Bool

```dlm
pub theory CoreBool {
    pub type Bool
    pub let true : Bool
    pub let false : Bool

    pub fn not(x: Bool) -> Bool;
    pub fn and(a: Bool, b: Bool) -> Bool;
    pub fn or(a: Bool, b: Bool) -> Bool;
}
```

Passport rules:

- literals are `Checked`, `Literal`, `Trivial`;
- boolean operations preserve trust through join;
- runtime booleans produce runtime predicates when used in `require`.

## 4. Nat

```dlm
pub theory CoreNat {
    pub type Nat

    pub let zero : Nat
    pub fn succ(n: Nat) -> Nat;
    pub fn add(a: Nat, b: Nat) -> Nat;
    pub fn mul(a: Nat, b: Nat) -> Nat;
    pub fn pow(a: Nat, b: Nat) -> Nat;

    pub fn compare(a: Nat, b: Nat) -> CompareResult;
    pub fn print_decimal(n: Nat) -> Text;
    pub fn describe(n: Nat) -> Text;
}
```

## 5. Nat literal passport

Integer literal example:

```dlm
let n = 7;
```

Passport:

```text
type = Nat
construction = Literal
capabilities = PRINT_DECIMAL | SYMBOLIC_PRINT | COMPARE_DIRECT | COMPUTE_MODULAR | EXPAND | USE_STATIC_PROOF | USE_RUNTIME
cost = Trivial
trust = Checked
provenance = InternalLiteral
validation = ProofChecked
universe = U0
equality = VALUE_EQUALITY
theory = AmbientTheory
```

## 6. Compressed Nat

Expression:

```dlm
let n = 10^100;
```

Passport:

```text
construction = Compressed
capabilities = SYMBOLIC_PRINT | COMPARE_BY_PROOF | COMPUTE_MODULAR | USE_STATIC_PROOF | USE_RUNTIME
cost = Compressed or LargeFinite depending threshold
trust = Checked
```

`print_decimal` may be rejected if expansion exceeds configured bound.

## 7. Big number builtins

MVP may expose symbolic builtins:

```dlm
pub fn Graham() -> Nat;
pub fn TREE(n: Nat) -> Nat;
pub fn BB(n: Nat) -> Nat;
```

Suggested passports:

### Graham

```text
construction = Recursive
capabilities = SYMBOLIC_PRINT | COMPARE_BY_PROOF | COMPUTE_MODULAR | USE_STATIC_PROOF
cost = NonExpandable
trust = Builtin
```

### TREE(3)

```text
construction = ProofDefined
capabilities = SYMBOLIC_PRINT | COMPARE_BY_PROOF | USE_STATIC_PROOF
cost = ProofRequired
trust = Builtin
```

### BB(n)

```text
construction = Definable
capabilities = SYMBOLIC_PRINT | COMPARE_BY_PROOF
cost = Uncomputable
trust = Builtin
```

## 8. Bytes and Text

```dlm
pub type Bytes
pub type Text
```

Raw bytes from IO are not mathematical values.

```text
External<Bytes> starts with validation = Raw.
```

## 9. Result and Option

```dlm
pub type Result<T, E>
pub type Option<T>
```

Core functions:

```dlm
fn ok<T, E>(value: T) -> Result<T, E>;
fn err<T, E>(error: E) -> Result<T, E>;
```

MVP checker may treat `Result` structurally without implementing full pattern matching.

## 10. Proof types

```dlm
pub type StaticProof<P>
pub type RuntimeWitness<P>
pub type Proof<P>
```

MVP semantics:

```text
Proof<P> may be an alias or generic wrapper with trust annotation.
StaticProof<P> is compile-time/static.
RuntimeWitness<P> is runtime checked.
```

Proof sources:

```text
CheckedProof
BuiltinProof
AxiomProof
OracleProof
UnsafeProof
```

## 11. require

```dlm
pub fn require<P>(p: RuntimeBool<P>) -> Result<RuntimeWitness<P>, ConstraintError> effects Runtime;
```

Simplified MVP syntax:

```dlm
let w = require(n > 0);
```

If `n` is runtime, result is `RuntimeWitness<n > 0>`.

If expression is static and provable by builtin rule, it may produce `StaticProof<P>` only through explicit static proof context.

## 12. External and Runtime wrappers

```dlm
pub type External<T>
pub type Runtime<T>
pub type Checked<T, P>
```

Rules:

- `External<T>` starts with minimal capabilities;
- parse functions convert `External<Bytes>` into `Result<Runtime<T>, ParseError>`;
- require functions convert `Runtime<T>` into `Checked<Runtime<T>, P>` with `RuntimeWitness<P>`.

## 13. IO

```dlm
pub theory CoreIO {
    pub type Stdin
    pub type FilePath
    pub type IOError
    pub type ParseError
    pub type ConstraintError

    pub fn read(stdin: Stdin) -> Result<External<Bytes>, IOError> effects IO, ExternalInput;
    pub fn parse_nat(bytes: External<Bytes>) -> Result<Runtime<Nat>, ParseError> effects Runtime;
    pub fn read_nat(stdin: Stdin) -> Result<Runtime<Nat>, IOError> effects IO, ExternalInput;
}
```

`read_nat` is sugar for read + parse.

## 14. Theory core

Types for metatheory:

```dlm
pub type Term<T>
pub type Provable<Theory, Term>
pub type TheoryId
pub type BridgeId
```

Builtins:

```dlm
fn quote<T>(x: T) -> Term<T>;
fn transport<B, T>(bridge: B, x: T) -> Transported<T>;
```

Actual behavior is bridge-dependent.

## 15. Infinity

```dlm
pub type Infinity<Mode>

pub type Potential
pub type Cardinal
pub type Ordinal
pub type Limit
pub type ClassInfinity
pub type UniverseInfinity
```

No bare `Infinity` in MVP.

## 16. Equality

Core equality modes:

```dlm
pub type Eq<ValueEquality, A, B>
pub type Eq<StructuralEquality, A, B>
pub type Eq<SyntacticEquality, A, B>
pub type Eq<ProofEquality, A, B>
```

MVP may parse but not deeply prove equality.

## 17. Standard primitive operation descriptors

MVP Rust core must define OperationSpec for:

```text
Nat literal
Nat add/mul/pow
Nat compare
print_decimal
describe
read
parse_nat
read_nat
require
quote
transport
```

## 18. Core diagnostic support

Each std_core primitive must define helpful error messages.

Example `print_decimal` required caps:

```text
required: PRINT_DECIMAL
if missing due to NonExpandable: E1203
if missing due to Uncomputable: E1203 with uncomputable explanation
if missing due to External Raw: E1305
```

## 19. MVP standard library strategy

MVP implementation can hardcode std_core in Rust while keeping `.dlm` interface docs.

Later versions can bootstrap std_core from `.dlm` files.

## 20. Non-goals for MVP std_core

- arbitrary recursion execution;
- full evaluator;
- integer big-int decimal expansion for enormous values;
- full proof calculus;
- symbolic algebra engine;
- SMT backend.


## v0.24 BigNumber Hierarchy

Added explicit huge-number passports for `Graham()`, `TREE(n)`, `BB(n)` and `fast_growing(level)`. Bare huge numbers are rejected; huge finite numbers can be symbolically printed/proof-compared but are not decimal-printable unless a future checked evaluator provides that capability.
