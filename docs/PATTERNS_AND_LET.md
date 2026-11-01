# v0.82 — Multi-line Declarations, `let`, `match`, Case Splitting

## Logical lines

Declarations can span several lines. A physical line continues on the next one while parentheses or braces are
open, or when it ends with `=`, `,`, `=>`, `+`, `:`, ` in` or ` by`. A line that starts with `by ` continues the
statement above it, so a proof clause can stand on its own line. Block headers (`theory ... {`, `bridge ... {`,
`mutual {`) and `}` always end a line. Diagnostics report the first physical line.

```dlm
theory Lines {
    fn fact(n: Nat) -> Nat decreasing n =
        match n {
            0 => 1,
            succ(k) => mul(n, fact(k))
        }

    theorem fact_positive(n: Nat):
        fact(n) > 0
        by induction n
}
```

## `let`

```text
let name = value in body
```

`value` is evaluated once and bound in `body`; bindings are lexical and may shadow. `let` and `match` extend to the
end of the expression. Inside an operator expression they must be parenthesized: `1 + (let x = n in x + x)`.
Parentheses also group ordinary expressions now: `(a + b) + c`.

| layer | meaning |
|---|---|
| checker | infers `value`, checks `body` with the binding in scope |
| runtime | evaluates `value` once (effects happen once) |
| symbolic kernel | substitutes the normalized value |
| recursion check | a binding of `pred(m)` / `monus(m, k)` / `tail(m)` of the measured parameter `m` is a *decreased alias*; calls may pass the alias, or `pred` / `monus` / `tail` of it. A `let` cannot shadow the measured parameter or a recursive function |

## `match`

```text
match scrutinee {
    pattern => expr,
    ...
}
```

| kind | patterns |
|---|---|
| Nat | `0`, `1`, ... literals, `succ(k)` |
| List / Sequence | `[]`, `cons(h, t)` |
| Option | `none`, `some(x)` |
| Bool | `true`, `false` |
| any | `_`, `x` (binds the whole value) |

Arms are tried in order. A match must be **exhaustive**: it ends with `_`/`x`, or covers `0` and `succ`, `[]` and
`cons`, `none` and `some`, or `true` and `false`. Every arm must be **reachable**: nothing after a catch-all, no
duplicate patterns, no positive literal after `succ(_)`. Mixing pattern kinds is an error.

`match` is desugared by the parser, so the checker, runtime and kernel see no new construct:

```text
match n { 0 => A, succ(k) => B }           if_then_else(nat_eq(n, 0), A, let k = pred(n) in B)
match xs { [] => A, cons(h, t) => B }      if_then_else(is_empty(xs), A, let t = tail(xs) in let h = __match_head(xs) in B)
match o { none => A, some(x) => B }        if_then_else(is_none(o), A, let x = __match_some(o) in B)
match e { ... }   (e not a variable)       let __match = e in ...
```

`__match_head` and `__match_some` are produced only inside the branch where the list is non-empty or the option
is `some`. Names starting with `__` are reserved and cannot be written in source. Recursion through patterns
satisfies the termination check directly: `succ(k)` binds `k` to `pred(n)` under the `nat_eq(n, 0)` guard, and
`cons(_, t)` binds `t` to `tail(xs)` under the `is_empty(xs)` guard.

## Nat case splitting in `by normalize`

A `match` produces conditions such as `nat_eq(n, 1)` that symbolic normalization cannot decide. When a goal fails
and its residual compares a still-symbolic Nat variable `n` (or `c + n`) with literals, `by normalize` splits `n`
into `0, 1, ..., c` and `n' + c + 1`, where `c` is the largest literal. This is a partition of Nat, so it is sound.
Every case is then proved. The split is bounded: depth 3, literals up to 16, at most 128 case obligations. The
evidence shows it as `normalize:cases[n<=1]+...`.

```dlm
theory Clamp {
    fn clamp(n: Nat) -> Nat = match n { 0 => 0, 1 => 1, _ => 2 }
    theorem clamp_le(n: Nat): not(clamp(n) > 2) by normalize
}
```

## Soundness testing

`symbolic_kernel_soundness.rs` fuzzes 2500 statements with `let`, `match`, `if_then_else(nat_eq(n, c), ...)` and
offset comparisons. Every proved statement is evaluated by the runtime for `n < 14`, well beyond every split
threshold. A split that skips the case `n = 0` is caught.
