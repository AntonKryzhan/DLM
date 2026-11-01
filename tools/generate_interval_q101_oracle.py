"""Deterministic rational-interval fixtures from Python Fraction/isqrt (independent of DLM).

Each row: op, bits, x, y, value, source.
* x, y: intervals `[lo,hi]` (endpoints `n/d`), a rational (contains), a resolution
  exponent k (sqrt) or `-`.
* bits: the smallest precision with which the operation succeeds: the largest
  width among the operands and every checked intermediate value of the
  documented algorithm (docs/EXACT_INTERVALS.md); with bits-1 it must fail with a
  precision error.
* value: an interval, a rational (width, midpoint), true/false (contains,
  subset), -1/0/1/2 (compare), `empty` (intersect), `ERR:zero` (inv/div by an
  interval containing zero) or `ERR:negative` (sqrt of negative values).
* source: 0 host only, 1 also DLM source/native C, 2 also the bits-1 check there.
"""
from fractions import Fraction
from math import isqrt
from pathlib import Path
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

rows = []


def width(*values):
    out = 1
    for v in values:
        if isinstance(v, Fraction):
            out = max(out, abs(v.numerator).bit_length(), v.denominator.bit_length())
        else:
            out = max(out, abs(v).bit_length())
    return out


def rat(v):
    return f"{'-' if v < 0 else ''}{abs(v.numerator)}/{v.denominator}"


def iv(x):
    return f"[{rat(x[0])},{rat(x[1])}]"


def sqrt_bound(x, k, upper):
    """(value, checked intermediates) of one sqrt bound."""
    if x == 0:
        return Fraction(0), []
    p, q = x.numerator, x.denominator
    scale = 1 << k
    radicand = p * q * scale * scale
    s = isqrt(radicand)
    r = radicand - s * s
    numerator = s + 1 if upper and r else s
    steps = [2, scale, scale * scale, p * q, radicand, s, r, q * scale]
    if upper and r:
        steps.append(s + 1)
    value = Fraction(numerator, q * scale)
    return value, steps + [value]


def operate(op, x, y):
    lo, hi = x if isinstance(x, tuple) else (None, None)
    if op == "add":
        r = (lo + y[0], hi + y[1])
        return list(r), iv(r)
    if op == "sub":
        r = (lo - y[1], hi - y[0])
        return list(r), iv(r)
    if op == "neg":
        return [], iv((-hi, -lo))
    if op == "abs":
        if lo >= 0:
            r = (lo, hi)
        elif hi <= 0:
            r = (-hi, -lo)
        else:
            r = (Fraction(0), max(-lo, hi))
        return [], iv(r)
    if op == "mul":
        products = [lo * y[0], lo * y[1], hi * y[0], hi * y[1]]
        return products, iv((min(products), max(products)))
    if op == "square":
        a, b = lo * lo, hi * hi
        r = (Fraction(0) if lo <= 0 <= hi else min(a, b), max(a, b))
        return [a, b], iv(r)
    if op == "inv":
        if lo <= 0 <= hi:
            return [], "ERR:zero"
        r = (1 / hi, 1 / lo)
        return list(r), iv(r)
    if op == "div":
        if y[0] <= 0 <= y[1]:
            return [], "ERR:zero"
        inverse = (1 / y[1], 1 / y[0])
        products = [lo * inverse[0], lo * inverse[1], hi * inverse[0], hi * inverse[1]]
        return list(inverse) + products, iv((min(products), max(products)))
    if op == "sqrt":
        if lo < 0:
            return [], "ERR:negative"
        a, steps_a = sqrt_bound(lo, y, False)
        b, steps_b = sqrt_bound(hi, y, True)
        # the guarantee itself, checked here with exact arithmetic
        assert a * a <= lo and hi <= b * b and b >= a
        if y < 64:
            exact_a = Fraction(isqrt(lo.numerator * lo.denominator * 4 ** y), lo.denominator * 2 ** y)
            assert a == exact_a
        return steps_a + steps_b, iv((a, b))
    if op == "hull":
        return [], iv((min(lo, y[0]), max(hi, y[1])))
    if op == "intersect":
        a, b = max(lo, y[0]), min(hi, y[1])
        return [], "empty" if a > b else iv((a, b))
    if op == "contains":
        return [y], "true" if lo <= y <= hi else "false"
    if op == "subset":
        return [], "true" if y[0] <= lo and hi <= y[1] else "false"
    if op == "compare":
        if hi < y[0]:
            return [], "-1"
        if y[1] < lo:
            return [], "1"
        if lo == hi == y[0] == y[1]:
            return [], "0"
        return [], "2"
    if op == "width":
        return [hi - lo], rat(hi - lo)
    if op == "mid":
        return [lo + hi, Fraction(2), (lo + hi) / 2], rat((lo + hi) / 2)
    raise ValueError(op)


def row(op, x, y=None, source=0):
    steps, value = operate(op, x, y)
    operands = list(x) + (list(y) if isinstance(y, tuple) else [])
    bits = width(*(operands + steps))
    if isinstance(y, tuple):
        y_text = iv(y)
    elif isinstance(y, Fraction):
        y_text = rat(y)
    elif y is None:
        y_text = "-"
    else:
        y_text = str(y)
    rows.append((op, bits, iv(x), y_text, value, source))


points = [Fraction(0), Fraction(1), Fraction(-1), Fraction(1, 2), Fraction(-2, 3), Fraction(5, 7), Fraction(3), Fraction(-4)]
intervals = []
for i, a in enumerate(points):
    for j, b in enumerate(points):
        if a <= b and (i + j) % 3 != 1:
            intervals.append((a, b))
binary = ("add", "sub", "mul", "div", "hull", "intersect", "subset", "compare")
unary = ("neg", "abs", "square", "inv", "width", "mid")
for xi, x in enumerate(intervals):
    for op in unary:
        row(op, x, None, 1 if xi % 4 == 0 else 0)
    for k in (0, 1, 3, 10):
        row("sqrt", x, k, 2 if xi % 5 == 0 and k in (1, 10) else 0)
    for q in points[:5]:
        row("contains", x, q, 1 if xi % 7 == 0 else 0)
    for yi, y in enumerate(intervals):
        for op in binary:
            chosen = (xi + 3 * yi + len(op)) % 31 == 0
            row(op, x, y, (2 if (xi + yi) % 2 == 0 else 1) if chosen else 0)
# square roots of rational squares are exact; wide resolution and wide endpoints
for x in ((Fraction(1, 4), Fraction(9, 4)), (Fraction(0), Fraction(2)), (Fraction(2), Fraction(3)),
          (Fraction(16, 9), Fraction(25, 9))):
    for k in (0, 5, 64, 200):
        row("sqrt", x, k, 2 if k in (5, 64) else 0)
for k in (1000, 4000):
    row("sqrt", (Fraction(2), Fraction(3)), k, 0)
for bits_k, bits_j in ((64, 3), (255, 17), (700, 9), (4000, 7), (7000, 5)):
    p = Fraction((1 << bits_k) - 1, (1 << bits_j) + 1)
    q = Fraction(-((1 << (bits_k // 2)) + 3), (1 << (bits_j + 1)) - 1)
    x, y = (q, p), (q / 3, p * 2)
    source = 1 if bits_k < 300 else 0
    for op in ("add", "sub", "mul", "div", "hull", "intersect", "compare"):
        row(op, x, y, source if op in ("add", "mul", "hull") else 0)
    for op in ("square", "abs", "width", "mid"):
        row(op, x, None, 0)
    row("sqrt", (Fraction(0), p), 16, source)
# zero-containing divisors and touching intervals
row("inv", (Fraction(0), Fraction(1)), None, 1)
row("inv", (Fraction(-1), Fraction(0)), None, 1)
row("div", (Fraction(1), Fraction(2)), (Fraction(-1), Fraction(1)), 1)
row("intersect", (Fraction(0), Fraction(1)), (Fraction(1), Fraction(2)), 1)
row("intersect", (Fraction(0), Fraction(1)), (Fraction(3, 2), Fraction(2)), 1)
row("compare", (Fraction(1), Fraction(1)), (Fraction(1), Fraction(1)), 1)
row("compare", (Fraction(0), Fraction(1)), (Fraction(1), Fraction(2)), 1)
row("square", (Fraction(-3), Fraction(2)), None, 2)
row("sqrt", (Fraction(-1), Fraction(4)), 3, 1)

target = Path(__file__).resolve().parents[1] / "crates/dlm_core/tests/fixtures/interval_q101.tsv"
target.write_text("# op\tbits\tx\ty\tvalue\tsource\n" +
                  "".join("\t".join(map(str, r)) + "\n" for r in rows), encoding="utf-8")
print(f"{len(rows)} vectors; {sum(1 for r in rows if r[-1])} source/C vectors; "
      f"{sum(1 for r in rows if r[-1] == 2)} source boundary checks; max bits {max(r[1] for r in rows)}; "
      f"max source bits {max(r[1] for r in rows if r[-1])}")
