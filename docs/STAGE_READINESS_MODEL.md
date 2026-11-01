# Stage Readiness Model — Local / Architectural / Fundamental Gates

This document adds a standing readiness model for DLM / ЯРД development. It is not a feature of the language runtime. It is a project-control mechanism used to decide when a stage may advance, when it must be hardened, and when a previous stage must be revisited.

The main rule:

```text
"move to the next stage" does not mean "the previous stage is perfect".
It means the previous stage has passed the minimum gate required to stress-test it with the next layer.
```

DLM is deliberately staged. A stage can be locally complete while still not being fundamentally complete. This is normal for a formal language: quantifiers, functions, dependent types, modules, stdlib and runtime will expose weaknesses in earlier metatheory.

---

## 1. Three readiness dimensions

### 1.1. Local readiness

Local readiness measures whether the current implementation works as a patch/release artifact.

Questions:

```text
Does it compile?
Do unit/regression tests pass?
Are diagnostics wired?
Are docs updated?
Are examples and invalid cases covered?
Is the working tree clean after commit/tag/push?
```

Typical evidence:

```text
cargo check
cargo test
feature-specific tests
CLI smoke tests
invalid examples fail as expected
docs/test matrix updated
```

Local readiness is about implementation closure, not deep mathematical finality.

### 1.2. Architectural readiness

Architectural readiness measures whether the stage fits the long-term language architecture.

Questions:

```text
Does it preserve proof/truth/runtime boundaries?
Does it respect trust monotonicity?
Does it integrate with Passport, TypeKind, diagnostics and docs?
Does it avoid ad-hoc checker-only logic?
Does it support later IR/proof/audit/compiler layers?
Does it avoid hidden axioms or hidden unsafe transitions?
```

Typical evidence:

```text
centralized rules instead of duplicated logic;
explicit TypeKind / DiagnosticKind coverage;
passport helpers preserve taint;
audit artifacts do not become theorems/proofs;
fingerprints/order-sensitive evidence are stable;
roadmap position is explicit.
```

Architectural readiness answers: can the next layer be built on top without immediately redesigning everything?

### 1.3. Fundamental readiness

Fundamental readiness measures whether the stage is robust as part of the final mathematical foundation.

Questions:

```text
Is the semantics fully specified?
Has it survived interaction with quantifiers/functions/substitution/dependent types?
Is there a small proof-kernel interpretation?
Are preservation/progress or equivalent metatheorems formulated?
Can the layer survive stdlib, modules, runtime and compiler erasure?
Has it had external review or formalization?
```

Typical evidence:

```text
formal semantics;
machine-checkable kernel rules;
property/fuzz coverage;
large corpus of invalid cases;
stdlib usage pressure;
compiler/runtime integration;
external audit.
```

Fundamental readiness is intentionally harder to reach than local readiness.

---

## 2. Percentage bands

Use these bands consistently:

```text
0–20%    sketch / idea only
20–40%   prototype, not yet a stable base
40–60%   usable but incomplete, expected redesign pressure
60–75%   MVP-capable, can support limited next-layer work
75–90%   strong MVP gate, safe to build the next stage with known caveats
90–95%   release-candidate quality for this layer
95–100%  reserved for formally specified, stress-tested, externally reviewed maturity
```

A layer should not be called `100%` merely because its tests pass. Test success mostly raises local readiness. Fundamental readiness needs much stronger evidence.

---

## 3. Current project readiness map

Since v0.87 readiness is tracked per law and stage by evidence statuses (Designed / Implemented / Validated /
Proven / Open) in `LAW_STATUS.md`, as law 42 requires; percentages without a measurable model are no longer
updated. The table below is kept as the historical record up to v0.86.1.

These numbers were control estimates, not marketing claims.

| Stage | Local readiness | Architectural readiness | Fundamental readiness | Control status |
|---|---:|---:|---:|---|
| 1. Metamathematical foundation | 90–95% after full `v0.54` hotfix validation | 80–85% | 55–65% | MVP gate can close; still expected to be refined under later pressure |
| 2. Ordinary mathematics of the language | 96–98% after `v0.85` | 92–95% | 68–74% | functions, lambdas, higher-order functions, simple, lexicographic and mutual recursion, `let`, `match`, multi-line declarations, Text with operations, exact `Int` with `-` and Euclidean division, Option/Result, List/Sequence, products, records, sums usable from `.dlm`; rationals and fixed-width integers remain |
| 3. Proof/audit architecture | 76–82% after `v0.81` | 78–84% | 52–60% | certificates from `.dlm` statements (`dlm certify`), trusted-base audits with transitive lemma/axiom attribution across modules (`dlm audit`), project-level checking (`dlm project`) |
| 4. Full proof kernel | 55–62% after `v0.79` | 64–70% | 40–48% | computation, exhaustion, symbolic normalization, Nat (step s) and List<Nat> induction with generalized hypotheses, lemmas, inequality and Bool hypotheses, Bool case split, through core induction objects; kernel fuzz-tested with mutation checks and every theorem cross-checked by the runtime; no proof terms independent of the kernel |
| 5. Standard library | 18–24% after `v0.80` | 32–40% | 18–26% | module system with imports, theory extension and private declarations; `lib/std` with `std.nat` and `std.list` (20 proved theorems); options, records, sorting and text still missing |
| 6. Runtime / production execution | 36–42% after `v0.86.1` | 42–50% | 28–36% | interpreter and native executables run complete command-line programs: `fn main`, exit status, stdin lines, arguments, Text, Int; compiled programs reclaim memory per call; Linux and Windows x64 (MinGW-w64) |
| 7. High-performance native compilation | 30–38% after `v0.86.1` | 42–50% | 24–32% | `dlm build`: IR with proof/measure/fuel erasure, closure conversion, C backend matching `dlm run`, native scalar functions, tagged small numbers, call regions with survivor groups, self tail calls as loops, machine-range overflow report; no IR optimisation, research layer not compiled |

---

## 4. Required readiness note for every future stage

Since v0.87 the normative status is criterion-based, as required by laws 39/42.
The percentage model in sections 1–3 is historical; do not produce a new
percentage unless the scope, weighting and measurement method are fixed first.

Each new patch or stage records:

```text
Stage / criterion ID:
  Designed:   contract and acceptance condition (if applicable)
  Implemented: exact supported scope and version
  Validated:  reproducible commands, results and target platform
  Proven:     exact formally proved property, model and trusted base, if any
  Open:       known limitations, dependencies and closure conditions
Application impact: what ordinary programs can do after this patch
```

Mathematical section inventories (such as N01–N16) retain their separate
closed/open counts; a completed numerical criterion does not automatically
close any compiler stage A–H.

---

## 5. Transition policy

Advance from a stage only after its explicitly scoped acceptance conditions
have reproducible evidence; unresolved prerequisites must remain visible.
There are no universal `85%` or `75%` thresholds. A narrow proof-kernel
milestone may close while the broader stage remains open. Later features may
reveal additional obligations, which are recorded without rewriting history.

Revisit a stage if a later change exposes a trust/soundness violation, breaks
compiler/interpreter parity, invalidates ownership or ABI rules, or fails
negative tests. Do not defer safety-critical defects merely to maintain the
planned version sequence.

---

## 6. Interpretation for DLM right now

The metamathematical foundation can be treated as an MVP gate, not a final mathematical theory.

Meaning:

```text
It is strong enough to start ordinary mathematics.
It is not finished forever.
It will be revisited when quantifiers, functions, substitution, dependent types, stdlib and compiler erasure apply pressure.
```

This prevents two opposite mistakes:

```text
Mistake A: staying forever in foundation work and never building the language.
Mistake B: calling the foundation 100% complete before later layers test it.
```

The intended development rhythm is:

```text
build a layer;
close its MVP gate;
build the next layer;
observe pressure;
return to harden earlier layers;
update criterion evidence and open obligations.
```

## Согласование инженерной и математической очередей (0.98)

Основной инженерный порядок и ограниченный переходный критерий C-min заданы
в [ROADMAP.md](../ROADMAP.md#c-min). N14 зависит от D/E, N15 — от C и правил
конкретного числового семейства. Общая реализация учитывается один раз,
приёмка каждого критерия остаётся отдельной. Полный C сохраняет статус Open.
Числовой раздел сохраняет 6/16 закрытых и 10/16 открытых критериев; Q(i) и
все записи проверки 0.98 сохранены. Последние 705 тестов относятся к проверке
реализации 0.98, а изменение документации не является новым полным прогоном.
