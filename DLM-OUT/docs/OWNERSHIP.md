# Ownership of mutable resources: D-min (0.102.0)

Stage D of law 42 requires move/borrow, handles, cleanup on errors and cancellation, and passed negative
lifetime scenarios. This version implements the bounded criterion **D-min** for one resource kind, the owned
mutable byte buffer `ByteBuf`, through the whole pipeline: checker, Typed Core, IR verifier, interpreter and
native C runtime. File, socket and other external handles (stage F/G) and cancellation (there are no tasks yet)
are not part of D-min.

## The type and its operations

`ByteBuf` is a buffer of bytes (0..255), at most 2^28 bytes long. It is not copyable: each value has exactly one
owner. Positions: **borrow** (the owner stays usable), **move** (ownership passes to the operation).

| Operation | Positions | Result | Failure (runtime error) |
|---|---|---|---|
| `buf_new(n, fill)` | — | `ByteBuf` of `n` bytes `fill` | `byte buffer length exceeds 268435456`, then `byte value exceeds 255` |
| `buf_from_list(xs: List<Nat>)` | — | `ByteBuf` with the elements | length limit, then `byte value exceeds 255` at the first bad element |
| `buf_len(b)` | borrow `b` | `Nat` | — |
| `buf_get(b, i)` | borrow `b` | `Option<Nat>` (`none` out of range) | — |
| `buf_set(b, i, v)` | move `b` | the same buffer, byte `i` set | `byte value exceeds 255`, then `byte buffer index out of range` |
| `buf_push(b, v)` | move `b` | the same buffer, one byte longer | `byte value exceeds 255`, then the length limit |
| `buf_to_list(b)` | move `b` | `List<Nat>`; the buffer is released | — |
| `buf_free(b)` | move `b` | its length; the buffer is released | — |

All eight are effects `runtime@byte_buffer` in function contracts (`buf_new`, `buf_from_list`, `buf_set`,
`buf_push` are partial). Function values are pure and total, so they cannot use buffers. A user function moves the
arguments of its `ByteBuf` parameters and may return a `ByteBuf`.

## Static rules (`crate::ownership`, error `E0943 OwnershipError`)

The pass runs on every function body and on all theory-level code after typing:

1. **Owners.** A parameter of type `ByteBuf`, or a `let` whose value is a buffer. A buffer produced by an
   expression that is not bound, moved or returned is an error (`a ByteBuf value is used as an ordinary value`).
2. **Borrow.** A borrowed argument must be a named owner that has not been moved (an unnamed buffer in a borrow
   position would be lost: `must be a named owner`).
3. **Move.** Passing an owner to a move position, binding it with `let` or returning it moves it. Any later use is
   an error (`used after it was moved at line L`, `borrowed after it was moved`).
4. **Arguments of one call.** A moved argument is transferred when the call is entered, after all its arguments are
   evaluated left to right. Sibling arguments may borrow it (`buf_set(b, 0, buf_len(b))`); moving it twice in one
   call is an error (`moved twice in one call`), as is moving it in an earlier sibling and using it later.
5. **Branches.** Each `if_then_else` branch starts from the same state. An outer owner consumed on one branch only
   is **released at the end of the other branch**, so after the conditional it is consumed on every path
   (`match` is desugared to `let`/`if_then_else` and follows the same rule).
6. **Lazy operands.** The right operand of `and`, `or`, `unwrap_or` may not move an outer owner.
7. **Function values** capture no owner and have no `ByteBuf` parameters.
8. **No aliasing through data.** `ByteBuf` cannot occur inside `List`, `Sequence`, `Option`, `Result`, products,
   sums, record fields, `Fn` types or type arguments, and cannot be a theory-level value or occur in statements.

The result is a **release plan** stored in the Typed Core facts: `FunctionFacts::released_params` (parameters
still owned when the body has its value), `TypedFacts::released_lets` and `TypedFacts::released_branches`.
Lowering inserts `__release(value, owner)` exactly there; the IR verifier checks that the released operand is a
`ByteBuf` local and that no node type nests a `ByteBuf`. The analysis is affine and path-sensitive over the
expression tree; it does not reason about values (a branch taken never is still checked).

## Execution

* **Interpreter (`dlm run`)**: value semantics (`Vec<u8>`); a moved buffer is never observed again, so copying
  and in-place mutation are indistinguishable.
* **Native C**: a `DV_BYTES` header in the call region points to storage allocated outside the regions, so the
  region invariant "no member ever changes" is kept while the bytes change in place. `buf_set`/`buf_push` mutate
  and return the same header; consuming operations and `__release` free the storage and clear the header, and a
  later access would fail with `byte buffer used after release` (never reached by checked programs).
* **Error paths.** There are no early returns; a runtime error ends the program (exit 1, message identical to
  `dlm run`). Live buffers are then released by the runtime's exit cleanup, so ASan/LSan report no leak on any
  path. `DLM_OWNERSHIP_TRACE=1` prints
  `dlm ownership: N buffers created, M released in scope, K released at exit`; checked programs that finish
  normally have `K = 0`.

## Evidence

`crates/dlm_core/tests/ownership.rs`: nine programs (recursion with in-place mutation, borrow+move in one call,
shadowing, branch releases, returned buffers) agree between interpreter and native C, all 11 buffers are released
in scope; the number of inserted releases per function is fixed; six runtime-error programs fail identically in
both executors and release their live buffers at exit; 26 negative lifetime scenarios (use after move, double
move, sibling moves, lazy moves, captures, containers, records, type arguments, globals, statements, rvalue
borrows) are rejected; diagnostics carry `E0943` and the physical position. ASan/UBSan/LSan and MinGW-w64/Wine
runs of the same programs are recorded in UPDATE.md. `examples/valid/byte_buffer_demo.dlm` (an in-place sieve)
runs and builds with identical output (`cli_creal_ownership.rs`).

## Open (full stage D)

External handles with explicit close/flush errors (files, sockets), shared read-only borrows that outlive a call,
mutable borrows of parts (disjoint ranges), buffers inside data structures (needs stage E dense containers and
generics), cancellation of structured tasks (stage G), and a formal soundness argument for the pass.

## v0.103: borrowed parameters, exclusive mutation, more operations

* `fn f(b: &ByteBuf)` borrows the caller's owner for the call: the callee may only borrow `b` (pass it to borrow
  positions or other `&ByteBuf` parameters); it never moves or releases it (`IrFunction::borrowed`; the IR
  verifier rejects a release of such a slot). At the call site the argument is a borrow position.
* One call may not receive the same owner both as a direct borrowed and a moved argument
  (`borrowed and moved by the same call`, e.g. `buf_append(b, b)`); shared borrows (`g(b, b)` with two `&ByteBuf`)
  and borrows completed in nested calls (`buf_set(b, 0, buf_len(b))`) stay allowed.
* `buf_copy(&b)`, `buf_slice(&b, start, len)` (`byte buffer range out of bounds`), `buf_append(a, &b)` (in place),
  `buf_eq(&a, &b)`, `buf_from_text(t)` (UTF-8 bytes), `buf_to_text(&b)` (`none` unless strict UTF-8).
* Tests (`ownership.rs`): 7 more programs, 11 buffers released in scope, borrowed-parameter IR facts and the
  verifier rejection, 6 negative borrow/alias cases, 3 runtime failures with exit cleanup; ASan and Wine.

## v0.104: TextBuf

Second owned resource with the same rules (`OrdinaryType::is_resource`): `tbuf_new()`, `tbuf_push(t, s)` (move,
in place), `tbuf_len(&t)` (Unicode scalars), `tbuf_finish(t) -> Text` (move, releases); `&TextBuf` parameters;
storage shared with `ByteBuf` in the C runtime. Tests: parity, 7 buffers released, 6 negative cases, exit cleanup.

## v0.109: File

Third resource, linear (never released implicitly): FILES.md.

## v0.130: explicitly bounded callbacks

`FnEffect` may capture ordinary immutable values. A captured owner or an owned
resource inside any callable signature is still rejected, including a function
passed directly to `each_effect`. A callback may create, borrow, move and close
its own local resources under the existing rules. Type operands and field labels
are metadata, not captures. Checker, interpreter and compiler share lexical
capture analysis; the interpreter copies only free locals into a closure.
No general user-owned types, mutable references or lifetime polymorphism are added.
See [EFFECT_FUNCTIONS.md](EFFECT_FUNCTIONS.md).
