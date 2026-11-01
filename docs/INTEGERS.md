# v0.85 — Int: Exact Signed Integers

```text
fn diff(a: Nat, b: Nat) -> Int = int(a) - int(b)
theorem square_expand(a: Int, b: Int): int_eq(sq(a - b), sq(a) - mul(int(2), mul(a, b)) + sq(b)) by normalize
```

## Values and literals

`Int` is an exact signed integer. At run time it is an `i128` (in the interpreter and in compiled programs);
leaving that range is a runtime error, never a wrap-around.

- An unsigned literal is a `Nat`: `5`.
- A negated literal is an `Int`: `-5`, `-170141183460469231731687303715884105728`.
- `int(n)` converts a Nat to an Int: `int(5)`.

Int and Nat are never mixed implicitly: `int(3) + 2` is a check error (`E0937`) with a hint to write `int(2)`.

## Operators

| syntax | meaning |
|---|---|
| `a + b` | Int + Int -> Int (also Nat + Nat) |
| `a - b` | Int - Int -> Int. **Nat has no subtraction**: use `monus(a, b)` (truncated at 0) or `int(a) - int(b)` |
| `-a` | Int negation; `-7` is a constant |
| `a > b` | Int comparison (also Nat) |
| `mul(a, b)` | Int * Int -> Int (also Nat) |

`+` and binary `-` share one precedence level and associate to the left: `a - b + c - d` is
`((a - b) + c) - d`. A `-` is unary at the start of an expression or after another operator, a comma or an
opening parenthesis: `a - -b`, `f(-1)`. `->` and `=>` are never subtraction. In the AST, `a - b` is the call
`__sub(a, b)` and `-a` is `__neg(a)`; both render back as operators.

## Builtins

| builtin | type |
|---|---|
| `int(n)` | `Nat -> Int` |
| `abs(i)` | `Int -> Nat` |
| `int_eq(a, b)` | `Int, Int -> Bool` |
| `div(a, b)`, `mod(a, b)` | `Int, Int -> Int` or `Nat, Nat -> Nat` |
| `int_to_text(i)` | `Int -> Text` (decimal, `-` for negatives) |
| `text_to_int(t)` | `Text -> Option<Int>` (optional leading `-`, then decimal digits; `none` otherwise or out of range) |

Division is total and Euclidean:

```text
div(a, 0) = 0        mod(a, 0) = a
a = mul(div(a, b), b) + mod(a, b)      0 <= mod(a, b) < abs(b)   (b != 0)
div(-7, 2) = -4   mod(-7, 2) = 1   div(7, -2) = -3   mod(7, -2) = 1
```

`print_decimal`, `print_symbolic` and `print_line(int_to_text(..))` print Int values in decimal.

## Proofs

Theorems may quantify over `Int` variables. The symbolic kernel normalizes Int expressions as polynomials with
integer coefficients:

- ring identities (`sub_self`, `square_expand`, antisymmetry of `int(a) - int(b)`) are proved by `by normalize`;
- `int_eq(x, y)` and `x > y` are decided only when `x - y` normalizes to a **constant**: `i + int(1) > i` is
  proved, `a + a > a` is rejected (it is false for `a <= 0`), and so is `mul(a, a) + int(1) > int(0)`. Int atoms
  carry no sign assumption, so no Nat positivity reasoning is ever applied to Int;
- `abs(x)` and `abs(-x)` normalize to the same term; `div(x, 1) = x`, `mod(x, 1) = 0`, `div(x, 0) = 0`,
  `mod(x, 0) = x`; constant divisions are folded with Euclidean semantics;
- when a theory or a goal involves Int, an irreducible term of unknown type is no longer treated as a Nat
  (`strict_opaque`), so Nat facts such as `t >= 0` cannot leak onto Int terms;
- `by induction` on an Int variable is refused; enumeration (`theorem` without `by`) over Int is refused.

Every symbolic theorem is cross-checked by the runtime: Int variables range over `-5 ..= 5` (11 values per
variable). A disagreement is `E0909` "kernel/runtime disagreement".

The kernel is fuzzed against the runtime (`tests/integers.rs`): 3000 random Int statements with Int functions,
Euclidean division, constant and variable divisors, `abs` and negation; deliberate kernel mutations (Nat
positivity on Int, `abs` dropped, truncating instead of Euclidean division, a wrong divisor rule) are caught.

## Compilation

`dlm build` compiles Int: a separate `DV_INT` value (`__int128`) with overflow-checked `+`, `-`, `mul` and
negation, Euclidean `div`/`mod` with the same zero rules, `abs`, `int_eq`, and the text conversions. Negative
constants become IR constants (`int(-3)`). Overflow messages are the same in `dlm run` and in compiled programs.
The differential tests compare compiled and interpreted output on `examples/valid/integers.dlm` and on generated
Int programs.

## Examples

- `examples/valid/integers.dlm` — functions, `match` on Int, theorems, printing, `main`.
- `examples/invalid/nat_subtraction.dlm` — `a - b` on Nat.
- `examples/invalid/int_nat_mixing.dlm` — `Int + Nat`.
- `examples/invalid/int_positivity_unproved.dlm` — `a + a > a` is not a theorem over Int.
