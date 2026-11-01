# v0.88 — Independent Equational Proof Replay (Stage C foundation)

The first independently replayable fragment is equality over **mathematical Nat**: variables, nonnegative
literals, addition and multiplication. It does not certify machine arithmetic, memory management, termination
or native code. In particular, an algebraic identity does not establish `Defined_128` for its executions.

## Proof objects and rules

`KernelProof` is a flat, structured object: variable binders, a term DAG, a proof DAG, the goal's two term IDs
and the root proof ID. Every edge points backward. The checker validates every node, including unused nodes;
out-of-scope variables, unknown IDs, forward edges, cycles and a root that proves a different equation fail.
Duplicate structural terms are canonicalized internally; term-table numbering has no semantic significance.

| Proof rule | Checked premise / conclusion |
|---|---|
| `Refl(t)` | `t = t` |
| `Sym(p)` | Reverse the equation established by `p` |
| `Trans(p,q)` | The right side of `p` must equal the left side of `q` |
| `Add(p,q)`, `Mul(p,q)` | Congruence of addition/multiplication on both established equations |
| `Law(law,args)` | An instance of one declared Nat algebra law with exact arity and term IDs |
| `Eval(t)` | One addition/multiplication of literal Nat operands, or a literal itself |
| `Axiom(id)` | A declaration in an explicitly supplied external `KernelContext` |

The twelve algebra laws are left/right additive zero, multiplicative one and multiplicative zero;
commutativity and associativity of addition/multiplication; left/right distributivity. These are explicit
trusted kernel rules for the mathematical Nat model. No arbitrary rewrite string or symbolic success flag is
accepted as a rule.

The replay checker (`proof_kernel.rs`) does not call `symbolic.rs`, the runtime, a tactic or the source checker.
The small producer (`proof_kernel/builder.rs`) is separate: it emits reflexivity, direct algebra-law instances,
congruence, unit/zero reductions and literal evaluations. A failure to produce evidence is a completeness
limit; it does not weaken replay verification.

## Source integration and certificate strength

For `theorem ... by normalize` without `using`, over Nat parameters and an unbounded `nat_eq` goal, the checker
attempts this producer after its existing symbolic check. Only an artifact that independently replays **and**
renders exactly the certificate's proposition is attached. The source syntax and existing theorem acceptance
remain unchanged.

* Replay-backed certificates have `kernel: Some(KernelProof)`, certificate format v2, a `dlm-cert-v2-...`
  fingerprint covering the complete artifact, and `proof_evidence: kernel-replay`.
* Other certificates retain v1 fingerprints and `proof_evidence: legacy-trusted`. Their existing status
  `Checked` and audit result `Verified` describe the legacy contract; they do not mean independent replay.
* `CertificateAuditReport::independently_checked` is true only after a successful audit with kernel evidence.
  Replacing a step, changing the goal or substituting a different valid claim fails even after recomputing the
  fingerprint. Removing kernel evidence visibly downgrades the certificate to the legacy path.
* `dlm audit` includes `equational-replay-kernel` for replay-backed theorems. The existing symbolic/runtime
  pipeline remains visible; this patch does not claim that all source acceptance depends only on the new kernel.

## Export and independent checking

```text
dlm proof-export --trusted-only examples/valid/equational_replay.dlm Equations.identity > identity.kproof
dlm proof-check identity.kproof
```

`proof-export` emits exactly one artifact with no CLI framing. The theorem name must be qualified. It fails
when the selected theorem has no independently replayable evidence; it never exports a legacy trace as a proof.
`proof-check` reads only the artifact and requires no source file, imports, tactic execution or runtime input.
Its output names the proposition, checked node counts, empty axiom dependencies and mathematical model.

`examples/proofs/identity.kproof` is a generated, checked sample. It can be passed directly to `proof-check`;
the checker does not need the corresponding `.dlm` file.

The single-artifact format begins with `DLM-KERNEL-PROOF v1`, declares `rules nat-semiring-v1` separately, and
ends with `END-DLM-KERNEL-PROOF`. Between them
are `var`, `term`, `proof`, `goal` and `root` lines in that order. IDs are zero-based positions in their tables.
Unknown versions/rules, wrong arities, repeated goal/root fields, trailing data and malformed IDs are rejected.
`KernelProof::from_text` parses; `check_kernel_proof` establishes logical validity. Parsing alone is not checking.

## Assumptions and budgets

The API can check an `Axiom` step only with an external `KernelContext`. Each declaration owns its term table
and binders, so changing an untrusted proof's numbering cannot change the authorized assumption. This first
version requires the same binder scope and supports no axiom instantiation/substitution. The report retains
the names of all axiom steps used anywhere in the DAG, conservatively including unused steps.

Serialized artifacts cannot declare their own axioms. Source certificate attachment and `proof-check` use an
empty context, so inserting an axiom step is rejected. Existing source-level axioms keep their legacy admitted
certificates and visible taint; the new feature never clears that taint.

Default limits: 50,000 term nodes (including internally derived terms), 20,000 proof nodes, depth 256,
200,000 counted operations, 1,024 variables, and 4 MiB of artifact/proposition text. Checks traverse flat tables
iteratively. Proposition rendering separately bounds expansion of shared subterms. The producer limits its
source traversal depth to 96.

`Invalid` means a violated logical/structural rule. `UnknownWithinBudget` means a size, depth, work or numeric
capacity limit; it is not a proof of falsity. Literal checking currently uses exact u128 capacity: an evaluation
requiring a larger mathematical integer returns unknown. Algebra rules may still prove equalities about that
expression without evaluating it; machine definedness remains separate.

## Evidence and remaining work

`equational_kernel.rs` tests all algebra laws against an independent finite evaluator, composition, round trips,
mutations, binder scope, explicit assumptions, numerical/resource limits, DAG expansion and malformed text.
`cli_proof_replay.rs` exports a proof, removes its source, checks the artifact, then rejects a damaged artifact.
Certificate mutation tests recompute the fingerprint before checking rejection.

Stage C remains **partial**. Definition unfolding, arbitrary polynomial witnesses, lemma instantiation,
substitution, logical connectives, nested quantifiers, Nat/list induction, Int/list proofs and their source
elaboration are not independently replayed yet. They retain the documented legacy path. This kernel itself
has implementation tests, not a machine-checked soundness proof. Source proof replay does not certify the C
backend, C runtime, external compiler or operating system.

## C-min acceptance (0.101.0)

The bounded transition criterion C-min (ROADMAP.md) is accepted: each condition is mapped to checks in
`crates/dlm_core/tests/c_min_acceptance.rs` (one test per condition) and `crates/dlm_cli/tests/cli_c_min.rs`.

| Condition | Evidence |
|---|---|
| 1. DAG checked without source checker, runtime or search | a hand-written proof (no producer, no source) is checked from its text alone; a false claim cannot be replayed |
| 2. Scopes, references, exact root/goal match; tampering rejected after rehash | out-of-scope variables, forward/self references, forward premises, swapped goal, other or missing root, wrong law arguments, duplicate or malformed binders; certificates with a swapped goal, replaced root step, another valid proof or permuted binders are rejected after the fingerprint is recomputed |
| 3. Axioms only from an explicit external context | an axiom step fails with the empty context and succeeds only with a supplied declaration, which stays visible in `used_axioms`; declarations own their binders; the text form has no declaration line (an inserted one is refused); the producer never emits axiom steps; `proof-check` rejects an artifact with an axiom step |
| 4. Versions, rules, cycles, damaged format, extra data rejected; budget ≠ falsity | 21 malformed variants (versions, unknown rules/laws, arity, signs, leading zeros, extra spaces, CR line ends, blank lines, misplaced/duplicate fields, trailing data, empty input); cyclic premises; step/node/work/depth/text budgets and exact-capacity evaluation give UnknownWithinBudget; `proof-check` exits 1 for Invalid, 3 for UnknownWithinBudget, 2 for unreadable input |
| 5. Replay after source deletion; audit separates kernel-replay from legacy-trusted | `cli_proof_replay.rs` (export, delete source, replay, reject damage); certificates of replayed, legacy and axiom-using theorems carry `kernel-replay` / `legacy-trusted`; the audit lists both the replay kernel and the symbolic kernel and keeps axiom dependencies |
| 6. Explicit guarantee boundary | a kernel-replayed theorem about a function body leaves its machine-arithmetic obligation open and its overflow failure listed; the program compiles to byte-identical C with or without the theorem and still fails with the defined overflow error; `proof-check` names the mathematical Nat model |

v0.101 tightened the codec: an artifact must be the canonical text of the parsed proof (`to_text(from_text(t)) =
t`), so blank lines, other whitespace, CR line ends, `+` signs and leading zeros are refused and a proof has one
spelling. C-min covers exactly the `nat-semiring-v1` fragment; full stage C (definitions, lemmas, substitution,
connectives, quantifiers, induction) remains Open.
