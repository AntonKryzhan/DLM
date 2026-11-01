"""Deterministic Q(i) oracles from Python Fraction pairs (no binary floats)."""
from fractions import Fraction as Q
from pathlib import Path
import random
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

rows = []
source_rows = []


def text(q):
    return f"{q.numerator}/{q.denominator}"


def words(n):
    out = []
    while n:
        out.append(str(n & 65535))
        n >>= 16
    return "list(" + ", ".join(out) + ")" if out else "empty_list(Nat)"


def literal(q):
    return f"BigRational({str(q < 0).lower()}, {words(abs(q.numerator))}, {words(q.denominator)})"


def pair(z):
    return f"ComplexQ({literal(z[0])}, {literal(z[1])})"


def calculate(op, z, w):
    a, b = z
    c, d = w
    if op == "add":
        return a + c, b + d
    if op == "sub":
        return a - c, b - d
    if op == "mul":
        return a * c - b * d, a * d + b * c
    if op in ("div", "inv"):
        if op == "inv":
            a, b, c, d = Q(1), Q(0), a, b
        s = c*c + d*d
        return (a*c + b*d) / s, (b*c - a*d) / s
    if op == "neg":
        return -a, -b
    if op == "conj":
        return a, -b
    if op == "norm_squared":
        return a*a + b*b
    if op == "equal":
        return z == w
    if op == "new":
        return z
    raise ValueError(op)


def emit(op, bits, z, w=(Q(0), Q(0)), source=False, error=None):
    result = calculate(op, z, w) if error is None else None
    if error:
        expected = error, "-"
    elif isinstance(result, bool):
        expected = str(result).lower(), "-"
    elif isinstance(result, tuple):
        expected = text(result[0]), text(result[1])
    else:
        expected = text(result), "-"
    rows.append((op, str(bits), *map(text, z), *map(text, w), *expected, "1" if source else "0"))
    if not source:
        return
    work = 1000000000000000 if bits <= 1024 else 18446744073709551615
    args = f"ExactBudget({bits}, {work}), {pair(z)}"
    if op == "new":
        args = f"ExactBudget({bits}, {work}), {literal(z[0])}, {literal(z[1])}"
    elif op in ("add", "sub", "mul", "div", "equal"):
        args += ", " + pair(w)
    expression = f"complexq_{op}({args})"
    kind = "error" if error else ("equal" if op == "equal" else ("rational" if op == "norm_squared" else "complex"))
    if error:
        expected_source = '"exact complex division by zero"' if error == "domain" else '"UnknownWithinBudget: source precision exceeded"'
    elif kind == "equal":
        expected_source = str(result).lower()
    elif kind == "rational":
        expected_source = literal(result)
    else:
        expected_source = pair(result)
    # Errors from the rational norm API have a different Result payload.
    if error and op == "norm_squared":
        kind = "qerror"
    source_rows.append((kind, expression, expected_source))


rng = random.Random(9800616384)
ops = ("new", "add", "sub", "mul", "div", "neg", "conj", "norm_squared", "equal", "inv")
for case in range(96):
    size = 5 if case < 8 else (16, 64, 128, 256)[case % 4]
    def fraction():
        return Q(rng.randrange(-(1 << size), 1 << size), rng.randrange(1, 1 << size))
    z = fraction(), fraction()
    if z == (0, 0):
        z = Q(0), Q(1)
    w = z if case % 3 == 0 else (fraction(), fraction())
    if w == (0, 0):
        w = Q(1), Q(0)
    for op in ops:
        emit(op, 8*size + 64, z, w, source=case < 8)

# Canonical zeros, signs, Gaussian units and exact rational cancellation.
for z, w in (((Q(0), Q(0)), (Q(0), Q(1))),
             ((Q(0), Q(1)), (Q(0), Q(1))),
             ((Q(1, 2), Q(3, 4)), (Q(-2), Q(1, 3))),
             ((Q(-255), Q(0)), (Q(255), Q(0)))):
    for op in ops:
        if op == "inv" and z == (0, 0):
            continue
        emit(op, 64, z, w, source=True)

for bits in (1025, 4096, 16384):
    a = Q((1 << (bits-1)) + 3)
    z = a, -a
    for op in ("new", "add", "sub", "neg", "conj", "equal"):
        emit(op, bits, z, (Q(1), Q(0)) if op in ("add", "sub") else z, source=True)
    for op in ("mul", "div"):
        emit(op, bits, z, (Q(0), Q(1)), source=True)
    half = Q(1 << ((bits-2)//2))
    emit("norm_squared", bits, (half, half), source=True)
    fractions = Q(1, a.numerator), Q(-1, a.numerator)
    emit("new", bits, fractions, source=True)
    emit("mul", bits, fractions, (a, Q(0)), source=True)
    emit("div", bits, fractions, (Q(0), Q(1)), source=True)
    emit("equal", bits, fractions, fractions, source=True)
    emit("add", bits, fractions, (-fractions[0], -fractions[1]), source=True)
    emit("sub", bits, fractions, fractions, source=True)
    emit("norm_squared", bits, fractions, source=True, error="precision")
    emit("inv", bits, (half, half), source=True)

emit("new", 1, (Q(0), Q(-1)), source=True)
emit("mul", 1, (Q(0), Q(1)), (Q(0), Q(1)), source=True)
emit("inv", 2, (Q(1), Q(1)), source=True)
emit("div", 8, (Q(1), Q(0)), source=True, error="domain")
emit("inv", 8, (Q(0), Q(0)), source=True, error="domain")
emit("norm_squared", 8, (Q(16), Q(16)), source=True, error="precision")
emit("div", 8, (Q(16), Q(16)), (Q(16), Q(16)), source=True, error="precision")
emit("add", 8, (Q(255), Q(0)), (Q(1), Q(0)), source=True, error="precision")
emit("mul", 8, (Q(16), Q(0)), (Q(16), Q(0)), source=True, error="precision")

target = Path(__file__).resolve().parents[1] / "crates/dlm_core/tests/fixtures"
target.joinpath("complex_q098.tsv").write_text(
    "# op\tbits\tar\tai\tbr\tbi\texpected_real_or_error\texpected_imag\tsource\n"
    + "".join("\t".join(row) + "\n" for row in rows), encoding="utf-8")
target.joinpath("complex_q098_source.tsv").write_text(
    "# kind\texpression\texpected_literal\n"
    + "".join("\t".join(row) + "\n" for row in source_rows), encoding="utf-8")
print(f"{len(rows)} host vectors; {len(source_rows)} source/native vectors")
