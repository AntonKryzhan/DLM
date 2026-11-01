#!/usr/bin/env python3
"""Deterministic independent Fraction oracle; cargo consumes the checked-in TSV."""
from fractions import Fraction
from pathlib import Path
import random

MAX = (1 << 128) - 1
small = [Fraction(n, d) for n in range(-3, 4) for d in range(1, 5)]
pairs = [(a, b) for a in small for b in small]
pairs += [
    (Fraction(MAX, 2), Fraction(MAX, 2)),
    (Fraction(MAX, MAX - 1), Fraction(-MAX, MAX - 1)),
    (Fraction(MAX - 1, MAX), Fraction(1, MAX)),
    (Fraction(MAX), Fraction(1)),
    (Fraction(MAX, MAX - 1), Fraction(MAX - 1, MAX)),
    (Fraction(0), Fraction(0)),
    (Fraction(1, MAX - 1), Fraction(1, MAX - 2)),
]
rng = random.Random(0xD1A092)
for _ in range(100):
    def rational():
        n = rng.getrandbits(rng.choice([8, 32, 64, 127, 128]))
        d = rng.getrandbits(rng.choice([8, 32, 64, 127, 128])) or 1
        return Fraction(n if rng.randrange(2) else -n, d)
    pairs.append((rational(), rational()))
lines = ['# op\tsign_a\tnum_a\tden_a\tsign_b\tnum_b\tden_b\texpected']
for a, b in pairs:
    prefix = [str(int(a < 0)), str(abs(a.numerator)), str(a.denominator), str(int(b < 0)), str(abs(b.numerator)), str(b.denominator)]
    for op in ['add', 'sub', 'mul', 'div', 'cmp']:
        try:
            if op == 'cmp': out = str((a > b) - (a < b))
            else:
                result = {'add': lambda: a+b, 'sub': lambda: a-b, 'mul': lambda: a*b, 'div': lambda: a/b}[op]()
                out = 'ERR' if abs(result.numerator) > MAX or result.denominator > MAX else f'{result.numerator}/{result.denominator}'
        except ZeroDivisionError: out = 'ERR'
        lines.append('\t'.join([op] + prefix + [out]))
path = Path(__file__).resolve().parents[1] / 'crates/dlm_core/tests/fixtures/rational128.tsv'
path.write_text('\n'.join(lines) + '\n')
print(f'{len(lines)-1} oracle vectors')
