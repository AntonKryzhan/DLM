# v0.76 — Statements, Theorems and Proofs in `.dlm`

Stage 4 begins on top of the ordinary mathematics surface. The core proof pipeline existed only as a Rust API: statement,
goal, proof context, closure, theorem and certificate. It is now driven by `.dlm` syntax.

```text
theorem name: P                            closed P               proved by computation
theorem name(x: Nat, b: Bool) below N: P   forall x < N, b. P     proved by exhaustion
theorem name(b: Bool, c: Bool): P          forall b, c. P         proved by exhaustion (finite domain)
axiom name(x: T, ...): P                   forall x: T. P         admitted, trust=Axiom, validation=Assumed
conjecture name(x: Nat) tested N: P        forall x: Nat. P       tested below N; an open Goal, never a Theorem
```

```dlm
theory Arithmetic {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn double(n: Nat) -> Nat = n + n

    theorem sum_to_4: nat_eq(sum_to(4), 10)
    theorem gauss_below_200(n: Nat) below 200: nat_eq(mul(2, sum_to(n)), mul(n, n + 1))
    axiom add_comm(n: Nat, m: Nat): nat_eq(n + m, m + n)
    conjecture double_monus(n: Nat) tested 1000: nat_eq(monus(double(n), n), n)
}
```

## The central distinction

The same computation can back very different claims. DLM records which claim was made:

| declaration | proposition | object | trust |
|---|---|---|---|
| `theorem t(n: Nat) below 200: P` | `forall n: Nat < 200. P` | `Theorem` | unchanged (`Builtin`) |
| `conjecture c(n: Nat) tested 200: P` | `forall n: Nat. P` | `Goal` (open) | unchanged |
| `axiom a(n: Nat): P` | `forall n: Nat. P` | `Theorem` | `Axiom`, `validation=Assumed` |

A bounded theorem proves exactly the bounded proposition. Testing an unbounded proposition produces evidence, not a
proof: the result is a `Goal` that no theorem constructor accepts. An unbounded universal theorem without a bound is
rejected (`E0909`); it must be stated as an axiom or a conjecture.

## Pipeline

Every statement goes through the core:

```text
statement_passport(P) -> goal_passport(P) -> open_proof_context
  theorem:  evidence (proof:computation | proof:exhaustion) -> StaticProof<P> -> close_proof_with_static_proof
  axiom:    close_proof_by_axiom
  -> Theorem passport -> certificate_from_closure -> verify_certificate_against_theorem -> CheckReport.certificates
```

Conjectures stop after the evaluation: the open `Goal` carries the history event
`conjecture:<name>:tested:cases=<n>:below=<N>:no_counterexample`.

## Rules

- The body is a `Bool` expression over the statement variables, declared `fn`s, records, builtins and lambdas.
- The body must be pure and total (`E0909`), by the same effect analysis as function contracts.
- Statements may not reference theory values (`let`). Theory values can come from input, effects or axioms. To
  share data, use a nullary function: `fn xs() -> List<Nat> = list(1, 2, 3)`.
- Theorem and conjecture variables must be `Nat` or `Bool` (enumerable). Axiom variables may have any ordinary type.
- `below N` applies only when there are `Nat` variables. `Bool`-only theorems are finite and need no bound.
- At most `MAX_PROOF_CASES = 100000` assignments are evaluated. The count is checked before evaluation.
- A false theorem or a refuted conjecture is an error with the first counterexample in enumeration order, for example
  `counterexample a = 2, b = 3`.
- Statement names share the theory namespace. In expressions a theorem name denotes its `Theorem` passport, a proof
  object, which ordinary operations reject (`some(t)`, `t + 1`). At runtime it denotes `StaticProof<P>` (theorem,
  axiom) or `Prop<P>` (conjecture).

## Soundness argument and trusted base

Proof by computation is valid for the stated proposition because:

1. statement bodies use only pure total code: checked function contracts, measured recursion, total function values;
2. evaluation is deterministic, with exact `u128` arithmetic where overflow is an error, not wraparound;
3. exhaustion enumerates every assignment of the stated bounded domain.

The runtime evaluator (`Runtime::evaluate_statement`) is therefore part of the trusted base for these theorems, like
`decide`/`rfl` kernels in other systems. Evaluation runs on the runtime thread with the v0.74 stack, fuel and measure
checks.

## Certificates

`dlm certify file.dlm` checks the module and prints one `DLM-PROOF-CERTIFICATE v1` per theorem and axiom, followed by
the evidence event from the theorem history:

```text
theorem: gauss_below_200
proposition: forall n: Nat < 200. nat_eq(mul(2, sum_to(n)), mul(n, (n + 1)))
status: Checked
...
evidence: proof:exhaustion:below=200:cases=200:theorem=gauss_below_200
```

Under `--trusted-only` an axiom blocks certification. `dlm explain` reports proved theorems, axiom-admitted theorems
and open goals.

## Not yet in the surface

```text
symbolic proofs over Bool / List / Sequence variables (v0.77 kernel: Nat variables)
inequality reasoning with hypotheses (the induction hypothesis must be an equation)
strong / lexicographic induction tactics, generalizing the induction hypothesis
statements about theory values
```

## Readiness delta

```text
Stage 3 — Proof/audit architecture   Local 60–70% -> 66–74%   certificates produced from syntax and exported by CLI
Stage 4 — Full proof kernel          Local 15–25% -> 28–36%   statements/theorems/axioms/conjectures in syntax;
                                                              computation and exhaustion proofs; no symbolic proofs yet
```

# v0.77 — Symbolic Proofs of Unbounded Statements

```dlm
theory Symbolic {
    fn sum_to(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, n + sum_to(pred(n)))
    fn add(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add(pred(a), b) + 1)
    fn twice_add(a: Nat) -> Nat = add(a, a)

    theorem binomial(a: Nat, b: Nat): nat_eq(mul(a + b, a + b), mul(a, a) + mul(2, mul(a, b)) + mul(b, b)) by normalize
    theorem gauss(n: Nat): nat_eq(mul(2, sum_to(n)), mul(n, n + 1)) by induction n
    theorem add_spec(a: Nat, b: Nat): nat_eq(add(a, b), a + b) by induction a
    theorem twice_add_spec(a: Nat): nat_eq(twice_add(a), mul(2, a)) by normalize using add_spec
}
```

An unbounded theorem over `Nat` variables now needs a proof clause:

```text
theorem name(x: Nat, ...): P by normalize [using lemma, ...]
theorem name(x: Nat, ...): P by induction x [using lemma, ...]
```

## The symbolic kernel (`crates/dlm_core/src/symbolic.rs`)

Nat terms normalize to polynomials with integer coefficients over atoms: variables, or canonical keys of terms
the kernel does not reduce.

| construct | rule |
|---|---|
| `+`, `mul`, `^k` (literal k) | polynomial arithmetic |
| `pred(p)` | `p - 1` if `p >= 1` for every assignment, `0` if `p = 0`, otherwise an atom |
| `monus(a, b)` | `a - b` if `a - b >= 0` for every assignment, `0` if `a <= b` always, otherwise an atom |
| `nat_eq(a, b)` | `true` if `a - b = 0`, `false` if `a - b` is surely nonzero, otherwise undecided |
| `a > b` | decided by the sign of `a - b`, otherwise undecided |
| `if_then_else(c, t, e)` | the selected branch if `c` decides, otherwise an atom over all three parts |
| `and`, `or`, `not` | Boolean simplification |
| declared `fn` | unfolded by its definition. A measured `fn` is unfolded only when its measured Nat arguments are constant or surely positive, so its guards decide |
| named fn value, lambda | applied (beta-reduction) |
| anything else | an atom keyed by its normalized arguments |

A polynomial is "surely `>= c`" when its constant term is `>= c` and all other coefficients are nonnegative. Every
atom denotes a natural number.

**`by normalize`** succeeds when the statement normalizes to `true`.

**`by induction n`** proves:

- the base case `n = 0` by normalization;
- the step case `n = k + 1` with the induction hypothesis `P(k)`, other variables held fixed. An equational
  hypothesis is used as a ground rewrite when one side is a single atom (`sum_to(k) -> ...`). Otherwise the step
  goal is accepted when `lhs - rhs` of the goal is a rational multiple of `lhs - rhs` of the hypothesis.

**`using lemma, ...`** turns earlier theorems into rewrite rules, with the side that is a call of a declared `fn`
used as the pattern (`add(?a, ?b) -> ?a + ?b`). Lemmas must be unconditional:

| theorem kind | usable as lemma |
|---|---|
| closed theorem (computation) | yes, ground equation |
| symbolic theorem (`by normalize` / `by induction`) | yes |
| axiom | yes, and the new theorem becomes Axiom-tainted |
| bounded theorem (`below N`) | no: it holds only below its bound |
| conjecture | no: tested evidence is not a proof |

Failures report the residual normal form, for example
`step case n = n_k + 1 does not normalize to true; residual: 1 + 2*n_k = 1 + n_k (hypothesis: ...)`.

## Through the core

`by normalize` produces `StaticProof<P>` from kernel evidence and closes the goal through the v0.76 pipeline.

`by induction n` builds the core induction objects over a proposition family `P_name`:

```text
nat_induction_scheme(P_name)
  + StaticProof<P_name(0)>                                  (base, kernel evidence)
  + StaticProof<forall n:Nat. P_name(n) -> P_name(succ(n))> (step, kernel evidence)
  -> InductionProof<forall n:Nat. P_name(n)>
  -> instantiate P_name(n) := <statement body> -> StaticProof<P> -> Theorem -> certificate
```

The theorem history records the base and step evidence (`proof:symbolic:base|step:<method>:steps=N:lemmas=[...]`),
every `lemma:use:<name>`, and the family instantiation. `dlm certify` prints each certificate with its own evidence
and the lemmas used.

## Soundness argument and trusted base

Every kernel rule replaces a term by one equal under all Nat assignments (see the module documentation). Unfolding
uses the defining equations of pure total functions. Lemmas are proven or explicitly admitted equations. The
unfolding policy and the step budget (`SYMBOLIC_STEP_BUDGET = 20000`) affect completeness and termination only.

The kernel is part of the trusted base. It is checked by `tests/symbolic_kernel_soundness.rs`: 3000 random
normalization statements and 2000 random induction statements over `+`, `mul`, `pred`, `monus`, `if_then_else`
and recursive functions. About half are proved, and every proved statement is re-checked by the runtime on an
8×8 grid. A deliberate unsound mutation of the `pred` rule is caught by this test.

## Readiness delta (v0.77)

```text
Stage 4 — Full proof kernel   Local 28–36% -> 40–48%   symbolic normalize/induction/lemmas in syntax, core induction objects,
                                                     soundness fuzzing; Nat-only, equational hypotheses only
```

# v0.78 — Inequalities, List Induction and Runtime Cross-Check

```dlm
theory Lists {
    fn fact(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 1, mul(n, fact(pred(n))))
    fn len(xs: List<Nat>) -> Nat decreasing xs = if_then_else(is_empty(xs), 0, 1 + len(tail(xs)))
    fn inc_all(xs: List<Nat>) -> List<Nat> = map(xs, fun(x: Nat, x + 1))

    theorem fact_positive(n: Nat): fact(n) > 0 by induction n
    theorem square_ge(n: Nat): not(n > mul(n, n)) by induction n
    theorem len_is_length(xs: List<Nat>): nat_eq(len(xs), length(xs)) by induction xs
    theorem map_length(xs: List<Nat>): nat_eq(length(inc_all(xs)), length(xs)) by induction xs
}
```

## Inequalities

Goals and induction hypotheses may be inequalities. The kernel turns a proposition into a polynomial fact over Nat
atoms:

| proposition | fact |
|---|---|
| `nat_eq(a, b)` | `a - b = 0` |
| `a > b` | `a - b - 1 >= 0` |
| `not(a > b)` | `b - a >= 0` |

A goal fact `g >= 0` is accepted when:

- every coefficient of `g` is nonnegative (`normalize+nonnegativity`; every atom denotes a natural number), or
- in an induction step with hypothesis `h >= 0`, `d*g - c*h` has only nonnegative coefficients for some rational
  `c >= 0`, `d > 0`; with an equational hypothesis `h = 0`, `c` may be negative
  (`normalize+hypothesis_nonnegative_combination`).

The criterion is sound but incomplete: `not(n > mul(n, n))` fails `by normalize` (residual `-n + n^2 >= 0`) and
succeeds `by induction n`. A failing base case reports e.g. `base case n = 0 does not normalize to true; residual: -1 >= 0`.

## `List<Nat>` variables and structural induction

Symbolic theorems may quantify over `List<Nat>`. The kernel represents a list as known elements followed by an
optional opaque tail and evaluates `list`, `cons`, `tail`, `head`, `is_empty`, `length`, `map`, `fold`, `some`,
`none`, `unwrap_or`, `is_some`, `is_none` on the known prefix. A function measured by a list unfolds when the list is
known to be empty or known to be non-empty.

`by induction xs` for `xs: List<Nat>` proves:

- the base case `xs = []`;
- the step case `xs = cons(xs_h, xs_t)` with the hypothesis for `xs_t`, other variables fixed.

It goes through new core objects:

```text
list_induction_scheme(List<Nat>, P_name)               InductionScheme<List<Nat>,P_name>
  + StaticProof<P_name([])>                                                  -> BaseCase
  + StaticProof<forall h:Nat, t:List<Nat>. P_name(t) -> P_name(cons(h, t))>  -> StepCase
  -> InductionProof<forall xs:List<Nat>. P_name(xs)>
  -> instantiate P_name(xs) := <statement body> -> StaticProof<P> -> Theorem -> certificate
```

The cases are not interchangeable, and a Nat scheme is not accepted as a list scheme.

List theorems are never proved by testing: a `List<Nat>` theorem without a proof clause is rejected (`E0909`, only
Nat and Bool variables can be enumerated). Other element types are not supported yet.

## Runtime cross-check

After the kernel proves a theorem, the runtime, a separate implementation, evaluates the statement on a grid:
Nat variables in `0..6`, `List<Nat>` variables over all 40 lists of length `<= 3` with elements `< 3`. This is not
part of the proof. It is an independent check:

| result | effect |
|---|---|
| no counterexample | history event `crosscheck:runtime:agree:cases=N:theorem=<name>` |
| counterexample | hard error `kernel/runtime disagreement ... at n = 1`; the theorem is rejected |
| grid larger than 20000 cases, or a runtime error (e.g. fuel) | `crosscheck:runtime:skipped:...`, recorded |

A counterexample means either a false axiom used via `using` or a disagreement between kernel and runtime. The first
case is reachable from syntax, see `examples/invalid/false_axiom_caught_by_crosscheck.dlm`. `dlm certify` prints the
cross-check line under each certificate:

```text
evidence: proof:symbolic:step:normalize+hypothesis_nonnegative_combination:steps=2:lemmas=[]:theorem=fact_positive
crosscheck: crosscheck:runtime:agree:cases=6:theorem=fact_positive
```

## Soundness testing

`tests/symbolic_kernel_soundness.rs` now also fuzzes 3000 inequality statements (normalization and induction) and
2000 `List<Nat>` statements with `cons`, `tail`, `map`, `fold`, `head`, `length` and measured list functions. Every
proved statement is re-checked by the runtime. A unit test checks the combination rule semantically on 20000 random
polynomial pairs. Injected unsound mutations (negative multiplier of an inequality hypothesis; `length` ignoring the
known prefix) are caught.

## Not yet in the surface

```text
List<T> for T other than Nat; Bool variables in symbolic proofs
strong / lexicographic induction tactics, generalizing the induction hypothesis
proof terms re-checkable without the kernel
statements about theory values
```

## Readiness delta (v0.78)

```text
Stage 4 — Full proof kernel   Local 40–48% -> 48–56%   inequalities with hypotheses, List<Nat> + structural induction
                                                     through core list induction objects, runtime cross-check
```

# v0.79 — Step Induction, Generalized Hypotheses, Bool Cases

```dlm
theory Advanced {
    fn half(n: Nat) -> Nat decreasing n = if_then_else(nat_eq(n, 0), 0, if_then_else(nat_eq(n, 1), 0, half(monus(n, 2)) + 1))
    fn even(n: Nat) -> Bool decreasing n = if_then_else(nat_eq(n, 0), true, if_then_else(nat_eq(n, 1), false, even(monus(n, 2))))
    fn add_acc(a: Nat, b: Nat) -> Nat decreasing a = if_then_else(nat_eq(a, 0), b, add_acc(pred(a), b + 1))
    fn pick(b: Bool, n: Nat) -> Nat = if_then_else(b, n + 1, n)

    theorem half_bound(n: Nat): not(mul(2, half(n)) > n) by induction n step 2
    theorem even_or_next(n: Nat): or(even(n), even(n + 1)) by induction n step 2
    theorem add_acc_spec(a: Nat, b: Nat): nat_eq(add_acc(a, b), a + b) by induction a generalizing b
    theorem pick_ge(b: Bool, n: Nat): not(n > pick(b, n)) by normalize
}
```

```text
by induction n [step s] [generalizing x, ...] [using lemma, ...]
```

## `step s` (Nat, `1 <= s <= 4`)

Proves the bases `P(0) .. P(s-1)` and the step `n = k + s` with the hypotheses `P(k) .. P(k+s-1)`. This covers
functions that recurse by `monus(n, 2)`, where the step from `k` to `k + 1` gets stuck on an undecided guard
(`half_bound` fails with plain induction, residual `... ite(nat_eq(1 + n_k,1), ...) ...`). Each hypothesis is used as
a ground rewrite, alone in a combination, or summed with the others.

It goes through new core objects:

```text
nat_step_induction_scheme(P, s)                           InductionScheme<Nat,step=s,P>   (s >= 2)
  + BaseCase<P(0)> .. BaseCase<P(s-1)>                    in order, all required
  + StepCase<forall n:Nat. P(n) and .. and P(n+s-1) -> P(n+s)>
  -> InductionProof<forall n:Nat. P(n)>
```

## `generalizing x, ...`

The induction hypothesis holds for all values of the listed variables, not only for the fixed ones. The kernel
installs it as a rewrite rule with those variables as pattern variables, so accumulator functions are provable
(`add_acc(k, b + 1)` is rewritten with the hypothesis at `b + 1`). The family instantiation records the quantifier:
`P_add_acc_spec(a) := forall b: Nat. nat_eq(add_acc(a, b), (a + b))`. The induction variable and Bool variables
cannot be generalized.

## Bool variables and Bool hypotheses

Symbolic theorems may have Bool variables. They are split by cases outside the proof (at most 6 variables, 64 cases);
the evidence method shows `cases=N+...`, and a failing case is named (`goal [b = false]`). Induction on a Bool variable
is rejected.

A Bool induction hypothesis whose normal form is irreducible (for example `or(even(k), even(k+1))`) is recorded as a
truth: the same term in the step goal becomes `true`.

## Kernel guards

- A rewrite that re-enters the same lemma on the same arguments is not applied again. Before v0.79 a lemma
  `h(n) = g(n)` with `g` unfolding to `h(n)` overflowed the native stack and aborted the process.
- Normalization depth is limited to `MAX_SYMBOLIC_DEPTH = 384`. Whole proofs run on a kernel thread with a 256 MiB
  stack (`KERNEL_STACK_BYTES`), so a deep definition chain yields `normalization nested deeper than 384`, not an abort.

## Soundness testing

The fuzz suite adds 2500 statements over Nat and Bool variables with `half`, `even`, accumulator functions and
`if_then_else(b, ...)`, each tried by normalization and four induction variants (`step 1/2` × plain/generalized).
Every proved statement is re-checked by the runtime. Injected mutations are caught: generalizing the induction
variable, dropping a base case, taking a Bool hypothesis from the goal instead of the hypothesis, and checking only
one Bool case.

## Readiness delta (v0.79)

```text
Stage 4 — Full proof kernel   Local 48–56% -> 55–62%   step induction, generalized hypotheses, Bool cases and
                                                     hypotheses, kernel stack/cycle guards
```
