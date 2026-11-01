"""Deterministic fixtures for complex numbers with computable real components (N09).

Components are computable-real programs; each operation builds the component
programs of its result by the construction in docs/COMPUTABLE_COMPLEX.md, and
both components are approximated with the schedule of generate_creal102_oracle.py.
The generator checks the result against complex arithmetic carried out
independently on 900-digit Decimal values of the operands.

Each row: kind, bits, n, x, y, program, value, source.
* kind: `add`, `sub`, `mul`, `div`, `neg`, `conj`, `inv` (value: `[re] | [im]`),
  `abs`, `abs_sq` (value: an interval) or `equal` (value: 1 apart, 0 the same
  exact point, 2 undecided). `x`, `y`: complex texts `<re> | <im>` (`-` unused).
* program: the canonical text of the built result (`-` for `equal`).
* bits: the smallest precision of the approximation (as in creal102; `-` for UNKNOWN).
* value may also be ERR:zero / ERR:negative / UNKNOWN as for computable reals.
* source: 0 host only, 1 also DLM/native C, 2 also the bits-1 check there.
"""
from decimal import Decimal
from fractions import Fraction
from pathlib import Path
import importlib.util
import random

spec = importlib.util.spec_from_file_location("creal102", Path(__file__).with_name("generate_creal102_oracle.py"))
cr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cr)
OPS = cr.OPS


def embed(acc, nodes):
    off = len(acc)
    for node in nodes:
        if node[0] == "c":
            acc.append(node)
        else:
            acc.append((node[0], *[a + off for a in node[1:]]))
    return len(acc) - 1


def op(acc, name, *args):
    acc.append((name, *args))
    return len(acc) - 1


def text(nodes):
    parts = []
    for node in nodes:
        parts.append(f"c {cr.rat(node[1])}" if node[0] == "c" else " ".join([node[0]] + [str(a) for a in node[1:]]))
    return " ; ".join(parts)


def build(parts, ops):
    acc = []
    roots = [embed(acc, p) for p in parts]
    ops(acc, roots)
    return acc


def modsq(acc, x, y):
    return op(acc, "add", op(acc, "square", x), op(acc, "square", y))


def compose(kind, z, w):
    a, b = z
    c, d = w if w else (None, None)
    if kind in ("add", "sub"):
        return build([a, c], lambda t, r: op(t, kind, r[0], r[1])), build([b, d], lambda t, r: op(t, kind, r[0], r[1]))
    if kind == "neg":
        return build([a], lambda t, r: op(t, "neg", r[0])), build([b], lambda t, r: op(t, "neg", r[0]))
    if kind == "conj":
        return list(a), build([b], lambda t, r: op(t, "neg", r[0]))
    if kind == "mul":
        return (build([a, b, c, d], lambda t, r: op(t, "sub", op(t, "mul", r[0], r[2]), op(t, "mul", r[1], r[3]))),
                build([a, b, c, d], lambda t, r: op(t, "add", op(t, "mul", r[0], r[3]), op(t, "mul", r[1], r[2]))))
    if kind == "inv":
        return (build([a, b], lambda t, r: op(t, "div", r[0], modsq(t, r[0], r[1]))),
                build([a, b], lambda t, r: op(t, "neg", op(t, "div", r[1], modsq(t, r[0], r[1])))))
    if kind == "div":
        def re(t, r):
            num = op(t, "add", op(t, "mul", r[0], r[2]), op(t, "mul", r[1], r[3]))
            op(t, "div", num, modsq(t, r[2], r[3]))

        def im(t, r):
            num = op(t, "sub", op(t, "mul", r[1], r[2]), op(t, "mul", r[0], r[3]))
            op(t, "div", num, modsq(t, r[2], r[3]))
        return build([a, b, c, d], re), build([a, b, c, d], im)
    if kind == "abs_sq":
        return build([a, b], lambda t, r: modsq(t, r[0], r[1]))
    if kind == "abs":
        return build([a, b], lambda t, r: op(t, "sqrt", modsq(t, r[0], r[1])))
    raise ValueError(kind)


def dec(v):
    return Decimal(v.numerator) / Decimal(v.denominator)


def expected(kind, z, w):
    """Independent complex arithmetic on Decimal values of the operands."""
    a, b = cr.truth(z[0]), cr.truth(z[1])
    if w:
        c, d = cr.truth(w[0]), cr.truth(w[1])
    if kind == "add":
        return a + c, b + d
    if kind == "sub":
        return a - c, b - d
    if kind == "neg":
        return -a, -b
    if kind == "conj":
        return a, -b
    if kind == "mul":
        return a * c - b * d, a * d + b * c
    if kind == "inv":
        q = a * a + b * b
        return a / q, -b / q
    if kind == "div":
        q = c * c + d * d
        return (a * c + b * d) / q, (b * c - a * d) / q
    if kind == "abs_sq":
        return a * a + b * b
    if kind == "abs":
        return (a * a + b * b).sqrt()


def contains(interval, t):
    eps = Decimal(10) ** -800 * max(Decimal(1), abs(t))
    return dec(interval[0]) - eps <= t <= dec(interval[1]) + eps


def parse_complex(s):
    re, im = s.split(" | ")
    return cr.parse(re), cr.parse(im)


rows = []


def outcome(programs, n, steps):
    """Approximate the programs in order; the first failure ends the request."""
    values = []
    for p in programs:
        v = cr.approx(p, n, steps)
        if not isinstance(v, tuple):
            return v
        values.append(v)
    return values


def add(kind, x, y, n, source):
    z = parse_complex(x)
    w = parse_complex(y) if y != "-" else None
    if kind == "equal":
        steps = []
        v = outcome([z[0], z[1], w[0], w[1]], n, steps)
        if isinstance(v, str):
            value = v
        else:
            re = cr_order(v[0], v[2])
            im = cr_order(v[1], v[3])
            value = "1" if "apart" in (re, im) else "0" if re == im == "same" else "2"
        program = "-"
    else:
        built = compose(kind, z, w)
        programs = list(built) if isinstance(built, tuple) else [built]
        if any(len(p) > 64 for p in programs):
            return
        steps = []
        v = outcome(programs, n, steps)
        program = " | ".join(text(p) for p in programs)
        if isinstance(v, str):
            value = v
        else:
            truth = expected(kind, z, w)
            truth = truth if isinstance(truth, tuple) else (truth,)
            for interval, t, p in zip(v, truth, programs):
                assert interval[1] - interval[0] <= Fraction(1, 1 << n)
                assert contains(interval, t), (kind, x, y, n)
                assert contains(interval, cr.truth(p))
            value = " | ".join(cr.iv(i) for i in v)
    bits = cr.width(steps)
    if value == "UNKNOWN" or bits > cr.WIDE:
        rows.append(f"{kind}\t-\t{n}\t{x}\t{y}\t{program}\tUNKNOWN\t{min(source, 1)}")
    else:
        rows.append(f"{kind}\t{bits}\t{n}\t{x}\t{y}\t{program}\t{value}\t{source}")


def cr_order(p, q):
    if p[1] < q[0] or q[1] < p[0]:
        return "apart"
    if p[0] == p[1] == q[0] == q[1]:
        return "same"
    return "overlap"


R2 = "c 2 ; sqrt 0"
R3 = "c 3 ; sqrt 0"
BASE = [
    "c 1 | c 2",
    f"{R2} | c 1/3",
    f"c -3/2 | {R3}",
    "c 0 | c 1",
    "c 0 | c 0",
    "c 5 ; sqrt 0 ; c 1 ; add 1 2 ; c 2 ; div 3 4 | c -1/7",
    f"{R2} | {R2}",
    "c 3 | c -4",
]
for i, x in enumerate(BASE):
    for kind in ("neg", "conj", "inv", "abs", "abs_sq"):
        for n in (0, 16):
            add(kind, x, "-", n, 2 if i < 4 and n == 16 else 0)
    for j, y in enumerate(BASE):
        for kind in ("add", "sub", "mul", "div"):
            n = (0, 8, 24)[(i + j) % 3]
            add(kind, x, y, n, 2 if (i, j) in ((0, 1), (1, 2), (2, 3), (3, 3), (6, 7), (7, 0), (5, 1)) else 0)

EQUAL = [
    ("c 0 | c 1", "c 0 | c 1", 8, 2),
    ("c 1 | c 0", "c 1 | c 1", 8, 2),
    (f"{R2} | c 0", "c 7/5 | c 0", 8, 2),
    ("c 4 ; sqrt 0 | c 0", "c 2 | c 0", 8, 2),
    (f"{R2} ; mul 1 1 | c 0", "c 2 | c 0", 32, 2),
    (f"{R2} ; sub 1 1 ; inv 2 | c 0", "c 0 | c 0", 8, 1),
    (f"c 0 | {R2} ; sub 1 1", "c 0 | c 0", 16, 1),
]
for x, y, n, source in EQUAL:
    add("equal", x, y, n, source)
# i·i = −1 exactly, √2·√2 stays undecided, |3 − 4i| = 5 exactly
add("mul", "c 0 | c 1", "c 0 | c 1", 8, 2)
add("mul", f"{R2} | c 0", f"{R2} | c 0", 8, 2)
add("abs", "c 3 | c -4", "-", 8, 2)
add("inv", f"{R2} ; sub 1 1 | c 0", "-", 4, 1)

rng = random.Random(103)


def small():
    parts = []
    for i in range(rng.randint(1, 4)):
        if i == 0 or rng.random() < 0.3:
            parts.append(f"c {cr.rat(Fraction(rng.randint(-9, 9), rng.randint(1, 5)))}")
        else:
            o = rng.choice(["add", "mul", "neg", "sqrt", "square", "sub", "abs"])
            parts.append(" ".join([o] + [str(rng.randrange(i)) for _ in range(OPS[o])]))
    return " ; ".join(parts)


for i in range(150):
    kind = rng.choice(["add", "sub", "mul", "div", "inv", "abs", "abs_sq", "conj", "neg", "equal"])
    x = f"{small()} | {small()}"
    y = f"{small()} | {small()}" if kind in ("add", "sub", "mul", "div", "equal") else "-"
    add(kind, x, y, rng.choice([0, 4, 12, 20]), 1 if i % 10 == 0 else 0)

out = Path(__file__).resolve().parent.parent / "crates/dlm_core/tests/fixtures/ccomplex103.tsv"
out.write_text("\n".join(rows) + "\n")
print(len(rows), "rows;", sum(r.split("\t")[6] == "UNKNOWN" for r in rows), "unknown;",
      sum(r.split("\t")[6].startswith("ERR:") for r in rows), "domain;",
      sum(r.split("\t")[7] != "0" for r in rows), "source")
