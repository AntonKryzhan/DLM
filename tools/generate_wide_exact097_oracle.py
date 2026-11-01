"""Deterministic wide arithmetic fixtures from Python int and Fraction."""
from fractions import Fraction
from math import gcd, isqrt
from pathlib import Path
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)
rows = []


def words(n):
    out = []
    while n:
        out.append(str(n & 65535))
        n >>= 16
    return "list(" + ", ".join(out) + ")" if out else "empty_list(Nat)"


def integer(n):
    return f"BigInt({str(n < 0).lower()}, {words(abs(n))})"


def rational(n):
    return f"BigRational({str(n < 0).lower()}, {words(abs(n.numerator))}, {words(n.denominator)})"


def emit(kind, mode, expression, a, b="-"):
    rows.append((kind, mode, expression, a, b))


for bits in (1025, 4096, 16384):
    budget = f"ExactBudget({bits}, 18446744073709551615)"
    a = (1 << (bits - 1)) + 3
    for op, other, value in (("add", 7, a + 7), ("sub", 3, a - 3), ("mul", -1, -a), ("lcm", -1, a)):
        emit("integer", "both", f"big_{op}({budget}, {integer(a)}, {integer(other)})", integer(value))
    r = -a % 65521
    emit("division", "both", f"big_div_rem({budget}, {integer(-a)}, {integer(65521)})", integer((-a-r)//65521), integer(r))
    emit("integer", "both", f"big_gcd({budget}, {integer(a)}, {integer(3)})", integer(gcd(a, 3)))
    emit("integer", "both", f"big_pow({budget}, {integer(-a)}, 1)", integer(-a))
    emit("rational", "both", f"bigrat_add({budget}, {rational(Fraction(a))}, {rational(Fraction(-a))})", rational(Fraction(0)))
    emit("rational", "both", f"bigrat_from_ints({budget}, {integer(-a)}, {integer(a)})", rational(Fraction(-1)))
    emit("rational", "both", f"bigrat_pow({budget}, {rational(Fraction(1,a))}, -1)", rational(Fraction(a)))
b = "ExactBudget(16384, 18446744073709551615)"
a = (1 << 16384) - 1
s = isqrt(a)
emit("root", "stress", f"big_sqrt_rem({b}, {integer(a)})", integer(s), integer(a-s*s))
a, c = 0, 1
for _ in range(9000):
    a, c = c, a + c
emit("integer", "stress", f"big_gcd({b}, {integer(a)}, {integer(c)})", integer(1))
a = 1 << 16383
error = '"UnknownWithinBudget: source precision exceeded"'
emit("error", "both", f"big_mul({b}, {integer(a)}, {integer(a)})", error)
emit("qerror", "both", f"bigrat_mul({b}, {rational(Fraction(a))}, {rational(Fraction(a))})", error)
emit("error", "both", f"big_add({b}, {integer((1<<16384)-1)}, {integer(1)})", error)
target = Path(__file__).resolve().parents[1] / "crates/dlm_core/tests/fixtures/wide_exact097.tsv"
target.write_text("# kind\tmode\texpression\texpected\tremainder\n" + "".join("\t".join(r)+"\n" for r in rows), encoding="utf-8")
target.with_name("wide_decimal097.txt").write_text(str((1<<16384)-1)+"\n", encoding="utf-8")
print(f"{len(rows)} vectors")
