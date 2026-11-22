# Substitution / Alpha-Equivalence Foundation

`v0.56.0` starts the second ordinary-mathematics layer after logical formulas and quantifiers.

The purpose is deliberately narrow: DLM now has auditable objects for variable scope, alpha-equivalence and capture-avoiding substitution, but this is not yet a full dependent term calculus.

## Main boundaries

```text
VariableScopeReport != Theorem
VariableScopeReport != StaticProof
AlphaEquivalenceReport != EqProof
AlphaEquivalenceReport != RewriteRule
SubstitutionReport != RewriteCertificate
SubstitutionReport != TruthClaim
```

The layer exists to prevent the next mathematical features from treating textual substitution as a harmless string replacement.

## New objects

```text
VariableOccurrence
VariableScopeReport
AlphaEquivalenceReport
SubstitutionReport
```

Statuses:

```text
AlphaEquivalenceStatus:
  equivalent
  not_equivalent

SubstitutionStatus:
  applied
  blocked_by_binder
  rejected_capture_risk
```

## Laws

```text
free variables are not bound variables
alpha-equivalence may rename binders only
substitution must not cross a shadowing binder
substitution must reject capture risk
proof/theorem/truth/runtime evidence cannot be substituted as formula text
Axiom/Oracle/Unsafe taint is preserved
```

## Example

```text
forall x:Nat. P(x)
```

The bound variable is `x`. It is not a free variable.

These are alpha-equivalent:

```text
forall x:Nat. P(x)
forall y:Nat. P(y)
```

This substitution is blocked because `x` is locally bound:

```text
[zero / x] forall x:Nat. P(x)
```

This substitution is rejected because it would capture `x`:

```text
[f(x) / y] forall x:Nat. P(y)
```

## Readiness delta

```text
Stage 2 — Ordinary mathematics of the language

Local readiness:         12–18% -> 18–24%
Architectural readiness: 35–45% -> 40–50%
Fundamental readiness:   22–32% -> 25–35%
```

This improves the mathematical foundation required before function types, forall/exists proof rules, dependent types and structural induction over user-defined data.
