"""Deterministic scalar fixtures from Python int, Fraction, math.lcm/isqrt."""
from fractions import Fraction
from math import isqrt, lcm
from pathlib import Path
import sys

if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

rows = []


def integer_power(a, exponent, source=False):
    value = a ** exponent
    bits = max(1, abs(a).bit_length(), abs(value).bit_length())
    rows.append(("pow", bits, a, exponent, value, "-", int(source)))


def rational_power(a, exponent, source=False):
    value = a ** exponent
    bits = max(1, abs(a.numerator).bit_length(), a.denominator.bit_length(),
               abs(value.numerator).bit_length(), value.denominator.bit_length())
    rows.append(("qpow", bits, f"{a.numerator}/{a.denominator}", exponent,
                 f"{value.numerator}/{value.denominator}", "-", int(source)))


def least_common_multiple(a, b, source=False):
    value = lcm(a, b)
    bits = max(1, abs(a).bit_length(), abs(b).bit_length(), value.bit_length())
    rows.append(("lcm", bits, a, b, value, "-", int(source)))


def square_root(a, source=False):
    value = isqrt(a)
    rows.append(("sqrt", max(1, a.bit_length()), a, 0, value,
                 a - value * value, int(source)))


for a in range(-9, 10):
    for exponent in range(9):
        integer_power(a, exponent, a in (-2, 0, 2) and exponent in (0, 1, 2, 7, 8))
for a in range(-12, 13):
    for b in range(-12, 13):
        least_common_multiple(a, b, a in (-12, 0, 12) and b in (-9, 0, 9))
for numerator in range(-3, 4):
    for denominator in range(1, 5):
        a = Fraction(numerator, denominator)
        for exponent in range(-4, 5):
            if a or exponent >= 0:
                rational_power(a, exponent, denominator == 3 and
                               numerator in (-2, 0, 2) and exponent in (-3, -1, 0, 1, 3))
for a in range(1025):
    square_root(a, a <= 17 or a in (63, 64, 65, 255, 256, 257, 1023, 1024))
for bits in (16, 17, 32, 63, 64, 127, 128, 255, 256, 511, 512, 1023, 1024,
             2048, 4096, 16384):
    square_root((1 << bits) - 1, bits in (17, 32, 128, 256, 512, 1024))
    integer_power(-2, bits - 1, bits in (128, 256, 1024))
for root_bits in (15, 16, 31, 63, 64, 127, 255, 511, 512, 8192):
    root = (1 << root_bits) - 1
    for delta in (-1, 0, 1):
        square_root(root * root + delta, root_bits in (16, 64, 127, 512))
for a in (-1, 0, 1):
    integer_power(a, (1 << 128) - 1, True)
    for exponent in (-(1 << 127), (1 << 127) - 1):
        if a or exponent > 0:
            rational_power(Fraction(a), exponent, True)
for bits in (128, 1024, 4096, 16384):
    a = (1 << bits) - 1
    least_common_multiple(-a, a, bits == 128)
least_common_multiple(-(1 << 127), 6, True)
rational_power(Fraction(-2, 3), -127, True)
rational_power(Fraction(2, 3), 255, True)

target = Path(__file__).resolve().parents[1] / "crates/dlm_core/tests/fixtures/exact_ops096.tsv"
target.write_text("# op\tbits\ta\tb\tvalue\tremainder\tsource\n" +
                  "".join("\t".join(map(str, row)) + "\n" for row in rows), encoding="utf-8")
print(f"{len(rows)} vectors; {sum(row[-1] for row in rows)} source/C vectors")
