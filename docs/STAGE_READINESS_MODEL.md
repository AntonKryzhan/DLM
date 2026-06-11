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

These numbers are control estimates, not marketing claims. They must be updated after major patches.

| Stage | Local readiness | Architectural readiness | Fundamental readiness | Control status |
|---|---:|---:|---:|---|
| 1. Metamathematical foundation | 90–95% after full `v0.54` hotfix validation | 80–85% | 55–65% | MVP gate can close; still expected to be refined under later pressure |
| 2. Ordinary mathematics of the language | 5–10% | 30–40% | 20–30% | next active construction stage |
| 3. Proof/audit architecture | 60–70% | 70–80% | 45–55% | many audit artifacts exist; needs deeper language/CLI integration |
| 4. Full proof kernel | 15–25% | 40–50% | 20–30% | minimal proof concepts exist; full kernel is not done |
| 5. Standard library | 10–15% | 25–35% | 15–25% | mostly future work |
| 6. Runtime / production execution | 15–25% | 30–40% | 20–30% | symbolic/runtime MVP only |
| 7. High-performance native compilation | 0–5% | 25–35% | 10–20% | documented strategic track, no backend yet |

---

## 4. Required readiness note for every future stage

Every future stage or large patch must update a small readiness block:

```text
Readiness delta:
  Local readiness:        old -> new, with test evidence
  Architectural readiness: old -> new, with integration evidence
  Fundamental readiness:   old -> new, with remaining caveats
```

Example:

```text
Readiness delta for Stage 2 / Quantifier MVP:
  Local:        5–10% -> 20–25% after parser/API/tests/docs
  Architectural: 30–40% -> 40–45% after substitution/scope model lands
  Fundamental: 20–30% -> 25–30% until formal substitution lemmas exist
```

---

## 5. Transition policy

A stage may move forward when:

```text
Local readiness >= 85% for the current gate;
Architectural readiness >= 75% for the current gate;
Fundamental readiness is explicitly documented with caveats;
all known critical compile/test failures are resolved;
the next stage is likely to reveal useful pressure, not hide broken foundations.
```

A stage must be revisited when:

```text
a later stage forces hidden axiom/trust/proof/runtime mixing;
substitution or quantifiers break existing theorem identity;
proof kernel rules cannot explain existing proof artifacts;
compiler/runtime erasure would hide trust or unsafe taint;
property/fuzz tests find non-monotonic trust or history loss.
```

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
update readiness scores.
```
