"""Fixtures for elementary functions of computable reals (numeric criterion N10).

The reference algorithm (docs/ELEMENTARY_FUNCTIONS.md) works on closed rational
intervals rounded outward to dyadic numbers 2^-p: exp by halving and Taylor
series with squaring, log by binary scaling and the atanh series with ln 2 =
2 atanh(1/3), pi by Machin's formula, sin/cos by reduction modulo 2 pi, the mean
value form and alternating Taylor series. It is evaluated inside the refinement
schedule of computable reals (generate_creal102_oracle.py); every successful
interval is checked here against mpmath at 1000 digits and against 2^-n.

Each row: n, program, value, source. value: an interval, ERR:log (logarithm of
a non-positive value), ERR:zero / ERR:negative, or UNKNOWN. source: 1 also in
DLM/native C (all rows run at 4096 bits on the host, 1024 in the source).
"""
from fractions import Fraction
from pathlib import Path
import importlib.util
import random

import mpmath

spec = importlib.util.spec_from_file_location("creal102", Path(__file__).with_name("generate_creal102_oracle.py"))
cr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cr)
mpmath.mp.dps = 1000


class LogDomain(cr.Domain):
    pass


def bitlen(n):
    return abs(n).bit_length()


def rnd(x, p, up):
    v = x * (1 << p)
    q = v.numerator // v.denominator
    if up and q * v.denominator != v.numerator:
        q += 1
    return Fraction(q, 1 << p)


def out(a, p):
    return (rnd(a[0], p, False), rnd(a[1], p, True))


def mul(a, b):
    ps = [a[0] * b[0], a[0] * b[1], a[1] * b[0], a[1] * b[1]]
    return (min(ps), max(ps))


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def sub(a, b):
    return (a[0] - b[1], a[1] - b[0])


def scale(a, q):
    """a * q for a rational q (exact)."""
    return mul(a, (q, q))


def mag(a):
    return max(abs(a[0]), abs(a[1]))


def exp_point(x, w):
    if x == 0:
        return (Fraction(1), Fraction(1))
    if x < 0:
        e = exp_point(-x, w)
        return out((1 / e[1], 1 / e[0]), w)
    s = max(0, bitlen(x.numerator) - bitlen(x.denominator) + 2)
    wp = w + s + 8
    eps = Fraction(1, 1 << wp)
    y = out((x / (1 << s), x / (1 << s)), wp)
    t = (Fraction(1), Fraction(1))
    total = t
    i = 0
    while True:
        i += 1
        t = out(scale(mul(t, y), Fraction(1, i)), wp)
        total = add(total, t)
        if t[1] <= eps:
            break
    total = (total[0], total[1] + t[1])
    for _ in range(s):
        total = out(mul(total, total), wp)
    return out(total, w)


def atanh_series(z, wp):
    """atanh(z) for 0 <= z <= 1/3 (rounded outward); tail below the last term."""
    if z == 0:
        return (Fraction(0), Fraction(0))
    eps = Fraction(1, 1 << wp)
    power = out((z, z), wp)
    zz = mul(power, power)
    total = (Fraction(0), Fraction(0))
    k = 0
    while True:
        term = out(scale(power, Fraction(1, 2 * k + 1)), wp)
        total = add(total, term)
        if term[1] <= eps:
            break
        power = out(mul(power, zz), wp)
        k += 1
    return (total[0], total[1] + term[1])


def log_point(x, w):
    m = bitlen(x.numerator) - bitlen(x.denominator)
    y = x / Fraction(2) ** m
    if y < 1:
        m -= 1
        y *= 2
    wp = w + bitlen(m) + 8
    a = atanh_series((y - 1) / (y + 1), wp)
    half_ln2 = atanh_series(Fraction(1, 3), wp)
    return out(add(scale(a, 2), scale(half_ln2, Fraction(2 * m))), w)


def atan_recip(q, wp):
    """atan(1/q) for an integer q >= 2: alternating series."""
    eps = Fraction(1, 1 << wp)
    z = Fraction(1, q)
    zz = z * z
    power = (z, z)
    total = (Fraction(0), Fraction(0))
    k = 0
    while True:
        term = out(scale(power, Fraction(1, 2 * k + 1)), wp)
        total = add(total, term) if k % 2 == 0 else sub(total, term)
        if term[1] <= eps:
            break
        power = out(scale(power, zz), wp)
        k += 1
    return (total[0] - term[1], total[1] + term[1])


def pi_interval(w):
    wp = w + 8
    a = atan_recip(5, wp)
    b = atan_recip(239, wp)
    return out(sub(scale(a, Fraction(16)), scale(b, Fraction(4))), w)


def taylor(m, w, cosine):
    """sin(m) or cos(m) for a rational |m| <= 4: alternating series."""
    wp = w + 8
    eps = Fraction(1, 1 << wp)
    mi = out((m, m), wp)
    mm = mul(mi, mi)
    t = (Fraction(1), Fraction(1)) if cosine else mi
    total = t
    k = 0
    while True:
        k += 1
        d = (2 * k - 1) * (2 * k) if cosine else (2 * k) * (2 * k + 1)
        t = out(scale(mul(t, mm), Fraction(-1, d)), wp)
        total = add(total, t)
        if mag(t) <= eps and k >= 2:
            break
    b = mag(t)
    return out((total[0] - b, total[1] + b), w)


def trig_point(c, w, cosine):
    if abs(c) <= 4:
        return taylor(c, w, cosine)
    e = max(0, bitlen(c.numerator) - bitlen(c.denominator) + 1)
    wr = w + e + 8
    p = pi_interval(wr)
    q = c / (2 * p[0]) + Fraction(1, 2)
    j = q.numerator // q.denominator
    reduced = out(sub((c, c), scale(p, Fraction(2 * j))), wr)
    return trig_interval(reduced, w, cosine)


def trig_interval(x, w, cosine):
    if x[0] == x[1]:
        return trig_point(x[0], w, cosine)
    mid = rnd((x[0] + x[1]) / 2, w + 8, False)
    r = max(x[1] - mid, mid - x[0])
    s = trig_point(mid, w, cosine)
    lo, hi = s[0] - r, s[1] + r
    return out((max(lo, Fraction(-1)), min(hi, Fraction(1))), w)


def op_exp(x, k, steps):
    return (exp_point(x[0], k)[0], exp_point(x[1], k)[1])


def op_log(x, k, steps):
    if x[1] <= 0:
        raise LogDomain("ERR:log")
    if x[0] <= 0:
        raise cr.Retry()
    return (log_point(x[0], k)[0], log_point(x[1], k)[1])


cr.OPS.update({"exp": 1, "log": 1, "sin": 1, "cos": 1, "pi": 0})
cr.EXTRA.update({
    "exp": (1, op_exp),
    "log": (1, op_log),
    "sin": (1, lambda x, k, s: trig_interval(x, k, False)),
    "cos": (1, lambda x, k, s: trig_interval(x, k, True)),
    "pi": (0, lambda x, k, s: pi_interval(k)),
})


def truth(nodes):
    vals = []
    for node in nodes:
        op = node[0]
        x = vals[node[1]] if cr.OPS[op] >= 1 else None
        y = vals[node[2]] if cr.OPS[op] == 2 else None
        v = {
            "c": lambda: mpmath.mpf(node[1].numerator) / node[1].denominator if op == "c" else None,
            "add": lambda: x + y, "sub": lambda: x - y, "mul": lambda: x * y, "neg": lambda: -x,
            "inv": lambda: 1 / x, "div": lambda: x / y, "sqrt": lambda: mpmath.sqrt(x),
            "square": lambda: x * x, "abs": lambda: abs(x), "exp": lambda: mpmath.exp(x),
            "log": lambda: mpmath.log(x), "sin": lambda: mpmath.sin(x), "cos": lambda: mpmath.cos(x),
            "pi": lambda: +mpmath.pi,
        }[op]()
        vals.append(v)
    return vals[-1]


rows = []


def row(text, n, source=0):
    nodes = cr.parse(text)
    steps = []
    try:
        value = cr.approx(nodes, n, steps)
    except LogDomain:
        value = "ERR:log"
    if isinstance(value, tuple):
        lo, hi = value
        assert hi - lo <= Fraction(1, 1 << n)
        t = truth(nodes)
        eps = mpmath.mpf(10) ** -900 * max(1, abs(t))
        assert mpmath.mpf(lo.numerator) / lo.denominator - eps <= t <= mpmath.mpf(hi.numerator) / hi.denominator + eps, (text, n)
        if max(bitlen(lo.numerator), bitlen(lo.denominator), bitlen(hi.numerator), bitlen(hi.denominator)) > 2000:
            return
        value = cr.iv(value)
    rows.append(f"{n}\t{text}\t{value}\t{source}")


if __name__ == "__main__":  # importable by generate_convert105_oracle.py
    NAMED = [
        ("pi", [0, 8, 64, 200], [0, 8]),
        ("c 1 ; exp 0", [0, 8, 64, 200], [8]),
        ("c -1 ; exp 0", [8, 64], [8]),
        ("c 1/3 ; exp 0", [16], [16]),
        ("c 100 ; exp 0", [8], []),
        ("c -100 ; exp 0", [8], []),
        ("c 0 ; exp 0", [8], [8]),
        ("c 2 ; log 0", [0, 8, 64, 200], [8]),
        ("c 1 ; log 0", [8], [8]),
        ("c 1/10 ; log 0", [16], [16]),
        ("c 1000000 ; log 0", [16], []),
        ("c 0 ; log 0", [8], [8]),
        ("c -2 ; log 0", [8], [8]),
        ("c 2 ; sqrt 0 ; c 2 ; sqrt 2 ; sub 1 3 ; log 4", [4], [4]),
        ("c 1 ; sin 0", [0, 8, 64, 200], [8]),
        ("c 1 ; cos 0", [8, 64], [8]),
        ("c 100 ; sin 0", [16], [16]),
        ("c -7/2 ; cos 0", [16], [16]),
        ("pi ; sin 0", [8, 32], [8]),
        ("pi ; c 2 ; div 0 1 ; cos 2", [8], [8]),
        ("c 1 ; exp 0 ; log 1", [8, 32], [8]),
        ("c 2 ; log 0 ; exp 1", [8], []),
        ("c 1/2 ; sin 0 ; square 1 ; c 1/2 ; cos 3 ; square 4 ; add 2 5", [8], [8]),
        ("c 1 ; c 4 ; c 1 ; div 1 2 ; c 239 ; div 0 4 ; sub 3 5 ; c 4 ; mul 6 7", [8], []),
        ("c 2 ; sqrt 0 ; exp 1 ; sin 2", [8], [8]),
        ("c 1000000 ; sin 0", [8], []),
        ("c 1 ; c 2 ; sqrt 1 ; mul 0 2 ; cos 3", [8], []),
    ]
    for text, precisions, source in NAMED:
        for n in precisions:
            row(text, n, 1 if n in source else 0)

    rng = random.Random(104)


    def random_program():
        parts = []
        size = rng.randint(2, 6)
        for i in range(size):
            if i == 0 or rng.random() < 0.25:
                parts.append(f"c {cr.rat(Fraction(rng.randint(-12, 12), rng.randint(1, 4)))}")
                continue
            o = rng.choice(["exp", "log", "sin", "cos", "add", "mul", "sub", "sqrt", "square", "pi", "neg"])
            parts.append(" ".join([o] + [str(rng.randrange(i)) for _ in range(cr.OPS[o])]))
        return " ; ".join(parts)


    count = 0
    while count < 220:
        text = random_program()
        if not any(f in text for f in ("exp", "log", "sin", "cos", "pi")):
            continue
        row(text, rng.choice([0, 4, 12, 24]), 1 if count % 20 == 0 else 0)
        count += 1

    out_path = Path(__file__).resolve().parent.parent / "crates/dlm_core/tests/fixtures/elementary104.tsv"
    out_path.write_text("\n".join(rows) + "\n")
    print(len(rows), "rows;", sum(r.split("\t")[2] == "UNKNOWN" for r in rows), "unknown;",
          sum(r.split("\t")[2].startswith("ERR:") for r in rows), "domain;", sum(r.endswith("\t1") for r in rows), "source")
