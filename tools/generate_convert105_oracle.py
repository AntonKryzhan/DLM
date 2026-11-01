"""Fixtures for explicit numeric conversions (N11) and the portable format (N12).

Independent Python Fraction implementations of the rounding modes, decimal
formatting and parsing; computable-real conversions use the enclosures of the
creal/elementary reference algorithm with the documented precision schedule
(0, 8, 16, ... below max_n, then max_n) and are checked against mpmath.

Each row: kind, a, b, c, value, source (1 also in DLM/native C).
"""
from fractions import Fraction
from pathlib import Path
import importlib.util
import random

import mpmath

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("elementary104", here / "generate_elementary104_oracle.py")
el = importlib.util.module_from_spec(spec)
spec.loader.exec_module(el)
cr = el.cr
mpmath.mp.dps = 600
MODES = ["floor", "ceil", "trunc", "half_even"]


def rat(v):
    return cr.rat(v)


def rnd(q, mode):
    f = q.numerator // q.denominator  # floor
    r = q - f
    if mode == "floor" or r == 0:
        return f
    if mode == "ceil":
        return f + 1
    if mode == "trunc":
        return f if q >= 0 else f + 1
    if r > Fraction(1, 2):
        return f + 1
    if r < Fraction(1, 2):
        return f
    return f if f % 2 == 0 else f + 1


def fixed(i, digits):
    sign = "-" if i < 0 else ""
    m = str(abs(i))
    if digits == 0:
        return sign + m
    if len(m) <= digits:
        m = "0" * (digits + 1 - len(m)) + m
    return f"{sign}{m[:-digits]}.{m[-digits:]}"


def decimal(q, digits, mode):
    return fixed(rnd(q * 10 ** digits, mode), digits)


def decimal_exact(q):
    d = q.denominator
    twos = fives = 0
    while d % 2 == 0:
        d //= 2
        twos += 1
    while d % 5 == 0:
        d //= 5
        fives += 1
    if d != 1:
        return "ERR:nondecimal"
    return decimal(q, max(twos, fives), "trunc")


def schedule(max_n):
    out = [0]
    n = 8
    while n < max_n:
        out.append(n)
        n *= 2
    if max_n > 0:
        out.append(max_n)
    return out


def decided(text, max_n, f):
    nodes = cr.parse(text)
    for n in schedule(max_n):
        v = cr.approx(nodes, n, [])
        if isinstance(v, str):
            return v
        a, b = f(v[0]), f(v[1])
        if a == b:
            return a
    return "UNKNOWN"


rows = []


def add(kind, a, b, c, value, source=0):
    rows.append(f"{kind}\t{a}\t{b}\t{c}\t{value}\t{source}")


rng = random.Random(105)
QS = [Fraction(0), Fraction(5, 2), Fraction(-5, 2), Fraction(7, 2), Fraction(-7, 2), Fraction(1, 3), Fraction(-1, 3),
      Fraction(2, 3), Fraction(-2, 3), Fraction(10), Fraction(-10), Fraction(1, 8), Fraction(-125, 1000), Fraction(22, 7),
      Fraction(-1, 1 << 70), Fraction((1 << 100) + 1, 2)]
QS += [Fraction(rng.randint(-10 ** 6, 10 ** 6), rng.randint(1, 10 ** 4)) for _ in range(40)]
for i, q in enumerate(QS):
    for mode in MODES:
        add("round", rat(q), mode, "-", str(rnd(q, mode)), 1 if i < 16 else 0)
        for p in (0, 3, 20):
            v = Fraction(rnd(q * 2 ** p, mode), 2 ** p)
            add("dyadic", rat(q), mode, str(p), rat(v), 1 if i < 6 and p == 3 else 0)
        for digits in (0, 2, 7):
            add("decimal", rat(q), mode, str(digits), decimal(q, digits, mode), 1 if i < 10 and digits == 2 else 0)
    add("decimal_exact", rat(q), "-", "-", decimal_exact(q), 1 if i < 16 else 0)
    add("integer_exact", rat(q), "-", "-", str(q.numerator) if q.denominator == 1 else "ERR:noninteger", 1 if i < 16 else 0)

DECIMALS = ["0", "-0", "12.5", "-0.125", "007", "1e3", "1.5e-3", "-2.50e1", "3.14159e0", "1e-4096", "5e4096",
            "1e4097", "", "-", "1.", ".5", "1e", "1e+3", "+1", "1,5", "1.2.3", "1e3.5", "١", "0.000"]
for i, t in enumerate(DECIMALS):
    try:
        if not t or t != t.strip():
            raise ValueError
        neg = t.startswith("-")
        body = t[1:] if neg else t
        mant, _, exp = body.partition("e")
        if "e" in body and (not exp or not (exp.lstrip("-").isascii() and exp.lstrip("-").isdigit()) or exp.startswith("--") or len(exp.lstrip("-")) > 5):
            raise ValueError
        ip, dot, fp = mant.partition(".")
        if not (ip.isascii() and ip.isdigit()) or (dot and not (fp.isascii() and fp.isdigit())):
            raise ValueError
        e = int(exp) if exp else 0
        if abs(e) > 4096:
            value = "ERR:exponent"
        else:
            v = Fraction(int(ip + fp)) * Fraction(10) ** (e - len(fp))
            value = rat(-v if neg else v)
    except ValueError:
        value = "ERR:invalid"
    add("parse_decimal", t if t else "<empty>", "-", "-", value, 0 if t in ("1e-4096", "5e4096") else 1)

PROGRAMS = ["c 5/2", "c -7/2", "c 2 ; sqrt 0", "pi", "c 1 ; exp 0", "c 2 ; log 0 ; neg 1", "c 2 ; sqrt 0 ; mul 1 1",
            "c 4 ; sqrt 0", "c 1 ; sin 0", "pi ; c 1000000 ; mul 0 1", "c 0 ; log 0", "c 2 ; sqrt 0 ; sub 1 1"]
for i, text in enumerate(PROGRAMS):
    truth = el.truth(cr.parse(text)) if text not in ("c 0 ; log 0",) else None
    for mode in MODES:
        v = decided(text, 64, lambda q: rnd(q, mode))
        if isinstance(v, int) and truth is not None:
            exact = {"floor": mpmath.floor, "ceil": mpmath.ceil, "trunc": lambda x: mpmath.floor(x) if x >= 0 else mpmath.ceil(x),
                     "half_even": mpmath.nint}[mode](truth)
            assert int(exact) == v, (text, mode, v, exact)
        add("creal_round", text, mode, "64", str(v), 1 if i < 9 and mode in ("floor", "half_even") else 0)
        for digits in (3, 10):
            v = decided(text, 64, lambda q: decimal(q, digits, mode))
            add("creal_decimal", text, mode, str(digits), v, 1 if i < 6 and digits == 3 and mode == "half_even" else 0)

# portable format: canonical texts and refusals
PORTABLE = [
    ("dlm1:int:0", "ok"), ("dlm1:int:-12345678901234567890", "ok"), ("dlm1:int:-0", "noncanonical"), ("dlm1:int:007", "noncanonical"),
    ("dlm1:int:+1", "invalid"), ("dlm1:rat:-3/4", "ok"), ("dlm1:rat:6/8", "noncanonical"), ("dlm1:rat:2", "noncanonical"),
    ("dlm1:rat:2/1", "ok"), ("dlm1:rat:1/0", "invalid"), ("dlm1:ivq:[-1/2,3/4]", "ok"), ("dlm1:ivq:[1/1,0/1]", "invalid"),
    ("dlm1:ivq:[0,1]", "noncanonical"), ("dlm1:cq:1/2;-3/1", "ok"), ("dlm1:cq:1/2", "invalid"), ("dlm1:creal:c 2/1 ; sqrt 0", "ok"),
    ("dlm1:creal:c 2 ; sqrt 0", "noncanonical"), ("dlm1:creal:pi ; sin 0", "ok"), ("dlm1:ccomplex:c 0/1 | c 1/1", "ok"),
    ("dlm1:ccomplex:c 0/1 |c 1/1", "invalid"), ("dlm2:int:1", "version"), ("dlm:int:1", "invalid"), ("DLM1:int:1", "invalid"),
    ("dlm1:float:1", "family"), ("dlm1:int", "invalid"), ("int:1", "invalid"), ("", "invalid"), ("dlm1:int:1 ", "invalid"),
]
for i, (t, v) in enumerate(PORTABLE):
    add("portable", t if t else "<empty>", "-", "-", v, 1)

out = here.parent / "crates/dlm_core/tests/fixtures/convert105.tsv"
out.write_text("\n".join(rows) + "\n")
print(len(rows), "rows;", sum(r.endswith("\t1") for r in rows), "source")
