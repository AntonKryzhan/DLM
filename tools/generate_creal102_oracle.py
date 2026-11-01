"""Deterministic computable-real fixtures from Python Fraction/isqrt and Decimal (independent of DLM).

The generator evaluates each program with the documented refinement schedule
(docs/COMPUTABLE_REALS.md): square-root resolution k = (n + 8) * 2^r for r < 6
while k <= 16384, closed rational interval arithmetic with exact endpoints, a
retry when a divisor interval contains zero or a radicand interval straddles
zero, a retry when the width exceeds 2^-n, a domain error when the bounds prove
it. It asserts itself, against a 900-digit Decimal evaluation of the program,
that every successful result contains the real number and has width <= 2^-n.

Each row: kind, bits, n, x, y, value, source.
* kind: `approx` (x a program, y `-`) or `compare` (x, y programs).
* bits: the smallest precision with which the request succeeds: the largest
  width among every constant, every checked intermediate of every evaluated
  interval operation in every round, the tolerance 2^-n and the width; with
  bits-1 it must fail with a precision error. `-` for UNKNOWN rows.
* value: an interval `[lo,hi]`, -1/0/1/2 (compare), `ERR:zero` (a divisor that
  is exactly zero), `ERR:negative` (a radicand whose interval is below zero) or
  `UNKNOWN` (the refinement limit or the precision is reached first).
* source: 0 host only, 1 also DLM source/native C, 2 also the bits-1 check there
  (UNKNOWN rows run there at 256 bits, where the precision ends the refinement).
"""
from decimal import Decimal, getcontext
from fractions import Fraction
from math import isqrt
from pathlib import Path
import random
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)
getcontext().prec = 900

OPS = {"c": 0, "add": 2, "sub": 2, "mul": 2, "neg": 1, "inv": 1, "div": 2, "sqrt": 1, "square": 1, "abs": 1}
# v0.104: further operations registered by generate_elementary104_oracle.py: name -> (arity, f(x, k, steps))
EXTRA = {}
WIDE = 16384


class Retry(Exception):
    pass


class Domain(Exception):
    pass


def width(values):
    out = 1
    for v in values:
        out = max(out, abs(v.numerator).bit_length(), v.denominator.bit_length())
    return out


def rat(v):
    return f"{'-' if v < 0 else ''}{abs(v.numerator)}/{v.denominator}"


def iv(x):
    return f"[{rat(x[0])},{rat(x[1])}]"


def parse(text):
    nodes = []
    for part in text.split(" ; "):
        words = part.split(" ")
        assert words[0] in OPS
        if words[0] == "c":
            nodes.append(("c", Fraction(words[1])))
        else:
            args = [int(w) for w in words[1:]]
            assert len(args) == OPS[words[0]] and all(a < len(nodes) for a in args)
            nodes.append((words[0], *args))
    assert 1 <= len(nodes) <= 64
    return nodes


def sqrt_bound(x, k, upper, steps):
    if x == 0:
        return Fraction(0)
    p, q = x.numerator, x.denominator
    scale = 1 << k
    radicand = p * q * scale * scale
    s = isqrt(radicand)
    r = radicand - s * s
    numerator = s + 1 if upper and r else s
    steps += [Fraction(v) for v in (2, scale, scale * scale, p * q, radicand, s, r, q * scale)]
    if upper and r:
        steps.append(Fraction(s + 1))
    value = Fraction(numerator, q * scale)
    steps.append(value)
    return value


def evaluate(nodes, k, steps):
    vals = []
    for node in nodes:
        op = node[0]
        x = vals[node[1]] if OPS[op] >= 1 else None
        y = vals[node[2]] if OPS[op] == 2 else None
        if op in EXTRA:
            r = EXTRA[op][1](x, k, steps)
        elif op == "c":
            r = (node[1], node[1])
            steps.append(node[1])
        elif op == "add":
            r = (x[0] + y[0], x[1] + y[1])
            steps += list(r)
        elif op == "sub":
            r = (x[0] - y[1], x[1] - y[0])
            steps += list(r)
        elif op == "neg":
            r = (-x[1], -x[0])
        elif op == "abs":
            r = x if x[0] >= 0 else (-x[1], -x[0]) if x[1] <= 0 else (Fraction(0), max(-x[0], x[1]))
        elif op == "mul":
            p = [x[0] * y[0], x[0] * y[1], x[1] * y[0], x[1] * y[1]]
            steps += p
            r = (min(p), max(p))
        elif op == "square":
            a, b = x[0] * x[0], x[1] * x[1]
            steps += [a, b]
            r = (Fraction(0) if x[0] <= 0 <= x[1] else min(a, b), max(a, b))
        elif op in ("inv", "div"):
            d = x if op == "inv" else y
            if d[0] == 0 and d[1] == 0:
                raise Domain("ERR:zero")
            if d[0] <= 0 <= d[1]:
                raise Retry()
            inverse = (1 / d[1], 1 / d[0])
            steps += list(inverse)
            if op == "inv":
                r = inverse
            else:
                p = [x[0] * inverse[0], x[0] * inverse[1], x[1] * inverse[0], x[1] * inverse[1]]
                steps += p
                r = (min(p), max(p))
        elif op == "sqrt":
            if x[1] < 0:
                raise Domain("ERR:negative")
            if x[0] < 0:
                raise Retry()
            a = sqrt_bound(x[0], k, False, steps)
            b = sqrt_bound(x[1], k, True, steps)
            assert a * a <= x[0] and x[1] <= b * b
            r = (a, b)
        vals.append(r)
    return vals[-1]


def approx(nodes, n, steps):
    """(interval or error code) with every checked value appended to steps."""
    tolerance = Fraction(1, 1 << n)
    steps += [Fraction(2), Fraction(1 << n), tolerance]
    k = n + 8
    for _ in range(6):
        if k > 16384:
            break
        try:
            value = evaluate(nodes, k, steps)
        except Retry:
            value = None
        except Domain as e:
            return "UNKNOWN" if width(steps) > WIDE else str(e)
        if width(steps) > WIDE:
            return "UNKNOWN"
        if value is not None:
            steps.append(value[1] - value[0])
            if value[1] - value[0] <= tolerance:
                return value
        k *= 2
    return "UNKNOWN"


def truth(nodes):
    vals = []
    for node in nodes:
        op = node[0]
        x = vals[node[1]] if OPS[op] >= 1 else None
        y = vals[node[2]] if OPS[op] == 2 else None
        if op in EXTRA:
            r = EXTRA[op][1](x, k, steps)
        elif op == "c":
            v = Decimal(node[1].numerator) / Decimal(node[1].denominator)
        else:
            v = {
                "add": lambda: x + y,
                "sub": lambda: x - y,
                "mul": lambda: x * y,
                "neg": lambda: -x,
                "abs": lambda: abs(x),
                "square": lambda: x * x,
                "inv": lambda: 1 / x,
                "div": lambda: x / y,
                "sqrt": lambda: x.sqrt(),
            }[op]()
        vals.append(v)
    return vals[-1]


def check_enclosure(nodes, value, n):
    lo, hi = value
    assert hi - lo <= Fraction(1, 1 << n)
    t = truth(nodes)
    eps = Decimal(10) ** -800 * max(Decimal(1), abs(t))
    lo_d = Decimal(lo.numerator) / Decimal(lo.denominator)
    hi_d = Decimal(hi.numerator) / Decimal(hi.denominator)
    assert lo_d - eps <= t <= hi_d + eps, (nodes, n)


rows = []


def add_approx(text, n, source=0):
    nodes = parse(text)
    steps = []
    value = approx(nodes, n, steps)
    bits = width(steps)
    if isinstance(value, tuple):
        check_enclosure(nodes, value, n)
        if bits > WIDE:
            value = "UNKNOWN"
    if value == "UNKNOWN":
        rows.append(f"approx\t-\t{n}\t{text}\t-\tUNKNOWN\t{min(source, 1)}")
        return
    shown = iv(value) if isinstance(value, tuple) else value
    rows.append(f"approx\t{bits}\t{n}\t{text}\t-\t{shown}\t{source}")


def add_compare(a, b, n, source=0):
    na, nb = parse(a), parse(b)
    steps = []
    x = approx(na, n, steps)
    y = approx(nb, n, steps) if isinstance(x, tuple) else x
    bits = width(steps)
    if isinstance(x, tuple) and isinstance(y, tuple) and bits <= WIDE:
        check_enclosure(na, x, n)
        check_enclosure(nb, y, n)
        if x[1] < y[0]:
            value = "-1"
        elif y[1] < x[0]:
            value = "1"
        elif x[0] == x[1] == y[0] == y[1]:
            value = "0"
        else:
            value = "2"
        rows.append(f"compare\t{bits}\t{n}\t{a}\t{b}\t{value}\t{source}")
    elif x in ("ERR:zero", "ERR:negative") or y in ("ERR:zero", "ERR:negative"):
        rows.append(f"compare\t{bits}\t{n}\t{a}\t{b}\t{x if isinstance(x, str) else y}\t{source}")
    else:
        rows.append(f"compare\t-\t{n}\t{a}\t{b}\tUNKNOWN\t{min(source, 1)}")


if __name__ == "__main__":  # importable by generate_ccomplex103_oracle.py
    SQRT2 = "c 2 ; sqrt 0"
    NAMED = [
        (SQRT2, [0, 1, 8, 20, 64], 2),
        (SQRT2, [256, 1000, 4000], 0),
        ("c 2 ; sqrt 0 ; c 3 ; sqrt 2 ; add 1 3", [16], 2),
        ("c 2 ; sqrt 0 ; c 3 ; sqrt 2 ; add 1 3", [128], 0),
        ("c 5 ; sqrt 0 ; c 1 ; add 1 2 ; c 2 ; div 3 4", [32], 2),
        ("c 5 ; sqrt 0 ; c 1 ; add 1 2 ; c 2 ; div 3 4", [200], 0),
        ("c 2 ; sqrt 0 ; sqrt 1", [24], 2),
        ("c 2 ; sqrt 0 ; sqrt 1", [100], 0),
        ("c 7 ; sqrt 0 ; inv 1", [40], 2),
        ("c 7 ; sqrt 0 ; inv 1", [150], 0),
        ("c 2 ; sqrt 0 ; mul 1 1", [64], 2),
        ("c 4 ; sqrt 0", [0, 64], 2),
        ("c 1/3 ; c 1/6 ; add 0 1", [0, 30], 2),
        ("c -7/3 ; abs 0 ; c 2 ; sqrt 2 ; sub 1 3 ; square 4", [20], 2),
        ("c 1000 ; c 2 ; sqrt 1 ; mul 0 2", [0, 10], 2),
        ("c 2 ; sqrt 0 ; c 1414213/1000000 ; sub 1 2 ; inv 3", [0, 12], 2),
        ("c 2 ; sqrt 0 ; c 3 ; sqrt 2 ; mul 1 3 ; c 6 ; sqrt 5 ; sub 4 6 ; abs 7", [16], 1),
        ("c 0 ; sqrt 0 ; c 9/4 ; sqrt 2 ; add 1 3", [8], 2),
        ("c 0 ; inv 0", [8], 2),
        ("c 1 ; c 3 ; c 3 ; sub 1 2 ; div 0 3", [8], 2),
        ("c -1 ; sqrt 0", [8], 2),
        ("c 2 ; sqrt 0 ; c 2 ; sub 1 2 ; sqrt 3", [8], 2),
        ("c 2 ; sqrt 0 ; sub 1 1 ; inv 2", [8], 1),
        ("c 2 ; sqrt 0 ; mul 1 1 ; c 2 ; sub 2 3 ; sqrt 4", [8], 1),
    ]
    for text, precisions, source in NAMED:
        for n in precisions:
            add_approx(text, n, source)

    COMPARES = [
        (SQRT2, "c 7/5", 8, 2),
        (SQRT2, "c 3/2", 8, 2),
        ("c 2 ; sqrt 0 ; mul 1 1", "c 2", 64, 2),
        ("c 4 ; sqrt 0", "c 2", 16, 2),
        (SQRT2, "c 1414213562373095/1000000000000000", 20, 2),
        (SQRT2, "c 1414213562373095/1000000000000000", 80, 2),
        ("c 2 ; sqrt 0 ; c 3 ; sqrt 2 ; add 1 3", "c 10 ; sqrt 0", 40, 1),
        ("c 0 ; inv 0", SQRT2, 8, 1),
        ("c 2 ; sqrt 0 ; sub 1 1 ; inv 2", SQRT2, 8, 1),
    ]
    for a, b, n, source in COMPARES:
        add_compare(a, b, n, source)

    rng = random.Random(102)


    def random_program(size):
        parts = []
        for i in range(size):
            if i < 2 or rng.random() < 0.25:
                num = rng.randint(-20, 30)
                den = rng.randint(1, 9)
                parts.append(f"c {rat(Fraction(num, den))}")
                continue
            op = rng.choice(["add", "sub", "mul", "neg", "inv", "div", "sqrt", "sqrt", "square", "abs"])
            args = [rng.randrange(i) for _ in range(OPS[op])]
            parts.append(" ".join([op] + [str(a) for a in args]))
        return " ; ".join(parts)


    for i in range(260):
        add_approx(random_program(rng.randint(2, 12)), rng.choice([0, 4, 16, 32, 64]), 1 if i % 6 == 0 else 0)
    for i in range(40):
        add_compare(random_program(rng.randint(2, 6)), random_program(rng.randint(2, 6)), rng.choice([8, 24]), 1 if i % 5 == 0 else 0)

    out = Path(__file__).resolve().parent.parent / "crates/dlm_core/tests/fixtures/creal102.tsv"
    out.write_text("\n".join(rows) + "\n")
    print(len(rows), "rows;", sum(r.split("\t")[5] == "UNKNOWN" for r in rows), "unknown;",
          sum(r.split("\t")[5].startswith("ERR:") for r in rows), "domain;",
          sum(r.split("\t")[6] != "0" for r in rows), "source")
