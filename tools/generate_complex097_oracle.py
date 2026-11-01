"""Deterministic Q(i) fixtures from Python Fraction (independent of DLM).

Each row: op, bits, z, w, value, source.
* bits: the smallest precision with which the operation succeeds: the largest
  numerator/denominator width among the operands and every intermediate
  rational of the documented algorithm (docs/EXACT_COMPLEX.md). With bits-1
  the operation must fail with a precision error.
* value: canonical text `<re>(+|-)<|im|>i` with `n/d` components, a rational
  `n/d` (norm), `true`/`false` (eq) or `ERR:zero` (division by zero).
* source: 0 host only, 1 also DLM source/native C, 2 also the bits-1 check there.
"""
from fractions import Fraction
from pathlib import Path
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

rows = []


def width(*values):
    return max([1] + [max(abs(v.numerator).bit_length(), v.denominator.bit_length()) for v in values])


def rat(v):
    return f"{'-' if v < 0 else ''}{abs(v.numerator)}/{v.denominator}"


def text(z):
    re, im = z
    return f"{rat(re)}{'-' if im < 0 else '+'}{rat(abs(im))}i"


def norm_steps(z):
    a, b = z
    aa, bb = a * a, b * b
    return [aa, bb, aa + bb], aa + bb


def operate(op, z, w):
    """(intermediates, value text) following the documented algorithms."""
    a, b = z
    c, d = w if w is not None else (Fraction(0), Fraction(0))
    if op == "add":
        r = (a + c, b + d)
        return list(r), text(r)
    if op == "sub":
        r = (a - c, b - d)
        return list(r), text(r)
    if op == "neg":
        return [], text((-a, -b))
    if op == "conj":
        return [], text((a, -b))
    if op == "mul":
        ac, bd, ad, bc = a * c, b * d, a * d, b * c
        return [ac, bd, ad, bc, ac - bd, ad + bc], text((ac - bd, ad + bc))
    if op == "norm":
        steps, n = norm_steps(z)
        return steps, rat(n)
    if op == "eq":
        return [], "true" if (a, b) == (c, d) else "false"
    if op == "div":
        if c == 0 and d == 0:
            return [], "ERR:zero"
        steps, n = norm_steps(w)
        ac, bd = a * c, b * d
        s = ac + bd
        bc, ad = b * c, a * d
        t = bc - ad
        return steps + [ac, bd, s, bc, ad, t, s / n, t / n], text((s / n, t / n))
    if op == "inv":
        if a == 0 and b == 0:
            return [], "ERR:zero"
        steps, n = norm_steps(z)
        return steps + [a / n, b / n], text((a / n, -b / n))
    raise ValueError(op)


def row(op, z, w=None, source=0):
    steps, value = operate(op, z, w)
    operands = list(z) + (list(w) if w is not None else [])
    bits = width(*(operands + steps))
    rows.append((op, bits, text(z), text(w) if w is not None else "-", value, source))


small = [Fraction(0), Fraction(1), Fraction(-1), Fraction(1, 2), Fraction(-2, 3), Fraction(5, 7), Fraction(3), Fraction(-4)]
complexes = [(small[i], small[j]) for i in range(len(small)) for j in range(len(small)) if (i + 2 * j) % 5 == 0 or i == j]
for zi, z in enumerate(complexes):
    for op in ("neg", "conj", "norm", "inv"):
        row(op, z, None, 1 if zi % 3 == 0 else 0)
    for wi, w in enumerate(complexes):
        for op in ("add", "sub", "mul", "div", "eq"):
            chosen = (zi + 3 * wi + len(op)) % 23 == 0
            boundary = chosen and (zi + wi) % 2 == 0
            row(op, z, w, 2 if boundary else (1 if chosen else 0))
# special values: i*i = -1, units, conjugate products, zero divisors
i, one, zero = (Fraction(0), Fraction(1)), (Fraction(1), Fraction(0)), (Fraction(0), Fraction(0))
row("mul", i, i, 2)
row("div", one, i, 2)
row("inv", i, None, 2)
row("div", one, zero, 1)
row("inv", zero, None, 1)
row("eq", zero, (Fraction(0), Fraction(-0)), 1)
row("mul", (Fraction(3), Fraction(4)), (Fraction(3), Fraction(-4)), 2)
# wide components: (2^k-1)/(2^j+1)
# (these supplied vectors select narrower source cases for regression cost;
# the existing source_complex_q suite and integration test cover wider profiles)
for k, j, source_ops in ((64, 3, ("add", "mul", "div")), (255, 17, ("add", "mul")), (500, 11, ("add",)),
                         (1000, 5, ()), (4000, 7, ()), (8000, 9, ())):
    p = Fraction((1 << k) - 1, (1 << j) + 1)
    q = Fraction(-((1 << (k // 2)) + 3), (1 << (j + 1)) - 1)
    z, w = (p, q), (q, p)
    for op in ("add", "sub", "mul", "div", "eq"):
        row(op, z, w, (2 if k < 500 else 1) if op in source_ops else 0)
    for op in ("neg", "conj", "norm", "inv"):
        row(op, z, None, 0)
# a result exactly at the retained 1024-bit profile boundary
big = Fraction((1 << 512) - 1)
row("mul", (big, Fraction(0)), (big, Fraction(0)), 1)
row("norm", (Fraction((1 << 511) - 1), Fraction((1 << 511) - 1)), None, 1)

target = Path(__file__).resolve().parents[1] / "crates/dlm_core/tests/fixtures/complex_q097.tsv"
target.write_text("# op\tbits\tz\tw\tvalue\tsource\n" +
                  "".join("\t".join(map(str, r)) + "\n" for r in rows), encoding="utf-8")
print(f"{len(rows)} vectors; {sum(1 for r in rows if r[-1])} source/C vectors; "
      f"{sum(1 for r in rows if r[-1] == 2)} source boundary checks; max bits {max(r[1] for r in rows)}")
